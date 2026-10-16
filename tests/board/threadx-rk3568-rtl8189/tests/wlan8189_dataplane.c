/**
 * @file    wlan8189_dataplane.c
 * @brief   Case wlan8189-dataplane: the lwIP netif over the RTL8189FTV driver's
 *          net device, DHCP on the association, a ping to the gateway, and the
 *          iperf server put up for the measurement phase.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  pipeline: lwIP stack bring-up (the shared helper, tests/board/common/
 *          lwip_stack.c), then the presentation layer over this driver's net
 *          device (ports/lwip/lkpinetif.h, the stack half; the netdev half is
 *          ports/lwip/lkpinetif.c), with the association feeding it through the
 *          supplicant backend's forwarding seam
 *          (lkpi_wpa_cfg80211_netif_bind). The case is the WLAN line's dataplane
 *          (tests/board/threadx-rk3568-wlan/tests/wlan_dataplane.c:1-194) with
 *          the net80211 bridge's netif replaced by this line's adapter. The
 *          address comes from DHCP on the association; the case waits for the
 *          lease, proves the gateway answers, prints one diagnostic line from
 *          the adapter's counters so a loss localizes, and leaves the
 *          generic-TCP server on 5001 for the round's measurement.
 *
 * This file lives in lwIP's header world, which owns names the driver world also
 * defines (lwIP's htons against the contract header's), so it never includes the
 * driver's headers: the device lookup and the seam calls are reached by name and
 * through the two headers that cross that boundary (lkpinetif.h,
 * backend_cfg80211.h).
 */

#include <stdio.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"
#include "log.h"

#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/dhcp.h"
#include "lwip/udp.h"
#include "lwip/apps/lwiperf.h"
#include "lwip/stats.h"
#include "lwip/memp.h"

#include "ping.h"

#include "board_suite.h"
#include "wlan8189_cases.h"
#include "wlan8189_census.h"
#include "lkpinetif.h"
#include "lwip_stack.h"
#include "wlan8189_wpa_obs.h"
#include "backend_cfg80211.h"

/* the substitute's device lookup, reached by name: this file lives in lwIP's
 * header world and must not include the driver's headers */
struct wiphy;
struct wiphy *lkpicfg_wiphy(void);

#define WLAN8189_DHCP_WAIT_MS	45000U
#define WLAN8189_PING_WAIT_MS	9000U

/* The measurement window: the host runner powers the board down as soon as the
 * suite's result line prints, so a host-side client connecting to the server
 * below would be cut off mid-run. The case keeps the server up for this bounded
 * window first, long enough for the round's client to connect and finish. The
 * throughput is recorded, not judged, so the window is not a verdict input.
 *
 * The window runs WLAN8189_IPERF_PHASES times in one boot. The sheet is
 * A/B/A/B rather than A/B/A: the pump-cadence round measured the same
 * configuration twice in one boot and saw the two phases differ by 41% (8.28
 * then 11.7), so a single treatment phase against a single control cannot
 * separate a lever under 40% from drift. Two of each, averaged, can.
 *
 * Which lever the sheet moves is a build-time choice (WLAN8189_LEVER_* below):
 * one lever per image keeps each boot's comparison single-variable, which is
 * the round's whole discipline. */
#define WLAN8189_IPERF_WINDOW_MS	20000U
#define WLAN8189_IPERF_PHASES		4U

/* The uplink sheet's own shape: the same phase count and arm order as the
 * downlink sheet, but the window is the library's - lwiperf's client hard-codes
 * its test duration (10 s: settings.amount = -1000 ten-ms units) - so an uplink
 * window is that duration plus the connect and the report's own end. The peer
 * wait bounds the stage's standby: a normal boot's runner sends the peer with
 * its join-time extra steps, minutes before the stage runs, so the wait only
 * ever covers the unconfigured case. */
#define WLAN8189_UPLINK_PHASES		4U
#define WLAN8189_UPLINK_PEER_WAIT_MS	15000U
#define WLAN8189_UPLINK_REPORT_WAIT_MS	25000U

/* the levers this sheet can carry. The census and the engine read the
 * treatments back per phase, so a phase's own line says which one it ran. */
#define WLAN8189_LEVER_PUMP	1	/* the pump cadence: adaptive vs fixed */
#define WLAN8189_LEVER_POLL	2	/* the controller's completion-poll step */
#define WLAN8189_LEVER_AGG	3	/* the card's RX aggregation timeout byte */
#define WLAN8189_LEVER_YIELD	4	/* the pump's fairness yield */
#define WLAN8189_LEVER_DWCDATA	5	/* the controller's data-wait step */
#define WLAN8189_LEVER_IRQ	6	/* the card interrupt (DAT1) vs the polling pump */
#define WLAN8189_LEVER_WAKE	7	/* the pump's band: below-normal vs normal */

#ifndef WLAN8189_LEVER
#define WLAN8189_LEVER	WLAN8189_LEVER_AGG
#endif

/* The align round's drain-to-empty: applied as a constant of whichever boot
 * enables it (the aggregation arm's four phases all run with it), so the
 * aggregation operating point stays the sheet's single variable and drain's
 * own account comes from the census counters (perf rxrel drain=). */
#ifndef WLAN8189_PUMP_DRAIN
#define WLAN8189_PUMP_DRAIN	0
#endif

/* which arm leads: 0 = control on the odd phases (default), 1 = treatment */
#ifndef WLAN8189_ARM_B_FIRST
#define WLAN8189_ARM_B_FIRST	0
#endif

/* the treatments each lever moves between. DWC poll: the controller's
 * completion wait sleeps this long between status reads, so a 100 us step makes
 * every command's completion read as ~100 us even when the hardware finished
 * sooner; the treatment step is the fine end of the same knob.
 *
 * Aggregation timeout: REG_RXDMA_AGG_PG_TH's high byte. The card releases a
 * burst on length OR timeout; the round measured the driver's stock timeout
 * (0x01) as the binding one - the card released ~4 KB per burst against a 15 KB
 * length threshold - and holding it longer was worth +42% / +45% on two tiers
 * across two boots, while going further (0x40) was worse and did not grow the
 * bursts at all. So the confirmed value is the baseline every phase starts
 * from, and the sheet's default treatment is the driver's stock value, which
 * makes an unparameterised boot re-demonstrate the lever it was chosen for.
 * Writing it from the test rather than the driver keeps the vendored tree
 * untouched. */
#define WLAN8189_DWC_POLL_STOCK_US	100U
#ifndef WLAN8189_DWC_POLL_TREAT_US
#define WLAN8189_DWC_POLL_TREAT_US	5U
#endif
/* The data-stage waits' step, the same knob restricted to the waits that cover
 * a data stage (the DTO wait and the IDMAC drain behind it). The eMMC line's
 * controller port runs that wait an order of magnitude finer than its command
 * wait (ports/fsl_sdmmc/sdmmc_host_dwcmshc.c), and this port ran both at one
 * step - so the treatment moves only the data waits, which pay their step once
 * per transfer instead of once per command access. Unlike the global step
 * (which the poll lever's round measured at +5.5% for 22x the wait-loop
 * iterations), this one should buy the same quantisation relief with a
 * fraction of the CPU, and both directions of the link pay it. */
#ifndef WLAN8189_DWC_DATA_BASE_US
#define WLAN8189_DWC_DATA_BASE_US	100U
#endif
#ifndef WLAN8189_DWC_DATA_TREAT_US
#define WLAN8189_DWC_DATA_TREAT_US	5U
#endif
#ifndef WLAN8189_AGG_TIMEOUT_BASE
#define WLAN8189_AGG_TIMEOUT_BASE	0x20u
#endif
#ifndef WLAN8189_AGG_TIMEOUT_TREAT
#define WLAN8189_AGG_TIMEOUT_TREAT	WLAN8189_AGG_TIMEOUT_STOCK
#endif

static struct netif g_netif;

/* the presentation layer's link steps, handed to the backend's forwarding slot */
static void
netif_on_up(void *netif)
{

	lkpinetif_note_link((struct netif *)netif, 1);
}

static void
netif_on_down(void *netif)
{

	lkpinetif_note_link((struct netif *)netif, 0);
}

static int
dataplane_up(void)
{
	ip4_addr_t ip, mask, gw;
	struct wiphy *wiphy;
	unsigned int waited = 0;

	/* 0.0.0.0 first: the interface starts empty and DHCP fills it; the gateway
	 * arrives with the lease */
	IP4_ADDR(&ip, 0, 0, 0, 0);
	IP4_ADDR(&mask, 0, 0, 0, 0);
	IP4_ADDR(&gw, 0, 0, 0, 0);

	if (lwip_stack_up() != 0) {
		board_case_fail("the TCP/IP stack did not come up");
		return -1;
	}

	/*
	 * Bind the adapter to the driver's primary interface. The concurrent-mode
	 * build registers two interfaces (a primary and a virtual one whose address
	 * is derived), and lkpicfg_wiphy() returns the first registered - the
	 * primary - which is the one the association and the eFuse address belong
	 * to.
	 */
	wiphy = lkpicfg_wiphy();
	if (wiphy == NULL || lkpinetif_bind(wiphy) != 0) {
		board_case_fail("the netif did not bind to the driver's interface");
		return -1;
	}
	if (lkpinetif_add(&g_netif, &ip, &mask, &gw) != ERR_OK) {
		board_case_fail("the netif did not assemble over the driver's interface");
		return -1;
	}
	/* the single-slot composition: the supplicant backend owns the substitute's
	 * consumer slot, so the netif's link state arrives through the backend's
	 * forwarding, and the slot is re-armed to the backend's sink */
	lkpi_wpa_cfg80211_netif_bind(&g_netif, netif_on_up, netif_on_down);
	lkpi_wpa_cfg80211_events_arm();

	/* the association normally precedes this case (the join case waited for
	 * it), so the link-up event has already fired into a netif that did not
	 * exist yet: replay it from the observer's latch, or the DHCP client would
	 * wait for a link state that has come and gone */
	if (wlan8189_wpa_obs_connected()) {
		netif_on_up(&g_netif);
	}

	lkpinetif_start(&g_netif);
	LOG_ANCHOR("wlan8189: dataplane up, dhcp started\n");

	while (waited < WLAN8189_DHCP_WAIT_MS) {
		if (!ip4_addr_isany_val(g_netif.ip_addr)) {
			break;
		}
		(void)osDelay(200U);
		waited += 200U;
		/* the lease-hunt pulse: the adapter's counters split "the client's
		 * discovers never reached the radio" (tx stays put) from "they left
		 * but nothing came back" (tx climbs while rx stays 0) */
		if ((waited % 10000U) == 0U) {
			struct lkpinetif_diag d;

			lkpinetif_get_diag(&d);
			LOG_INFO("wlan8189: dhcp wait t=%us rx=%lu eapol=%lu "
			    "tx=%lu tx_fail=%lu link=%u\n", waited / 1000U,
			    d.rx_frames, d.rx_eapol, d.tx_frames, d.tx_fail,
			    netif_is_link_up(&g_netif));
		}
	}
	if (ip4_addr_isany_val(g_netif.ip_addr)) {
		static char reason[160];

		(void)snprintf(reason, sizeof(reason), "no DHCP lease within %u s "
		    "(link=%u assoc=%u)", (unsigned)(WLAN8189_DHCP_WAIT_MS / 1000),
		    netif_is_link_up(&g_netif), wlan8189_wpa_obs_connected());
		board_case_fail(reason);
		return -1;
	}
	LOG_ANCHOR("wlan8189: dhcp bound ip=%u.%u.%u.%u/%u gw=%u.%u.%u.%u\n",
	    ip4_addr1_16(&g_netif.ip_addr), ip4_addr2_16(&g_netif.ip_addr),
	    ip4_addr3_16(&g_netif.ip_addr), ip4_addr4_16(&g_netif.ip_addr),
	    ip4_addr1_16(&g_netif.netmask) == 255 ? 24u : 0u,
	    ip4_addr1_16(&g_netif.gw), ip4_addr2_16(&g_netif.gw),
	    ip4_addr3_16(&g_netif.gw), ip4_addr4_16(&g_netif.gw));
	return 0;
}

/*
 * The UDP receive sink: a datagram socket that only counts what arrives. A UDP
 * blast has no acknowledgement clock - the sender never waits for us - so the
 * sink's byte count is the receive path's own ceiling under the current pump
 * mode, and the difference to the TCP number is what the acknowledgement path
 * costs the sender. The counters reset with the census, so a phase's sink is
 * that phase's burst.
 */
#define WLAN8189_UDP_SINK_PORT	5002U

static struct udp_pcb *g_udp_pcb;
static volatile unsigned int g_udp_pkts;
static volatile unsigned long g_udp_bytes;

static void
udp_sink_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
    const ip_addr_t *addr, u16_t port)
{

	(void)arg;
	(void)pcb;
	(void)addr;
	(void)port;
	if (p != NULL) {
		g_udp_bytes += (unsigned long)p->tot_len;
		g_udp_pkts++;
		pbuf_free(p);
	}
}

/*
 * The lwIP half of the census snapshot: this file owns the lwIP header world,
 * so the stack's own counters print from here (the driver-world half prints
 * from wlan8189_census.c). STAT_COUNTER is lwIP's own width, so the snapshot is
 * per phase and the RUN sums per-phase deltas.
 */
static void
census_print_lwip(void)
{
#if LWIP_STATS && MEM_STATS
	LOG_ANCHOR("perf census: C lwip_mem err=%u avail=%u used=%u max=%u\n",
	    (unsigned)lwip_stats.mem.err, (unsigned)lwip_stats.mem.avail,
	    (unsigned)lwip_stats.mem.used, (unsigned)lwip_stats.mem.max);
#endif
#if LWIP_STATS && TCP_STATS
	LOG_ANCHOR("perf census: C lwip_tcp recv=%u xmit=%u drop=%u chkerr=%u memerr=%u err=%u\n",
	    (unsigned)lwip_stats.tcp.recv, (unsigned)lwip_stats.tcp.xmit,
	    (unsigned)lwip_stats.tcp.drop, (unsigned)lwip_stats.tcp.chkerr,
	    (unsigned)lwip_stats.tcp.memerr, (unsigned)lwip_stats.tcp.err);
#endif
#if LWIP_STATS && MEMP_STATS
	LOG_ANCHOR("perf census: C lwip_msg err=%u max=%u\n",
	    (unsigned)lwip_stats.memp[MEMP_TCPIP_MSG_INPKT]->err,
	    (unsigned)lwip_stats.memp[MEMP_TCPIP_MSG_INPKT]->max);
#endif
#if LWIP_STATS && SYS_STATS
	LOG_ANCHOR("perf census: C lwip_sys mbox_used=%u mbox_max=%u mbox_err=%u\n",
	    (unsigned)lwip_stats.sys.mbox.used, (unsigned)lwip_stats.sys.mbox.max,
	    (unsigned)lwip_stats.sys.mbox.err);
#endif
}

/* ------------------------------------------------------------------ */
/* the phase machinery both sheets share                               */
/* ------------------------------------------------------------------ */

/* The effective state a phase ran under, read back after the arm was applied:
 * the phase line carries this identity so a number can never be read against
 * the wrong treatment. */
struct wlan8189_phase_ident {
	const char	*pump;
	unsigned int	poll_us;
	unsigned int	data_us;
	unsigned int	agg;
	int		yield;
	int		irq;
	int		prio;
	int		drain;
};

/*
 * Put one phase's arm in place: re-assert the full baseline every phase starts
 * from - pump fixed, the adopted aggregation pair, no fairness yield, stock
 * command and data steps - then move the one lever this image was built to
 * measure. Both sheets call this, so a phase is exactly the lever's setting and
 * nothing carried over, and the arm definition cannot drift between the two
 * directions.
 */
static void
phase_apply_levers( int treatment, struct wlan8189_phase_ident *id )
{

	(void)wlan8189_pump_set( "fixed" );
	wlan8189_agg_set( WLAN8189_AGG_STOCK_KB );
	wlan8189_agg_timeout_set( WLAN8189_AGG_TIMEOUT_BASE );
	wlan8189_pump_yield_set( 0 );
	(void)wlan8189_irq_set( 0 );
	(void)wlan8189_pump_prio_set( 0 );
	(void)wlan8189_pump_drain_set( WLAN8189_PUMP_DRAIN );
	id->poll_us = wlan8189_dwc_poll_set( WLAN8189_DWC_POLL_STOCK_US );
	id->data_us = wlan8189_dwc_data_set( WLAN8189_DWC_DATA_BASE_US );

#if WLAN8189_LEVER == WLAN8189_LEVER_PUMP
	id->pump = treatment ? "adaptive" : "fixed";
	(void)wlan8189_pump_set( id->pump );
#elif WLAN8189_LEVER == WLAN8189_LEVER_POLL
	id->pump = "adaptive";
	(void)wlan8189_pump_set( id->pump );
	if( treatment ) {
		id->poll_us = wlan8189_dwc_poll_set(
		    WLAN8189_DWC_POLL_TREAT_US );
	}
#elif WLAN8189_LEVER == WLAN8189_LEVER_AGG
	id->pump = "adaptive";
	(void)wlan8189_pump_set( id->pump );
	if( treatment ) {
		wlan8189_agg_timeout_set( WLAN8189_AGG_TIMEOUT_TREAT );
	}
#elif WLAN8189_LEVER == WLAN8189_LEVER_YIELD
	id->pump = "adaptive";
	(void)wlan8189_pump_set( id->pump );
	wlan8189_pump_yield_set( treatment ? 1 : 0 );
#elif WLAN8189_LEVER == WLAN8189_LEVER_DWCDATA
	id->pump = "adaptive";
	(void)wlan8189_pump_set( id->pump );
	if( treatment ) {
		id->data_us = wlan8189_dwc_data_set(
		    WLAN8189_DWC_DATA_TREAT_US );
	}
#elif WLAN8189_LEVER == WLAN8189_LEVER_IRQ
	/* the card interrupt replaces the poll's discovery role; the cadence
	 * stays on the same adaptive baseline the other levers run under (in
	 * interrupt mode the period degrades to the lost-line backstop) */
	id->pump = "adaptive";
	(void)wlan8189_pump_set( id->pump );
	if( treatment ) {
		(void)wlan8189_irq_set( 1 );
	}
#elif WLAN8189_LEVER == WLAN8189_LEVER_WAKE
	/* the wake chain without the card interrupt: the pump moves into the
	 * driver threads' band, so an AVAL/OQT wake-up does not wait behind
	 * them. Independent of the IRQ lever by construction (it never touches
	 * the interrupt line). */
	id->pump = "adaptive";
	(void)wlan8189_pump_set( id->pump );
	if( treatment ) {
		(void)wlan8189_pump_prio_set( 1 );
	}
#else
#error "WLAN8189_LEVER is not a known lever"
#endif
	id->agg = wlan8189_agg_get();
	id->yield = wlan8189_pump_yield_get();
	id->irq = wlan8189_irq_get();
	id->prio = wlan8189_pump_prio_get();
	id->drain = wlan8189_pump_drain_get();
}

/* The ready line: one shape for both sheets. The downlink's stays the original
 * line byte for byte (the judge anchors and the round's ledgers read it); the
 * uplink's carries its sheet name after the suite prefix. */
static void
phase_print_ready( const char *sheet, unsigned int phase, int treatment,
    const struct wlan8189_phase_ident *id )
{

	LOG_ANCHOR( "wlan8189: %sphase %u ready lever=%u arm=%c pump=%s "
	    "poll_us=%u data_us=%u agg=0x%04x yield=%d lps=%d irq=%d prio=%d "
	    "drain=%d\n",
	    sheet, phase, (unsigned)WLAN8189_LEVER, treatment ? 'B' : 'A',
	    id->pump, id->poll_us, id->data_us, id->agg, id->yield,
	    wlan8189_ps_get(), id->irq, id->prio, id->drain );
}

/* ------------------------------------------------------------------ */
/* the uplink sheet                                                    */
/* ------------------------------------------------------------------ */

/*
 * The reverse direction of the same link under the same lever: the board is
 * the iperf *client* here - the runner's host is the server - because the lwIP
 * stack's client is the end that can drive a send test. The far end's address
 * arrives from the console (`wlan8189 uplink <a.b.c.d>`, sent by the runner's
 * extra steps); with none the stage prints why and leaves the verdict the
 * downlink part earned, so a regression boot - which configures no peer - is
 * not a failure, while a configured window that moved no bytes is one.
 */
static volatile unsigned int g_uplink_peer;	/* host order; 0 = unset */

void
wlan8189_uplink_peer_set( unsigned int peer )
{

	g_uplink_peer = peer;
}

int
wlan8189_uplink_peer_get( unsigned int *peer )
{

	if( g_uplink_peer == 0U || peer == NULL ) {
		return -1;
	}
	*peer = g_uplink_peer;
	return 0;
}

/* the interface this case assembled: the AP case (same boot, after this one)
 * puts the access point's static address on it */
void *
wlan8189_dataplane_netif( void )
{

	return &g_netif;
}

/* the client test's report (fires in the TCP/IP thread; the stage polls the
 * latch, so the two never share a payload struct) */
static volatile unsigned int g_uplink_report_seen;
static volatile unsigned int g_uplink_report_type;
static volatile unsigned long g_uplink_bytes;
static volatile unsigned long g_uplink_ms;
static volatile unsigned long g_uplink_kbit;

static void
uplink_report( void *arg, enum lwiperf_report_type report_type,
    const ip_addr_t *local_addr, u16_t local_port,
    const ip_addr_t *remote_addr, u16_t remote_port,
    u32_t bytes_transferred, u32_t ms_duration, u32_t bandwidth_kbitpsec )
{

	(void)arg;
	(void)local_addr;
	(void)local_port;
	(void)remote_addr;
	(void)remote_port;
	g_uplink_report_type = (unsigned int)report_type;
	g_uplink_bytes = (unsigned long)bytes_transferred;
	g_uplink_ms = (unsigned long)ms_duration;
	g_uplink_kbit = (unsigned long)bandwidth_kbitpsec;
	g_uplink_report_seen = 1U;
}

/*
 * One uplink window: apply the arm, bracket the census, run the client to its
 * own end, and record the far end's own account of what arrived. The peer is in
 * host order; its octets are split out here so no byte-order assumption sits
 * between the console command and lwIP's address type.
 */
static int
uplink_phase_run( unsigned int phase, int treatment, unsigned int peer,
    const char *sheet )
{
	struct wlan8189_phase_ident id;
	ip_addr_t remote;
	void *session;
	unsigned int waited;

	phase_apply_levers( treatment, &id );
	wlan8189_census_reset();
	phase_print_ready( sheet, phase, treatment, &id );

	IP_ADDR4( &remote, (peer >> 24) & 0xffU, (peer >> 16) & 0xffU,
	    (peer >> 8) & 0xffU, peer & 0xffU );

	g_uplink_report_seen = 0U;
	g_uplink_report_type = 0U;
	g_uplink_bytes = 0UL;
	g_uplink_ms = 0UL;
	g_uplink_kbit = 0UL;

	LOCK_TCPIP_CORE();
	session = lwiperf_start_tcp_client( &remote, LWIPERF_TCP_PORT_DEFAULT,
	    LWIPERF_CLIENT, uplink_report, NULL );
	UNLOCK_TCPIP_CORE();
	if( session == NULL ) {
		board_case_fail( "the uplink iperf client did not start" );
		return -1;
	}

	/* the client's own duration (10 s) plus the connect and the report's
	 * delivery; the wait is a ceiling, the report ends it */
	for( waited = 0; waited < WLAN8189_UPLINK_REPORT_WAIT_MS;
	    waited += 100U ) {
		if( g_uplink_report_seen ) {
			break;
		}
		(void)osDelay( 100U );
	}
	if( !g_uplink_report_seen ) {
		LOCK_TCPIP_CORE();
		lwiperf_abort( session );
		UNLOCK_TCPIP_CORE();
		board_case_fail( "the uplink window ended without a report "
		    "from the far end (is the runner's iperf server up?)" );
		return -1;
	}

	/* the sheet name is the whole prefix (the ready line uses the same one
	 * through phase_print_ready): the dataplane's uplink phases print as
	 * `wlan8189: uplink phase N ...`, the AP case's window as
	 * `wlan8189: ap uplink phase N ...`, and neither doubles a word */
	LOG_ANCHOR( "wlan8189: %sphase %u result bytes=%lu ms=%lu "
	    "kbit=%lu type=%u\n", sheet, phase, g_uplink_bytes, g_uplink_ms,
	    g_uplink_kbit, g_uplink_report_type );
	wlan8189_census_print();
	census_print_lwip();
	LOG_ANCHOR( "wlan8189: %sphase %u done\n", sheet, phase );

	if( g_uplink_bytes == 0UL ) {
		board_case_fail( "the uplink window moved no bytes "
		    "(peer reachable and server listening?)" );
		return -1;
	}
	return 0;
}

/*
 * One uplink window toward `peer`, for a case that needs one outside the
 * dataplane sheet (the AP case, whose window the peer measures in): the same
 * machinery the sheet's uplink phases run - the arm is the baseline, the census
 * brackets the window, the result line is the board's own account. Returns 0
 * when the window moved bytes, non-zero otherwise (the named failure is already
 * printed).
 */
int
wlan8189_uplink_window( unsigned int peer, unsigned int phase )
{

	/* the sheet name keeps this window's lines out of the dataplane sheet's
	 * own counts (the judge reconciles those), while the machinery - arm,
	 * census bracket, report - is the same code path */
	return uplink_phase_run( phase, 0, peer, "ap uplink " );
}

void wlan8189_dataplane_chain( void )
{
	unsigned int waited;

	if( dataplane_up() != 0 ) {
		return;
	}

	/*
	 * The gateway reachability window doubles as the round's clean per-frame
	 * cost sample: no iperf client is running yet, so each echo's round trip
	 * is one request frame and one reply frame with nothing queued behind
	 * them, and the floor it shows is what a single frame pays on this path.
	 * The window is split in half and the aggregation timeout moves between
	 * the halves - the adopted value, then the driver's stock value - each
	 * half bracketed by the census, so the floor the timeout imposes is read
	 * in the same boot that carries its throughput.
	 *
	 * One ping thread spans both halves on purpose: lwIP's ping_stop() only
	 * clears the target, it does not join the thread, so a second ping_init
	 * leaves the first thread running and both then answer the same sequence
	 * (the first build of this window did exactly that - 27 sends for 18
	 * expected - and its round trips pair against the wrong send). The thread
	 * is started once and the register is flipped under it.
	 */
	{
		unsigned int pass;

		wlan8189_agg_set( WLAN8189_AGG_STOCK_KB );
		wlan8189_agg_timeout_set( WLAN8189_AGG_TIMEOUT_BASE );
		LOG_INFO( "wlan8189: ping gw window=%u s halves agg=0x%02x%02x "
		    "then 0x%02x%02x\n",
		    (unsigned)( WLAN8189_PING_WAIT_MS / 1000 ),
		    (unsigned)( WLAN8189_AGG_TIMEOUT_BASE & 0xffU ),
		    (unsigned)( WLAN8189_AGG_STOCK_KB & 0xffU ),
		    (unsigned)( WLAN8189_AGG_TIMEOUT_TREAT & 0xffU ),
		    (unsigned)( WLAN8189_AGG_STOCK_KB & 0xffU ) );
		ping_init( &g_netif.gw );
		for( pass = 0U; pass < 2U; pass++ ) {
			if( pass == 1U ) {
				wlan8189_agg_timeout_set(
				    WLAN8189_AGG_TIMEOUT_TREAT );
			}
			LOG_INFO( "wlan8189: ping pass=%u agg=0x%02x%02x\n",
			    pass + 1U,
			    (unsigned)( ( ( pass == 0U ) ?
			    WLAN8189_AGG_TIMEOUT_BASE :
			    WLAN8189_AGG_TIMEOUT_TREAT ) & 0xffU ),
			    (unsigned)( WLAN8189_AGG_STOCK_KB & 0xffU ) );
			wlan8189_census_reset();
			for( waited = 0; waited < WLAN8189_PING_WAIT_MS;
			    waited += 100U ) {
				(void)osDelay( 100U );
			}
			LOG_ANCHOR( "wlan8189: ping window done\n" );
			wlan8189_census_print();
			census_print_lwip();
		}
		ping_stop();
	}

	/* the adapter's own accounting, printed once so a failure that got this
	 * far localizes: rx_frames/rx_eapol are what the driver handed over,
	 * tx_frames/tx_fail what the stack handed down */
	{
		struct lkpinetif_diag d;

		lkpinetif_get_diag( &d );
		LOG_ANCHOR( "wlan8189: diag rx=%lu eapol=%lu tx=%lu fail=%lu\n",
		    d.rx_frames, d.rx_eapol, d.tx_frames, d.tx_fail );
	}

	/* the measurement phase's server: the generic-TCP lwiperf server every
	 * TCP port 5001 client can drive (the round's runner plays the host side
	 * against it); it keeps listening across the phases, one client at a time */
	LOCK_TCPIP_CORE();
	if( lwiperf_start_tcp_server_default( NULL, NULL ) == NULL ) {
		UNLOCK_TCPIP_CORE();
		board_case_fail( "the iperf server did not start" );
		return;
	}
	UNLOCK_TCPIP_CORE();
	LOG_ANCHOR( "wlan8189: iperf server ready port=%u\n",
	    LWIPERF_TCP_PORT_DEFAULT );

	/* the UDP ceiling probe's sink (the host blasts datagrams at it; the
	 * receive path drains them with no acknowledgement clock in the way) */
	LOCK_TCPIP_CORE();
	g_udp_pcb = udp_new();
	if( g_udp_pcb != NULL &&
	    udp_bind( g_udp_pcb, IP_ANY_TYPE, WLAN8189_UDP_SINK_PORT ) == ERR_OK ) {
		udp_recv( g_udp_pcb, udp_sink_recv, NULL );
	} else {
		UNLOCK_TCPIP_CORE();
		board_case_fail( "the UDP sink did not bind" );
		return;
	}
	UNLOCK_TCPIP_CORE();
	LOG_ANCHOR( "wlan8189: udp sink ready port=%u\n",
	    WLAN8189_UDP_SINK_PORT );

	/*
	 * The phases (see the defines): A/B/A/B around one lever, equal windows in
	 * one boot, so the four numbers are comparable and the two treatment phases
	 * average out the drift the earlier single-treatment sheet could not tell
	 * from its lever. Every phase re-asserts the full baseline state - pump
	 * cadence, controller command and data steps, card aggregation pair,
	 * fairness yield - so a phase is exactly the lever's setting and nothing
	 * carried over. Power save stays at the driver's built-in policy (the
	 * latency matrix measured power a secondary lever). The census reset and
	 * snapshot bracket each window, and the phase line prints the effective
	 * settings so the numbers carry their own identity. The same machinery runs
	 * the uplink sheet below, one arm definition for both directions.
	 */
	{
		unsigned int phase;

		for( phase = 1U; phase <= WLAN8189_IPERF_PHASES; phase++ ) {
			/* which arm a phase runs is a build choice: the default puts the
			 * control first (arm A on the odd phases), and the confirmatory
			 * boot runs the treatment first so both of its samples land before
			 * the last phase, which is where this sheet's predecessor wedged */
#if WLAN8189_ARM_B_FIRST
			const int treatment = ((phase % 2U) == 1U);
#else
			const int treatment = ((phase % 2U) == 0U);
#endif
			struct wlan8189_phase_ident id;

			phase_apply_levers( treatment, &id );

			g_udp_pkts = 0U;
			g_udp_bytes = 0UL;
			wlan8189_census_reset();
			phase_print_ready( "", phase, treatment, &id );
			LOG_ANCHOR( "wlan8189: iperf server ready port=%u\n",
			    LWIPERF_TCP_PORT_DEFAULT );
			LOG_ANCHOR( "wlan8189: iperf window %u s\n",
			    (unsigned)( WLAN8189_IPERF_WINDOW_MS / 1000U ) );
			for( waited = 0; waited < WLAN8189_IPERF_WINDOW_MS; waited += 200U ) {
				(void)osDelay( 200U );
			}
			LOG_ANCHOR( "wlan8189: iperf window done\n" );
			LOG_ANCHOR( "wlan8189: udp sink pkts=%u bytes=%lu\n",
			    g_udp_pkts, g_udp_bytes );
			wlan8189_census_print();
			census_print_lwip();
			LOG_ANCHOR( "wlan8189: phase %u done\n", phase );
		}
	}

	/*
	 * The uplink sheet - the reverse direction of the same link under the same
	 * lever (see the machinery above). Standby first: the runner's host server
	 * must be listening before the client's connect, so this line is its cue,
	 * and the peer address arrives from the console (the runner's extra steps)
	 * long before. Without a peer the stage says why and leaves the case's
	 * verdict exactly as the downlink part earned it.
	 */
	LOG_ANCHOR( "wlan8189: uplink standby (configure with 'wlan8189 uplink "
	    "<peer-ip>')\n" );
	{
		unsigned int phase;
		unsigned int uplink_peer = 0U;

		for( waited = 0; waited < WLAN8189_UPLINK_PEER_WAIT_MS;
		    waited += 100U ) {
			if( wlan8189_uplink_peer_get( &uplink_peer ) == 0 ) {
				break;
			}
			(void)osDelay( 100U );
		}
		if( wlan8189_uplink_peer_get( &uplink_peer ) != 0 ) {
			LOG_ANCHOR( "wlan8189: uplink skipped reason=no-peer\n" );
			return;
		}

		for( phase = 1U; phase <= WLAN8189_UPLINK_PHASES; phase++ ) {
			int treatment;

#if WLAN8189_ARM_B_FIRST
			treatment = ((phase % 2U) == 1U);
#else
			treatment = ((phase % 2U) == 0U);
#endif
			if( uplink_phase_run( phase, treatment,
			    uplink_peer, "uplink " ) != 0 ) {
				return;
			}
		}
	}
}

BOARD_TEST( "wlan8189-dataplane", wlan8189_dataplane_chain );

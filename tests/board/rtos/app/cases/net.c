/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   net.c
 * @brief  Case net: lwIP over the board's GMAC - quiet-link checks, a TCP
 *         baseline in both directions, and the measurement tier a throughput
 *         round reads.
 *
 * Topology (the lab's reliable port, per the board's MAC contract): gmac1 at
 * fe010000, MAC 02:e4:a5:35:68:01, static 192.168.0.200/24, peer the Raspberry
 * Pi at 192.168.0.17 on the same switch.  Static addressing keeps the case
 * independent of a DHCP lease that the lab's other lines also hold.
 *
 * What is claimed as evidence, in order:
 *   net: link ...             PHY autoneg result and the MAC's configured mode
 *   net: addr ...             the address and the contract MAC actually read back
 *   net: ping tx/rx           ten echo requests, counted at both ends (the peer
 *                             side is counted by the board runner)
 *   net: rtt ...              the same echoes' round trip, min/avg/max in us
 *   net: arp ...              the peer's resolved entry, read from the stack
 *   net: phase <n> begin|done the phase machine (the plan table below)
 *   net: iperf down ...       a peer-to-board TCP session (the board serves)
 *   net: iperf up ...         a board-to-peer TCP session (the board initiates)
 *   net: udp sink|blast ...   capacity probes: the stack's own iperf app is TCP
 *                             only (third-party/lwip/src/apps/lwiperf/lwiperf.c:15
 *                             "implement UDP mode"), so the UDP leg is a
 *                             counting sink on 5002 and a blast to the peer's
 *                             iperf UDP server
 *   net: loopback ...         a board-local session (127.0.0.1) - attribution
 *                             only, never a pass criterion: both endpoints
 *                             share one TCP/IP thread, so it bounds "the stack
 *                             alone" from above, it does not measure the wire
 *   net: census <phase> ...   per-phase deltas of the driver's and the bridge's
 *                             own buckets (ticks of the 24 MHz counter)
 *
 * Why the phases exist: a throughput number without the phase boundary is a
 * number without a denominator. Each phase announces itself BEFORE it waits, so
 * the peer driver (tools/rtos/board-round.py --plan) fires the matching iperf
 * command on the anchor instead of on a sleep, and each phase closes with the
 * census delta of exactly that window.
 *
 * The plan is a build knob: NET_PHASE_PLAN=0 is the delivery profile (one
 * downlink window, one uplink - the schedule every earlier boot was judged on),
 * 1 is the throughput round's schedule (loopback, tcp-down, udp-down, tcp-down
 * with a large peer window, udp-down again, tcp-up, udp-up). Both plans share
 * the same dispatchers, so the legacy anchors and the pass criteria are
 * identical in either plan.
 *
 * Every wait is bounded: a missing peer fails the phase, it does not hang the
 * image.
 */

#include "cases.h"

#include "os/cmsis2/cmsis2.h"

#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/etharp.h"
#include "lwip/raw.h"
#include "lwip/udp.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/apps/lwiperf.h"

#include "dwc_eqos.h"
#include "eqos_rk3568.h"
#include "rtl8211f.h"

#include "ethnetif.h"

#define NET_PORT		1u
#define NET_IP_A		192u
#define NET_IP_B		168u
#define NET_IP_C		0u
#define NET_IP_D		200u
#define NET_MASK_A		255u
#define NET_MASK_B		255u
#define NET_MASK_C		255u
#define NET_MASK_D		0u
#define NET_GW_D		1u
#define PEER_IP_D		17u

#define PING_COUNT		10u
/* The bound on waiting for one echo's reply - not a pacing interval: the loop
 * waits for the answer instead of sleeping between requests, which is why ten
 * pings finish in about a second rather than in PING_COUNT * 200 ms. */
#define PING_REPLY_WAIT_MS	300u
#define PING_TRACK		32u
#define IPERF_DOWN_WINDOW_MS	40000u
#define IPERF_UP_WAIT_MS	15000u
#define LINK_WAIT_MS		10000u
#define PING_ID			0x5a5au

/* UDP capacity probes. 5002 is the sink; the blast goes to the peer's iperf
 * UDP server on 5001. 1470 bytes of payload + 8 UDP + 20 IP = 1498, so a
 * datagram never fragments on this path. */
#define NET_UDP_SINK_PORT	5002u
#define NET_UDP_BLAST_PORT	5001u
#define NET_UDP_BLAST_LEN	1470u
/* Datagrams per core-lock hold: the blast has to leave the lock often enough
 * for the TCP/IP thread and the RX service to run, or the board measures its
 * own starvation instead of the wire. */
#define NET_UDP_BLAST_BURST	64u

/* The phase plan (see the file comment). Overridable from the build:
 *   LEVERS=-DNET_PHASE_PLAN=1 tools/rtos/build.sh */
#ifndef NET_PHASE_PLAN
#define NET_PHASE_PLAN 0
#endif

/* Whether the RX thread re-arms the ring on every notification, before it
 * drains.
 *
 * The re-arm is what puts the ring back in front of the DMA after a sweep, so
 * it is NOT optional for the full-ring sweep: with it off the ring stays
 * disarmed until the 100 ms service thread pokes it, and the link degrades to
 * one 100 ms burst at a time. Measured 2026-10-25 on the first arm of this
 * round: ICMP RTT went 0.2 ms -> 89 ms and a 128 KiB-window TCP stream fell to
 * 1.4 Mbit/s (peer-side report), which is RTT-bound behaviour, not loss
 * behaviour. So the default is ON and the lever exists for one purpose only.
 *
 * That purpose is the early-stop sweep (CONFIG_GMAC_RX_SCAN=5): an unconditional
 * re-arm publishes the ring's tail, which overwrites the arrive-ordered cursor's
 * landing point, and the DMA then runs into slots nobody re-armed (the 2026-10-25
 * b13 arm measured -25% that way). Turning the re-arm off is therefore only
 * coherent together with the early-stop scan - and even then the 100 ms service
 * thread is the only thing that re-arms, so that arm is registered as a
 * mechanism test, not as a candidate default. */
#ifndef CONFIG_GMAC_WATCHDOG_ON_NOTIFY
#if defined(CONFIG_GMAC_RX_SCAN) && (CONFIG_GMAC_RX_SCAN == 5)
#define CONFIG_GMAC_WATCHDOG_ON_NOTIFY 0
#else
#define CONFIG_GMAC_WATCHDOG_ON_NOTIFY 1
#endif
#endif

enum net_phase_kind {
	PH_DOWN_TCP,
	PH_UP_TCP,
	PH_DOWN_UDP,
	PH_UP_UDP,
	PH_LOOPBACK,
};

struct net_phase {
	const char *name;	/* the census tag and the peer driver's key */
	const char *proto;
	const char *role;
	enum net_phase_kind kind;
	u16_t port;
	unsigned window_ms;
};

#if NET_PHASE_PLAN == 1
/* The throughput round's schedule. tcpdn16k/tcpdn256k are the same board phase
 * with two different peer send windows: the pair separates "our receive path is
 * slow" from "the peer's window paces us". The UDP phases repeat and never
 * lead, because a cold first UDP phase under-reports. */
static const struct net_phase net_plan[] = {
	{ "loopback",  "tcp", "client", PH_LOOPBACK,  5001u,
	  IPERF_DOWN_WINDOW_MS },
	{ "tcpdn16k",  "tcp", "server", PH_DOWN_TCP,  5001u,
	  IPERF_DOWN_WINDOW_MS },
	{ "udpdn",     "udp", "server", PH_DOWN_UDP,  NET_UDP_SINK_PORT, 20000u },
	{ "tcpdn256k", "tcp", "server", PH_DOWN_TCP,  5001u, 25000u },
	{ "udpdn2",    "udp", "server", PH_DOWN_UDP,  NET_UDP_SINK_PORT, 20000u },
	{ "tcpup",     "tcp", "client", PH_UP_TCP,    5001u, IPERF_UP_WAIT_MS },
	{ "udpup",     "udp", "client", PH_UP_UDP,    NET_UDP_BLAST_PORT,
	  12000u },
};
#else
/* The delivery profile: exactly the two sessions every earlier boot was judged
 * on, in the same order, with the same windows. */
static const struct net_phase net_plan[] = {
	{ "down", "tcp", "server", PH_DOWN_TCP, 5001u, IPERF_DOWN_WINDOW_MS },
	{ "up",   "tcp", "client", PH_UP_TCP,   5001u, IPERF_UP_WAIT_MS },
};
#endif

#define NET_PLAN_LEN	(sizeof(net_plan) / sizeof(net_plan[0]))

static struct netif netif;
static osSemaphoreId_t rx_sem;
static osSemaphoreId_t stack_ready;
static osThreadId_t rx_thread;
static ip_addr_t peer;
static struct raw_pcb *ping_pcb;
static struct udp_pcb *udp_sink;
static volatile unsigned ping_tx;
static volatile unsigned ping_rx;
static volatile unsigned ping_seen;	/* every ICMP packet the pcb was handed */
static int net_up;

/* Echo round trip (throughput-round instrument): the send instant per sequence
 * number, so a reply's arrival prices one round trip and nothing else. The
 * bounded wait above could only ever say "under 300 ms". */
static uint64_t ping_sent_us[PING_TRACK];
static unsigned ping_rtt_n;
static unsigned ping_rtt_min_us;
static unsigned ping_rtt_max_us;
static uint64_t ping_rtt_sum_us;

/* iperf sessions: the board serves the downlink and initiates the uplink. */
static volatile unsigned down_bytes;
static volatile unsigned down_ms;
static volatile unsigned down_kbit;
static volatile int down_done;
static volatile unsigned up_bytes;
static volatile unsigned up_ms;
static volatile unsigned up_kbit;
static volatile int up_done;

/* The loopback session borrows the client report type, so it needs its own
 * route out of the callback - and its own numbers: a board-local session must
 * never be folded into the wire's uplink. */
static volatile int lo_active;
static volatile unsigned lo_bytes;
static volatile unsigned lo_ms;
static volatile unsigned lo_kbit;
static volatile int lo_done;

/* UDP sink counters, reset at the start of every UDP phase so one phase is one
 * number. */
static volatile unsigned udp_pkts;
static volatile unsigned udp_bytes;

static void iperf_report(void *arg, enum lwiperf_report_type type,
			 const ip_addr_t *local_addr, u16_t local_port,
			 const ip_addr_t *remote_addr, u16_t remote_port,
			 u32_t bytes, u32_t ms, u32_t kbitpsec)
{
	(void)arg;
	(void)local_addr;
	(void)local_port;
	(void)remote_addr;
	(void)remote_port;

	CASE_ANCHOR("net: iperf report type=%d bytes=%lu ms=%lu kbit=%lu\n",
		    (int)type, (unsigned long)bytes, (unsigned long)ms,
		    (unsigned long)kbitpsec);

	if (type == LWIPERF_TCP_DONE_SERVER) {
		down_bytes = (unsigned)bytes;
		down_ms = (unsigned)ms;
		down_kbit = (unsigned)kbitpsec;
		down_done = 1;
	} else if (type == LWIPERF_TCP_DONE_CLIENT) {
		if (lo_active != 0) {
			lo_bytes = (unsigned)bytes;
			lo_ms = (unsigned)ms;
			lo_kbit = (unsigned)kbitpsec;
			lo_done = 1;
		} else {
			up_bytes = (unsigned)bytes;
			up_ms = (unsigned)ms;
			up_kbit = (unsigned)kbitpsec;
			up_done = 1;
		}
	}
}

/* GMAC ISR context: frames landed in the driver's staging ring. */
static void net_rx_notify(unsigned int index)
{
	(void)index;
	(void)osSemaphoreRelease(rx_sem);
}

static void net_rx_thread(void *arg)
{
	(void)arg;

	for (;;) {
		(void)osSemaphoreAcquire(rx_sem, osWaitForever);
#if CONFIG_GMAC_WATCHDOG_ON_NOTIFY
		/* The ISR hands the sweep to this thread: the watchdog re-arms
		 * the ring and re-pokes the line before the drain.  Skipping it
		 * leaves the first interrupt's work done but the ring disarmed.
		 * Off by default - the 100 ms service thread below re-arms the
		 * ring, and doing it here costs an early-stop sweep its landing
		 * point (see CONFIG_GMAC_WATCHDOG_ON_NOTIFY). */
		dwc_eqos_watchdog(NET_PORT);
#endif
		(void)ethnetif_poll(&netif);
	}
}

static void net_service_thread(void *arg)
{
	(void)arg;

	for (;;) {
		(void)osDelay(100u);
		dwc_eqos_watchdog(NET_PORT);
	}
}

static void stack_done(void *arg)
{
	(void)arg;
	CASE_ANCHOR("net: stack ready lwip=%s\n", LWIP_VERSION_STRING);
	(void)osSemaphoreRelease(stack_ready);
}

static int stack_up(void)
{
	stack_ready = osSemaphoreNew(1u, 0u, NULL);
	if (stack_ready == NULL) {
		return -1;
	}

	tcpip_init(stack_done, NULL);

	if (osSemaphoreAcquire(stack_ready, 5000u) != osOK) {
		return -1;
	}

	return 0;
}

/* --- census ----------------------------------------------------------------- */

/* One phase's worth of the driver's and the bridge's own accounting. Every
 * field is a delta between two snapshots, so a phase's numbers cannot carry a
 * previous phase's traffic. Ticks are the 24 MHz system counter - 24 per
 * microsecond - and the report divides at the edge so the printed unit stays
 * honest. */
struct net_snap {
	uint64_t rx_frames;
	uint64_t rx_irqs;
	uint64_t walk_slots;
	uint64_t sweeps;
	uint64_t tx_frames;
	uint64_t irq_ticks;
	uint64_t sweep_ticks;
	uint64_t copy_ticks;
	uint64_t txf_ticks;
	uint64_t txcpy_ticks;
	uint64_t flush_ticks;
	uint64_t gap_samples;
	uint64_t gap_fast;	/* b0..b7: <= ~10.7 us apart */
	uint64_t gap_slow;	/* b15..b17: >= ~1.37 ms apart */
	uint64_t fed;
	uint64_t polls;
	uint64_t pool_fail;
	uint64_t input_fail;
	uint64_t prep_ticks;
	uint64_t input_ticks;
	uint64_t input_calls;
	uint64_t shape_data;
	uint64_t shape_ack;
	uint64_t shape_dup;
	uint64_t ovf;
	uint64_t crc;
	uint64_t mac_pkts;
	uint64_t mac_octets;
	uint64_t csum_drop;
	uint64_t csum_frames;
	uint64_t tx_waits;
	uint64_t tx_wait_ticks;
	/* Driver-side receive losses and the zero-copy spare pool. The first is
	 * the guard for every receive arm ("the MAC handed us N frames, did the
	 * ring deliver N?"); the second only moves in ZC builds and answers
	 * "did the stack hold frames longer than the spare pool could cover". */
	uint64_t rx_dropped;
	uint64_t rx_free;
	/* Input-call split by frame class (INPUTSPLIT builds): wall time blocked
	 * on the core lock versus the ethernet_input body. Indexed with the
	 * ethnetif class ids; only DATA and ACK are reported, the rest are not
	 * what the round is about. */
	uint64_t split_wait[ETHNETIF_CLASSES];
	uint64_t split_proc[ETHNETIF_CLASSES];
	uint64_t split_calls[ETHNETIF_CLASSES];
};

static void net_snap_take(struct net_snap *s)
{
	struct dwc_eqos_prof prof;
	struct dwc_eqos_diag diag;
	struct dwc_eqos_stats stats;
	struct dwc_eqos_mmc_stats mmc;
	struct ethnetif_shape rx;
	struct ethnetif_shape tx;
	unsigned long poll_calls;
	unsigned long pool_fail;
	unsigned long input_fail;
	unsigned long fed;
	unsigned long input_calls;
	uint64_t prep_ticks;
	uint64_t input_ticks;
	unsigned b;

	(void)memset(s, 0, sizeof(*s));

	dwc_eqos_get_prof(NET_PORT, &prof);
	dwc_eqos_get_diag(NET_PORT, &diag);
	dwc_eqos_get_stats(NET_PORT, &stats);
	dwc_eqos_get_mmc_stats(NET_PORT, &mmc);
	ethnetif_get_diag(&poll_calls, &pool_fail, &input_fail, &fed);
	ethnetif_get_stack_prof(&prep_ticks, &input_ticks, &input_calls);
	ethnetif_get_shape(&rx, &tx);

	s->rx_frames = prof.rx_frames;
	s->rx_irqs = stats.rx_irqs;
	s->walk_slots = prof.rx_walk_slots;
	s->sweeps = prof.rx_sweeps;
	s->tx_frames = prof.tx_frames;
	s->irq_ticks = prof.irq_ticks;
	s->sweep_ticks = prof.irq_rx_sweep_ticks;
	s->copy_ticks = prof.irq_rx_copy_ticks;
	s->txf_ticks = prof.tx_total_ticks;
	s->txcpy_ticks = prof.tx_memcpy_ticks;
	s->flush_ticks = prof.tx_flush_ticks;
	s->gap_samples = prof.rx_gap_samples;
	for (b = 0u; b < 8u; b++) {
		s->gap_fast += prof.rx_gap_hist[b];
	}
	for (b = 15u; b < DWC_EQOS_PROF_HIST_BUCKETS; b++) {
		s->gap_slow += prof.rx_gap_hist[b];
	}
	s->fed = fed;
	s->polls = poll_calls;
	s->pool_fail = pool_fail;
	s->input_fail = input_fail;
	s->prep_ticks = prep_ticks;
	s->input_ticks = input_ticks;
	s->input_calls = input_calls;
	s->shape_data = rx.data;
	s->shape_ack = rx.pure_ack;
	s->shape_dup = rx.ack_dup;
	s->ovf = mmc.rx_fifo_overflow;
	s->crc = mmc.rx_crc_error;
	s->mac_pkts = mmc.rx_pkts_gb;
	s->mac_octets = mmc.rx_octets_gb;
	s->csum_drop = stats.rx_csum_drops;
	s->csum_frames = stats.tx_csum_frames;
	s->tx_waits = diag.tx_waits;
	s->tx_wait_ticks = diag.tx_wait_ticks;
	s->rx_dropped = stats.rx_dropped;
	s->rx_free = diag.rx_free_count;
	{
		uint64_t split_wait[ETHNETIF_CLASSES];
		uint64_t split_proc[ETHNETIF_CLASSES];
		unsigned long split_calls[ETHNETIF_CLASSES];

		ethnetif_get_input_split(split_wait, split_proc, split_calls);
		for (b = 0u; b < (unsigned)ETHNETIF_CLASSES; b++) {
			s->split_wait[b] = split_wait[b];
			s->split_proc[b] = split_proc[b];
			s->split_calls[b] = split_calls[b];
		}
	}
}

static void net_census_report(const char *tag, const struct net_snap *a,
			      const struct net_snap *b)
{
	CASE_ANCHOR("net: census %s rx_frames=%lu rx_irqs=%lu irq_us=%lu "
		    "sweep_us=%lu copy_us=%lu walk_slots=%lu sweeps=%lu\n",
		    tag,
		    (unsigned long)(b->rx_frames - a->rx_frames),
		    (unsigned long)(b->rx_irqs - a->rx_irqs),
		    (unsigned long)((b->irq_ticks - a->irq_ticks) / 24u),
		    (unsigned long)((b->sweep_ticks - a->sweep_ticks) / 24u),
		    (unsigned long)((b->copy_ticks - a->copy_ticks) / 24u),
		    (unsigned long)(b->walk_slots - a->walk_slots),
		    (unsigned long)(b->sweeps - a->sweeps));

	CASE_ANCHOR("net: census %s tx_frames=%lu txf_us=%lu txcpy_us=%lu "
		    "flush_us=%lu tx_waits=%lu tx_wait_ticks=%lu\n",
		    tag,
		    (unsigned long)(b->tx_frames - a->tx_frames),
		    (unsigned long)((b->txf_ticks - a->txf_ticks) / 24u),
		    (unsigned long)((b->txcpy_ticks - a->txcpy_ticks) / 24u),
		    (unsigned long)((b->flush_ticks - a->flush_ticks) / 24u),
		    (unsigned long)(b->tx_waits - a->tx_waits),
		    (unsigned long)(b->tx_wait_ticks - a->tx_wait_ticks));

	CASE_ANCHOR("net: census %s fed=%lu polls=%lu poolfail=%lu "
		    "inputfail=%lu prep_us=%lu input_us=%lu calls=%lu\n",
		    tag,
		    (unsigned long)(b->fed - a->fed),
		    (unsigned long)(b->polls - a->polls),
		    (unsigned long)(b->pool_fail - a->pool_fail),
		    (unsigned long)(b->input_fail - a->input_fail),
		    (unsigned long)((b->prep_ticks - a->prep_ticks) / 24u),
		    (unsigned long)((b->input_ticks - a->input_ticks) / 24u),
		    (unsigned long)(b->input_calls - a->input_calls));

	CASE_ANCHOR("net: census %s data=%lu ack=%lu dup=%lu ovf=%lu crc=%lu "
		    "csum_drop=%lu csum_tx=%lu gap_n=%lu gap_le10us=%lu "
		    "gap_ge1ms=%lu\n",
		    tag,
		    (unsigned long)(b->shape_data - a->shape_data),
		    (unsigned long)(b->shape_ack - a->shape_ack),
		    (unsigned long)(b->shape_dup - a->shape_dup),
		    (unsigned long)(b->ovf - a->ovf),
		    (unsigned long)(b->crc - a->crc),
		    (unsigned long)(b->csum_drop - a->csum_drop),
		    (unsigned long)(b->csum_frames - a->csum_frames),
		    (unsigned long)(b->gap_samples - a->gap_samples),
		    (unsigned long)(b->gap_fast - a->gap_fast),
		    (unsigned long)(b->gap_slow - a->gap_slow));

	/* The MAC's own view: how many frames the wire actually handed it, next to
	 * how many the ring delivered. A gap between the two is a drop inside the
	 * MAC (its FIFO overflowed); no gap means the traffic never arrived and the
	 * ceiling upstream of this port is someone else's. */
	CASE_ANCHOR("net: census %s mac_pkts=%lu mac_octets=%lu\n",
		    tag,
		    (unsigned long)(b->mac_pkts - a->mac_pkts),
		    (unsigned long)(b->mac_octets - a->mac_octets));

	/* Receive losses and the zero-copy spare pool. The first is the guard
	 * every receive arm is judged on: the MAC's own count and the ring's
	 * deliveries must add up, so a drop is attributed to the driver rather
	 * than to the wire. The second only moves in zero-copy builds and
	 * answers "did the stack hold frames longer than the pool could cover"
	 * (a shrinking pool with rising rx_dropped is that, exactly). */
	CASE_ANCHOR("net: census %s rx_dropped=%lu rx_free=%lu\n",
		    tag,
		    (unsigned long)(b->rx_dropped - a->rx_dropped),
		    (unsigned long)b->rx_free);

	/* The input call torn in two for the two classes the round is aimed at
	 * (INPUTSPLIT builds; zeros otherwise): what the drain spent blocked on
	 * the core lock, and what the stack's own ethernet entry then took. The
	 * uplink's per-ACK cost and the downlink's per-segment cost are the same
	 * measurement on different frame classes. */
	CASE_ANCHOR("net: census %s split data_wait_us=%lu data_proc_us=%lu "
		    "data_calls=%lu ack_wait_us=%lu ack_proc_us=%lu "
		    "ack_calls=%lu\n",
		    tag,
		    (unsigned long)((b->split_wait[ETHNETIF_CLASS_DATA] -
				     a->split_wait[ETHNETIF_CLASS_DATA]) / 24u),
		    (unsigned long)((b->split_proc[ETHNETIF_CLASS_DATA] -
				     a->split_proc[ETHNETIF_CLASS_DATA]) / 24u),
		    (unsigned long)(b->split_calls[ETHNETIF_CLASS_DATA] -
				    a->split_calls[ETHNETIF_CLASS_DATA]),
		    (unsigned long)((b->split_wait[ETHNETIF_CLASS_ACK] -
				     a->split_wait[ETHNETIF_CLASS_ACK]) / 24u),
		    (unsigned long)((b->split_proc[ETHNETIF_CLASS_ACK] -
				     a->split_proc[ETHNETIF_CLASS_ACK]) / 24u),
		    (unsigned long)(b->split_calls[ETHNETIF_CLASS_ACK] -
				    a->split_calls[ETHNETIF_CLASS_ACK]));
}

/* --- ping ------------------------------------------------------------------- */

/* A raw pcb is handed the packet from its IP header on (lwIP's raw_input
 * reads the protocol out of that header and passes the same pbuf through), so
 * the ICMP header starts after the IP header the sender wrote. */
static u8_t ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p,
		      const ip_addr_t *addr)
{
	struct ip_hdr *iphdr;
	struct icmp_echo_hdr *iecho;
	u16_t hlen;

	(void)arg;
	(void)pcb;
	(void)addr;

	if (p->len < sizeof(struct ip_hdr)) {
		return 0;
	}
	iphdr = (struct ip_hdr *)p->payload;
	if (IPH_V(iphdr) != 4u) {
		return 0;
	}
	hlen = IPH_HL_BYTES(iphdr);
	if (p->len < (u16_t)(hlen + sizeof(struct icmp_echo_hdr))) {
		return 0;
	}

	ping_seen++;
	iecho = (struct icmp_echo_hdr *)((u8_t *)p->payload + hlen);

	if ((ICMPH_TYPE(iecho) == ICMP_ER) &&
	    (iecho->id == PP_HTONS(PING_ID))) {
		u16_t seq = lwip_ntohs(iecho->seqno);
		uint64_t sent = ping_sent_us[seq % PING_TRACK];
		unsigned rtt_us = (sent != 0u) ?
			(unsigned)(rtos_clock_us() - sent) : 0u;

		ping_rx++;
		/* A stale stamp (a duplicate reply from an earlier round) would
		 * price a negative interval; the one-second ceiling keeps that
		 * out of the statistics without hiding a genuinely slow echo. */
		if ((sent != 0u) && (rtt_us < 1000000u)) {
			ping_rtt_n++;
			ping_rtt_sum_us += rtt_us;
			if ((ping_rtt_min_us == 0u) ||
			    (rtt_us < ping_rtt_min_us)) {
				ping_rtt_min_us = rtt_us;
			}
			if (rtt_us > ping_rtt_max_us) {
				ping_rtt_max_us = rtt_us;
			}
		}
		pbuf_free(p);
		return 1;
	}

	return 0;
}

static void ping_send(u16_t seq)
{
	struct pbuf *p;
	struct icmp_echo_hdr *iecho;
	uint8_t *payload;
	u16_t len = (u16_t)(sizeof(struct icmp_echo_hdr) + 8u);
	unsigned i;

	p = pbuf_alloc(PBUF_IP, len, PBUF_RAM);
	if (p == NULL) {
		return;
	}

	iecho = (struct icmp_echo_hdr *)p->payload;
	ICMPH_TYPE_SET(iecho, ICMP_ECHO);
	ICMPH_CODE_SET(iecho, 0);
	iecho->chksum = 0;
	iecho->id = PP_HTONS(PING_ID);
	iecho->seqno = lwip_htons(seq);

	payload = (uint8_t *)p->payload + sizeof(struct icmp_echo_hdr);
	for (i = 0u; i < 8u; i++) {
		payload[i] = (uint8_t)(0xa5u ^ (uint8_t)(seq + i));
	}

	iecho->chksum = inet_chksum(iecho, p->len);

	ping_sent_us[seq % PING_TRACK] = rtos_clock_us();
	if (raw_sendto(ping_pcb, p, &peer) == ERR_OK) {
		ping_tx++;
	}

	pbuf_free(p);
}

/* --- UDP capacity probes ----------------------------------------------------- */

/* The sink only counts: it never answers, so a phase's byte count is what the
 * peer actually put on the wire, with no reverse traffic to confuse the
 * census. */
static void udp_sink_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
			  const ip_addr_t *addr, u16_t port)
{
	(void)arg;
	(void)pcb;
	(void)addr;
	(void)port;

	if (p == NULL) {
		return;
	}
	udp_pkts++;
	udp_bytes += p->tot_len;
	pbuf_free(p);
}

static int udp_sink_up(void)
{
	udp_sink = udp_new();
	if (udp_sink == NULL) {
		return -1;
	}
	udp_recv(udp_sink, udp_sink_recv, NULL);
	if (udp_bind(udp_sink, IP_ANY_TYPE, NET_UDP_SINK_PORT) != ERR_OK) {
		udp_remove(udp_sink);
		udp_sink = NULL;
		return -1;
	}
	return 0;
}

/* As fast as the path takes it, in lock-held bursts. The peer's own iperf
 * report is this direction's independent witness; the board-side byte count
 * only proves what was offered. */
static int net_do_up_udp(unsigned window_ms)
{
	struct udp_pcb *pcb;
	struct pbuf *p;
	uint64_t deadline;
	uint64_t t0;
	unsigned sent = 0u;
	unsigned pkts = 0u;
	unsigned ms;

	pcb = udp_new();
	if (pcb == NULL) {
		CASE_ANCHOR("net: FAIL udp pcb\n");
		return -1;
	}
	p = pbuf_alloc(PBUF_TRANSPORT, NET_UDP_BLAST_LEN, PBUF_RAM);
	if (p == NULL) {
		udp_remove(pcb);
		CASE_ANCHOR("net: FAIL udp pbuf\n");
		return -1;
	}
	(void)memset(p->payload, 0x5au, NET_UDP_BLAST_LEN);
	if (udp_connect(pcb, &peer, NET_UDP_BLAST_PORT) != ERR_OK) {
		pbuf_free(p);
		udp_remove(pcb);
		CASE_ANCHOR("net: FAIL udp connect\n");
		return -1;
	}

	t0 = rtos_clock_us();
	deadline = t0 + ((uint64_t)window_ms * 1000u);
	for (;;) {
		unsigned before = pkts;
		unsigned i;

		LOCK_TCPIP_CORE();
		for (i = 0u; i < NET_UDP_BLAST_BURST; i++) {
			if (udp_send(pcb, p) != ERR_OK) {
				break;
			}
			pkts++;
			sent += p->tot_len;
		}
		UNLOCK_TCPIP_CORE();
		if (pkts == before) {
			/* The ring is full: give the completion reclaim a tick
			 * instead of hammering a driver that cannot take more. */
			(void)osDelay(1u);
		}
		if (rtos_clock_us() >= deadline) {
			break;
		}
	}

	ms = (unsigned)((rtos_clock_us() - t0) / 1000u);
	CASE_ANCHOR("net: udp blast port=%u sent=%u pkts=%u ms=%u kbit=%lu\n",
		    (unsigned)NET_UDP_BLAST_PORT, sent, pkts, ms,
		    (unsigned long)((ms != 0u) ?
			(((uint64_t)sent * 8u * 1000u) / ((uint64_t)ms * 1000u)) :
			0u));

	pbuf_free(p);
	udp_remove(pcb);
	return (sent != 0u) ? 0 : -1;
}

/* --- phase dispatchers ------------------------------------------------------- */

/* Downlink window: the peer pings and then drives a TCP session into the
 * server started above. */
static int net_do_down_tcp(unsigned window_ms)
{
	unsigned waited;

	down_done = 0;
	CASE_ANCHOR("net: iperf ready port=5001 window_ms=%u\n", window_ms);

	for (waited = 0u; (waited < window_ms) && (down_done == 0);
	     waited += 50u) {
		(void)osDelay(50u);
	}

	CASE_ANCHOR("net: iperf down done=%d bytes=%u ms=%u kbit=%u\n",
		    down_done, down_bytes, down_ms, down_kbit);

	return ((down_done != 0) && (down_bytes != 0u)) ? 0 : -1;
}

/* Uplink: the board initiates the session, so the peer only needs its server
 * listening. */
static int net_do_up_tcp(unsigned wait_ms)
{
	void *up_client;
	unsigned waited;

	up_done = 0;
	LOCK_TCPIP_CORE();
	up_client = lwiperf_start_tcp_client_default(&peer, iperf_report, NULL);
	UNLOCK_TCPIP_CORE();

	CASE_ANCHOR("net: iperf up start=%s\n", (up_client != NULL) ? "ok" : "fail");
	if (up_client == NULL) {
		return -1;
	}

	for (waited = 0u; (waited < wait_ms) && (up_done == 0);
	     waited += 100u) {
		(void)osDelay(100u);
	}

	CASE_ANCHOR("net: iperf up done=%d bytes=%u ms=%u kbit=%u\n",
		    up_done, up_bytes, up_ms, up_kbit);

	return ((up_done != 0) && (up_bytes != 0u)) ? 0 : -1;
}

/* The peer drives a UDP stream into the sink. Bounded by the window, and the
 * counters are reset here so the phase's number is its own. */
static int net_do_down_udp(unsigned window_ms)
{
	uint64_t t0 = rtos_clock_us();
	unsigned waited;
	unsigned ms;

	udp_pkts = 0u;
	udp_bytes = 0u;

	for (waited = 0u; waited < window_ms; waited += 50u) {
		(void)osDelay(50u);
	}

	ms = (unsigned)((rtos_clock_us() - t0) / 1000u);
	CASE_ANCHOR("net: udp sink port=%u pkts=%u bytes=%u ms=%u kbit=%lu\n",
		    (unsigned)NET_UDP_SINK_PORT, udp_pkts, udp_bytes, ms,
		    (unsigned long)((ms != 0u) ?
			(((uint64_t)udp_bytes * 8u * 1000u) /
			 ((uint64_t)ms * 1000u)) : 0u));

	return (udp_bytes != 0u) ? 0 : -1;
}

/* Board-local session: both endpoints are this board's TCP/IP thread, so the
 * number is the stack's own ceiling with two sessions' work on one context. It
 * exists to refute "the wire or the peer is the wall" inside one boot, not to
 * be reported as throughput. */
static int net_do_loopback(unsigned window_ms)
{
	ip_addr_t lo;
	void *client;
	unsigned waited;

	IP_ADDR4(&lo, 127, 0, 0, 1);
	lo_done = 0;
	lo_active = 1;
	LOCK_TCPIP_CORE();
	client = lwiperf_start_tcp_client(&lo, 5001u, LWIPERF_CLIENT,
					  iperf_report, NULL);
	UNLOCK_TCPIP_CORE();

	CASE_ANCHOR("net: loopback start=%s\n", (client != NULL) ? "ok" : "fail");
	if (client == NULL) {
		lo_active = 0;
		return -1;
	}

	for (waited = 0u; (waited < window_ms) && (lo_done == 0);
	     waited += 100u) {
		(void)osDelay(100u);
	}
	lo_active = 0;

	CASE_ANCHOR("net: loopback proto=tcp bytes=%u ms=%u kbit=%u done=%d\n",
		    lo_bytes, lo_ms, lo_kbit, lo_done);

	return ((lo_done != 0) && (lo_bytes != 0u)) ? 0 : -1;
}

static int net_run_phase(unsigned n, const struct net_phase *ph)
{
	struct net_snap before;
	struct net_snap after;
	int rc;

	CASE_ANCHOR("net: phase %u begin proto=%s role=%s port=%u window_ms=%u "
		    "name=%s\n",
		    n, ph->proto, ph->role, (unsigned)ph->port, ph->window_ms,
		    ph->name);
	net_snap_take(&before);

	switch (ph->kind) {
	case PH_DOWN_TCP:
		rc = net_do_down_tcp(ph->window_ms);
		break;
	case PH_UP_TCP:
		rc = net_do_up_tcp(ph->window_ms);
		break;
	case PH_DOWN_UDP:
		rc = net_do_down_udp(ph->window_ms);
		break;
	case PH_UP_UDP:
		rc = net_do_up_udp(ph->window_ms);
		break;
	case PH_LOOPBACK:
		rc = net_do_loopback(ph->window_ms);
		break;
	default:
		rc = -1;
		break;
	}

	net_snap_take(&after);
	net_census_report(ph->name, &before, &after);
	CASE_ANCHOR("net: phase %u done name=%s ok=%d\n", n, ph->name,
		    (rc == 0) ? 1 : 0);

	return rc;
}

/* --- bring-up --------------------------------------------------------------- */

static int arp_lookup(uint8_t mac[6]);

static int gmac_up(void)
{
	eqos_link_info_t link;
	uint32_t configure;

	if (dwc_eqos_initialize(NET_PORT, eqos_board_plat(NET_PORT)) != EQOS_OK) {
		CASE_ANCHOR("net: FAIL gmac initialize\n");
		return -1;
	}
#if defined(CONFIG_GMAC_RX_ZC) && CONFIG_GMAC_RX_ZC
	/* Arm zero copy before the receiver exists: the driver drops a frame that
	 * completes with no lend-out hook installed, so arming after the first
	 * interrupt would show up as receive loss, not as a hang. */
	ethnetif_zc_arm();
#endif
	if (dwc_eqos_power_control(NET_PORT, EQOS_POWER_FULL) != EQOS_OK) {
		CASE_ANCHOR("net: FAIL gmac power\n");
		return -1;
	}
	if (rtl8211f_initialize(NET_PORT, (void *)(uintptr_t)NET_PORT,
				eqos_board_mdio_read,
				eqos_board_mdio_write) != EQOS_OK) {
		CASE_ANCHOR("net: FAIL phy initialize\n");
		return -1;
	}
	if (rtl8211f_power_control(NET_PORT, EQOS_POWER_FULL) != EQOS_OK) {
		CASE_ANCHOR("net: FAIL phy power\n");
		return -1;
	}
	(void)rtl8211f_set_interface(NET_PORT, EQOS_INTERFACE_RGMII);
	if (rtl8211f_set_mode(NET_PORT,
			      EQOS_PHY_AUTO_NEGOTIATE |
			      EQOS_PHY_SPEED_1G) != EQOS_OK) {
		CASE_ANCHOR("net: FAIL phy autoneg\n");
		return -1;
	}

	link = rtl8211f_get_link_info(NET_PORT);
	configure = link.speed | link.duplex;
	(void)dwc_eqos_control(NET_PORT, DWC_EQOS_CONFIGURE, configure);
	(void)dwc_eqos_control(NET_PORT, DWC_EQOS_CONTROL_TX, 1);
	(void)dwc_eqos_control(NET_PORT, DWC_EQOS_CONTROL_RX, 1);

	CASE_ANCHOR("net: link speed=%lu duplex=%lu\n",
		    (unsigned long)link.speed, (unsigned long)link.duplex);

	return 0;
}

static int line_up(void)
{
	ip4_addr_t ip;
	ip4_addr_t mask;
	ip4_addr_t gw;
	uint8_t mac[6];

	if (net_up) {
		return 0;
	}

	if (gmac_up() != 0) {
		return -1;
	}

	rx_sem = osSemaphoreNew(0xFFFFu, 0u, NULL);
	if (rx_sem == NULL) {
		CASE_ANCHOR("net: FAIL rx semaphore\n");
		return -1;
	}
	dwc_eqos_set_rx_notify(net_rx_notify);

	rx_thread = osThreadNew(net_rx_thread, NULL,
				&(const osThreadAttr_t){ .name = "gmac-rx",
							 .priority = osPriorityNormal,
							 .stack_size = 4096U });
	if (rx_thread == NULL) {
		CASE_ANCHOR("net: FAIL rx thread\n");
		return -1;
	}

	if (osThreadNew(net_service_thread, NULL,
			&(const osThreadAttr_t){ .name = "gmac-svc",
						 .priority = osPriorityLow,
						 .stack_size = 2048U }) == NULL) {
		CASE_ANCHOR("net: FAIL service thread\n");
		return -1;
	}

	if (stack_up() != 0) {
		CASE_ANCHOR("net: FAIL stack bring-up\n");
		return -1;
	}

	IP4_ADDR(&ip, NET_IP_A, NET_IP_B, NET_IP_C, NET_IP_D);
	IP4_ADDR(&mask, NET_MASK_A, NET_MASK_B, NET_MASK_C, NET_MASK_D);
	IP4_ADDR(&gw, NET_IP_A, NET_IP_B, NET_IP_C, NET_GW_D);
	IP4_ADDR(&peer, NET_IP_A, NET_IP_B, NET_IP_C, PEER_IP_D);

	LOCK_TCPIP_CORE();
	ethnetif_add(&netif, NET_PORT, &ip, &mask, &gw);
	if (lwiperf_start_tcp_server_default(iperf_report, NULL) == NULL) {
		CASE_ANCHOR("net: FAIL lwiperf server\n");
	}
	if (udp_sink_up() != 0) {
		CASE_ANCHOR("net: FAIL udp sink\n");
	}
	UNLOCK_TCPIP_CORE();

	(void)dwc_eqos_get_mac_address(NET_PORT, mac);
	CASE_ANCHOR("net: addr %lu.%lu.%lu.%lu/24 mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    (unsigned long)NET_IP_A, (unsigned long)NET_IP_B,
		    (unsigned long)NET_IP_C, (unsigned long)NET_IP_D,
		    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	net_up = 1;
	return 0;
}

/* Wait for the PHY's autoneg result, bounded. */
static int wait_link(void)
{
	unsigned waited = 0u;

	while (waited < LINK_WAIT_MS) {
		eqos_link_info_t link = rtl8211f_get_link_info(NET_PORT);

		if (link.speed != 0u) {
			CASE_ANCHOR("net: link up speed=%lu duplex=%lu\n",
				    (unsigned long)link.speed,
				    (unsigned long)link.duplex);
			return 0;
		}
		(void)osDelay(100u);
		waited += 100u;
	}

	CASE_ANCHOR("net: FAIL link down after %ums\n", LINK_WAIT_MS);
	return -1;
}

static int arp_lookup(uint8_t mac[6])
{
	const ip4_addr_t *addr = NULL;
	struct eth_addr *eth = NULL;
	int rc;

	LOCK_TCPIP_CORE();
	rc = etharp_find_addr(&netif, &peer, &eth, &addr);
	UNLOCK_TCPIP_CORE();

	if ((rc < 0) || (eth == NULL)) {
		return -1;
	}
	mac[0] = eth->addr[0];
	mac[1] = eth->addr[1];
	mac[2] = eth->addr[2];
	mac[3] = eth->addr[3];
	mac[4] = eth->addr[4];
	mac[5] = eth->addr[5];
	return 0;
}

/* --- the case --------------------------------------------------------------- */

int case_net_run(void)
{
	uint8_t mac[6];
	unsigned i;
	unsigned waited;
	unsigned arp_waited;
	int ok = 1;

	if (line_up() != 0) {
		CASE_ANCHOR("net: FAIL bring-up\n");
		return -1;
	}

	if (wait_link() != 0) {
		return -1;
	}

	ping_pcb = raw_new(IP_PROTO_ICMP);
	if (ping_pcb == NULL) {
		CASE_ANCHOR("net: FAIL raw pcb\n");
		return -1;
	}
	raw_recv(ping_pcb, ping_recv, NULL);

	/* Warm-up: one throwaway echo forces the ARP resolution, and the first
	 * counted packets are not sent into an unresolved entry (lwIP drops
	 * them without a queue). */
	ping_tx = 0u;
	ping_rx = 0u;
	ping_seen = 0u;
	ping_send(0u);
	for (waited = 0u; (waited < 2000u) && (arp_lookup(mac) != 0); waited += 20u) {
		(void)osDelay(20u);
	}

	ping_tx = 0u;
	ping_rx = 0u;
	ping_seen = 0u;
	ping_rtt_n = 0u;
	ping_rtt_sum_us = 0u;
	ping_rtt_min_us = 0u;
	ping_rtt_max_us = 0u;
	for (i = 1u; i <= PING_COUNT; i++) {
		unsigned before = ping_rx;

		ping_send((u16_t)i);
		for (waited = 0u;
		     (waited < PING_REPLY_WAIT_MS) && (ping_rx == before);
		     waited += 10u) {
			(void)osDelay(10u);
		}
	}

	CASE_ANCHOR("net: ping tx=%u rx=%u of=%u seen=%u\n",
		    ping_tx, ping_rx, PING_COUNT, ping_seen);

	if (ping_rx != PING_COUNT) {
		ok = 0;
	}

	CASE_ANCHOR("net: rtt n=%u min_us=%u avg_us=%lu max_us=%u\n",
		    ping_rtt_n, ping_rtt_min_us,
		    (unsigned long)((ping_rtt_n != 0u) ?
			(ping_rtt_sum_us / ping_rtt_n) : 0u),
		    ping_rtt_max_us);

	/* The ping above resolved the peer; read the stack's own entry. */
	for (arp_waited = 0u; arp_waited < 500u; arp_waited += 20u) {
		if (arp_lookup(mac) == 0) {
			CASE_ANCHOR("net: arp %lu.%lu.%lu.%lu -> %02x:%02x:%02x:%02x:%02x:%02x\n",
				    (unsigned long)NET_IP_A,
				    (unsigned long)NET_IP_B,
				    (unsigned long)NET_IP_C, (unsigned long)PEER_IP_D,
				    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
			break;
		}
		(void)osDelay(20u);
	}
	if (arp_waited >= 500u) {
		CASE_ANCHOR("net: FAIL arp entry missing\n");
		ok = 0;
	}

	/* The phase table is the schedule: each phase announces itself, runs its
	 * bounded window and closes with its own census delta. */
	for (i = 0u; i < (unsigned)NET_PLAN_LEN; i++) {
		if (net_run_phase(i + 1u, &net_plan[i]) != 0) {
			/* The loopback phase is an attribution instrument, not a
			 * claim: a board-local session that does not complete says
			 * something about the stack, and it is reported, but it
			 * does not fail a boot whose wire sessions are fine. */
			if (net_plan[i].kind != PH_LOOPBACK) {
				ok = 0;
			}
		}
	}

	raw_remove(ping_pcb);
	ping_pcb = NULL;

	CASE_ANCHOR("net: %s\n", ok ? "PASS" : "FAIL");

	return ok ? 0 : -1;
}
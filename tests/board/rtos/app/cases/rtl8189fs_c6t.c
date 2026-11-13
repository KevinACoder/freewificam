/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtl8189fs_c6t.c
 * @brief  Case rtl8189fs_c6t: the throughput phases over the lwIP netif the
 *         station case already brought up.
 *
 * The case is deliberately not a second connection bring-up: `rtl8189fs_sta`
 * runs first in the same boot (CONFIG_CASES order), joins the open bench AP
 * and assembles the netif.  This case checks that state and then drives the
 * phases the round compares against the GT denominator
 * (docs/GT-8189FTV-REFERENCE.md §3), with the round's pre-registered 80%
 * criterion:
 *
 *   loopback   board-local TCP (127.0.0.1) - attribution only, never a gate
 *   tcpdn      the board serves; the peer's iperf2 client sends for 30 s, and
 *              the case samples the receiver every second (the GT round's
 *              stall window: a >=2 s zero-delta stretch is a stall, not a
 *              throughput number)
 *   udpdn      the board's counting sink; the peer sends at 35 Mbit/s (the
 *              GT ladder's top rung) and the sink reports what arrived, so
 *              the loss is the receiver's count, not the sender's
 *   tcpdn2     the same as tcpdn; the pair catches the cold-first bias
 *   udpdn2     the same as udpdn
 *   tcpup      the board initiates; lwiperf's client is time-limited
 *   udpup      the board blasts 1470-byte datagrams at 35 Mbit/s
 *
 * Every phase announces itself BEFORE it waits, so the host driver fires the
 * matching peer command on the anchor rather than on a sleep.  Each phase
 * closes with the adapter's and lwIP's own counter deltas for exactly that
 * window; no per-frame print, no threshold here - the harness applies the
 * pre-registered criterion.
 *
 * Anchors:
 *   rtl8189fs_c6t: plan=throughput bssid=<mac> dev=<name>
 *   rtl8189fs_c6t: ready netif=<0|1> rx_frames=<n> tx_frames=<n>
 *   rtl8189fs_c6t: server tcp=<port> udp=<port> ok=<0|1>
 *   rtl8189fs_c6t: phase <n> begin proto=<p> role=<r> port=<n> window_ms=<n> name=<n>
 *   rtl8189fs_c6t: t=<n> bytes=<n> delta=<n> done=<0|1>
 *   rtl8189fs_c6t: phase <n> report bytes=<n> ms=<n> kbit=<n> pkts=<n>
 *   rtl8189fs_c6t: phase <n> census rx_frames=<n> rx_bytes=<n> tx_frames=<n> ...
 *   rtl8189fs_c6t: phase <n> done ok=<0|1>
 *   rtl8189fs_c6t: PASS C6T
 *   rtl8189fs_c6t: FAIL <reason>
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"
#include "linuxkpi_sdio.h"
#include "rtl8189fs_diag.h"
#include "linux80211/nl80211_probe.h"
#include "linux80211/net.h"

#include <linux/netdevice.h>
#include <net/net_namespace.h>

#ifndef RTL8189FS_PEER_IP
#define RTL8189FS_PEER_IP	"192.168.99.1"
#endif
static char c6t_peer_ip[16] = RTL8189FS_PEER_IP;

void
lkpi_c6_peer_ip_set(const char *ip)
{
	if (ip == NULL || ip[0] == '\0')
		return;
	strncpy(c6t_peer_ip, ip, sizeof(c6t_peer_ip) - 1u);
	c6t_peer_ip[sizeof(c6t_peer_ip) - 1u] = '\0';
}

#define C6T_TCP_PORT		5001u
#define C6T_UDP_PORT		5002u

#define C6T_LOOP_MS		12000u
#define C6T_DN_MS		30000u
#define C6T_UDPDN_MS		20000u
#define C6T_UP_MS		12000u
#define C6T_UDPUP_MS		20000u
#define C6T_UDPUP_KBIT		35000u
#define C6T_SAMPLE_MS		1000u

enum c6t_phase_kind {
	C6T_LOOPBACK,
	C6T_TCP_DOWN,
	C6T_UDP_DOWN,
	C6T_TCP_UP,
	C6T_UDP_UP,
};

struct c6t_phase {
	const char *name;
	const char *proto;
	const char *role;
	unsigned port;
	unsigned window_ms;
	enum c6t_phase_kind kind;
};

#if CONFIG_C6T_UDP_FIRST
/* The receive-only-first arm (tests/board/rtos/Kconfig: C6T_UDP_FIRST): no
 * TCP phase runs before the sink, so the transmit path is idle while the
 * downlink is offered.  A stall here is an RX-path finding; a clean window
 * followed by the default plan's stall points at the transmit path's claim. */
static const struct c6t_phase c6t_plan[] = {
	{ "udpdn",    "udp", "server", C6T_UDP_PORT, C6T_UDPDN_MS, C6T_UDP_DOWN },
	{ "udpdn2",   "udp", "server", C6T_UDP_PORT, C6T_UDPDN_MS, C6T_UDP_DOWN },
	{ "loopback", "tcp", "client", C6T_TCP_PORT, C6T_LOOP_MS, C6T_LOOPBACK },
	{ "tcpdn",    "tcp", "server", C6T_TCP_PORT, C6T_DN_MS, C6T_TCP_DOWN },
	{ "tcpdn2",   "tcp", "server", C6T_TCP_PORT, C6T_DN_MS, C6T_TCP_DOWN },
	{ "tcpup",    "tcp", "client", C6T_TCP_PORT, C6T_UP_MS, C6T_TCP_UP },
	{ "udpup",    "udp", "client", C6T_TCP_PORT, C6T_UDPUP_MS, C6T_UDP_UP },
};
#else
static const struct c6t_phase c6t_plan[] = {
	{ "loopback", "tcp", "client", C6T_TCP_PORT, C6T_LOOP_MS, C6T_LOOPBACK },
	{ "tcpdn",    "tcp", "server", C6T_TCP_PORT, C6T_DN_MS, C6T_TCP_DOWN },
	{ "udpdn",    "udp", "server", C6T_UDP_PORT, C6T_UDPDN_MS, C6T_UDP_DOWN },
	{ "tcpdn2",   "tcp", "server", C6T_TCP_PORT, C6T_DN_MS, C6T_TCP_DOWN },
	{ "udpdn2",   "udp", "server", C6T_UDP_PORT, C6T_UDPDN_MS, C6T_UDP_DOWN },
	{ "tcpup",    "tcp", "client", C6T_TCP_PORT, C6T_UP_MS, C6T_TCP_UP },
	{ "udpup",    "udp", "client", C6T_TCP_PORT, C6T_UDPUP_MS, C6T_UDP_UP },
};
#endif

#define C6T_PLAN_LEN	(sizeof(c6t_plan) / sizeof(c6t_plan[0]))
#define C6T_TRAFFIC_PHASES	6u

struct c6t_snap {
	struct lkpi_net_diag diag;
	struct lkpi_net_stack_diag stack;
	struct lkpi_skb_pool_stats pool;
};

static void
c6t_snap_take(struct c6t_snap *s)
{
	(void)memset(s, 0, sizeof(*s));
	lkpi_net_diag(&s->diag);
	lkpi_net_stack_diag(&s->stack);
	lkpi_skb_pool_stats(&s->pool);
}

/* The layered line a stall is read from: who holds the SDIO bus and what
 * operation it is inside (the engine's own state), what the card's receive
 * FIFO and the driver's buffer pools look like (the vendor's gauges, through
 * the first-party port), and how many transfers the engine has issued.  A
 * stall with the claim held mid-read, a stall with the pump idle and the FIFO
 * growing, and a stall with every buffer drained are three different findings;
 * one line per second separates them. */
static unsigned c6t_recv_min = ~0u;
static unsigned c6t_rxbuf_min = ~0u;

static void
c6t_obs_line(unsigned int sec)
{
	struct rtl8189f_diag d;
	struct lkpi_sdio_stats st;

	rtl8189f_diag_get(&d);
	lkpi_sdio_stats_get(&st);
	if (d.valid != 0u) {
		if (d.recv_free < c6t_recv_min)
			c6t_recv_min = d.recv_free;
		if (d.recvbuf_free < c6t_rxbuf_min)
			c6t_rxbuf_min = d.recvbuf_free;
	}
	CASE_ANCHOR("rtl8189fs_c6t: obs t=%u claim=%s/%u/%s/%s pump=%s "
		    "irq=%u io=%u/%u err=%u drv recv=%u rxbuf=%u min=%u/%u "
		    "rxfifo=%u txb=%u/%u cmd=%u/%u\n",
		    sec, lkpi_sdio_claim_owner(), (unsigned)lkpi_sdio_claim_held_ms(),
		    lkpi_sdio_pump_stage(), lkpi_sdio_claim_op(),
		    lkpi_sdio_pump_state(),
		    lkpi_sdio_pump_irq_get() ? 1u : 0u,
		    (unsigned)st.io_direct, (unsigned)st.io_extended,
		    (unsigned)st.errors, (unsigned)d.recv_free,
		    (unsigned)d.recvbuf_free, c6t_recv_min, c6t_rxbuf_min,
		    (unsigned)d.rx_fifo, (unsigned)d.txbuf_free,
		    (unsigned)d.txframe_free, (unsigned)d.cmd_issued,
		    (unsigned)d.cmd_done);
}

/* The 30 s downlink window as the GT arm measured it: the receiver's byte
 * count once a second, printed before the next second is waited for, so a
 * zero-delta stretch is visible where it happened. */
static int
c6t_tcp_down_sampled(unsigned window_ms, struct lkpi_net_phase *out)
{
	struct lkpi_net_phase cur;
	struct lkpi_net_diag diag;
	unsigned int prev_bytes = 0u;
	unsigned long prev_rx = 0u;
	unsigned int elapsed = 0u;

	/* The completion report only lands when the peer closes, so the window's
	 * per-second reading is the adapter's own receive byte count (the same
	 * place the GT arm's /proc/net/dev sampler read), with the completion's
	 * total reported at the end.  A zero-delta second here is a stall. */
	(void)lkpi_net_diag(&diag);
	prev_rx = diag.rx_bytes;
	(void)lkpi_net_iperf_down_begin();
	for (elapsed = C6T_SAMPLE_MS; elapsed <= window_ms;
	     elapsed += C6T_SAMPLE_MS) {
		osDelay(C6T_SAMPLE_MS);
		(void)memset(&cur, 0, sizeof(cur));
		(void)lkpi_net_iperf_down_poll(&cur);
		(void)lkpi_net_diag(&diag);
		CASE_ANCHOR("rtl8189fs_c6t: t=%u bytes=%u delta=%u rx=%lu "
			    "rxd=%lu done=%d\n",
			    elapsed / C6T_SAMPLE_MS, cur.bytes,
			    cur.bytes - prev_bytes, diag.rx_bytes,
			    diag.rx_bytes - prev_rx, cur.done);
		prev_bytes = cur.bytes;
		prev_rx = diag.rx_bytes;
		c6t_obs_line(elapsed / C6T_SAMPLE_MS);
	}
	(void)memset(out, 0, sizeof(*out));
	(void)lkpi_net_iperf_down_poll(out);
	lkpi_net_iperf_down_end();

	return (out->bytes != 0u) ? 0 : -1;
}

static int
c6t_run_phase(unsigned n, const struct c6t_phase *ph)
{
	struct c6t_snap before;
	struct c6t_snap after;
	struct lkpi_net_phase r;
	int rc;

	(void)memset(&r, 0, sizeof(r));
	CASE_ANCHOR("rtl8189fs_c6t: phase %u begin proto=%s role=%s port=%u "
		    "window_ms=%u name=%s\n",
		    n, ph->proto, ph->role, ph->port, ph->window_ms, ph->name);
	c6t_snap_take(&before);

	switch (ph->kind) {
	case C6T_LOOPBACK:
		rc = lkpi_net_loopback(ph->port, ph->window_ms, &r);
		break;
	case C6T_TCP_DOWN:
		rc = c6t_tcp_down_sampled(ph->window_ms, &r);
		break;
	case C6T_UDP_DOWN:
		rc = lkpi_net_udp_sink_window(ph->window_ms, &r);
		break;
	case C6T_TCP_UP:
		rc = lkpi_net_iperf_up(c6t_peer_ip, ph->port, ph->window_ms, &r);
		break;
	case C6T_UDP_UP:
		rc = lkpi_net_udp_blast(c6t_peer_ip, ph->port, ph->window_ms,
					C6T_UDPUP_KBIT, &r);
		break;
	default:
		rc = -1;
		break;
	}

	c6t_snap_take(&after);
	CASE_ANCHOR("rtl8189fs_c6t: phase %u report bytes=%u ms=%u kbit=%u "
		    "pkts=%u\n",
		    n, r.bytes, r.ms, r.kbit, r.pkts);
	CASE_ANCHOR("rtl8189fs_c6t: phase %u census rx_frames=%lu rx_bytes=%lu "
		    "tx_frames=%lu tx_bytes=%lu tx_fail=%lu rxdlv_us=%lu "
		    "txndo_us=%lu tcp_recv=%lu tcp_xmit=%lu tcp_drop=%lu "
		    "tcp_memerr=%lu pbuf_err=%lu mbox_full=%lu skb_used=%u "
		    "skb_high=%u data_used=%u data_high=%u alloc_fail=%lu "
		    "data_ovf=%lu dyalloc=%u\n",
		    n,
		    (unsigned long)(after.diag.rx_frames - before.diag.rx_frames),
		    (unsigned long)(after.diag.rx_bytes - before.diag.rx_bytes),
		    (unsigned long)(after.diag.tx_frames - before.diag.tx_frames),
		    (unsigned long)(after.diag.tx_bytes - before.diag.tx_bytes),
		    (unsigned long)(after.diag.tx_fail - before.diag.tx_fail),
		    (unsigned long)((after.diag.rx_deliver_ns -
				     before.diag.rx_deliver_ns) / 1000u),
		    (unsigned long)((after.diag.tx_ndo_ns -
				     before.diag.tx_ndo_ns) / 1000u),
		    (unsigned long)(after.stack.tcp_recv - before.stack.tcp_recv),
		    (unsigned long)(after.stack.tcp_xmit - before.stack.tcp_xmit),
		    (unsigned long)(after.stack.tcp_drop - before.stack.tcp_drop),
		    (unsigned long)(after.stack.tcp_memerr -
				    before.stack.tcp_memerr),
		    (unsigned long)(after.stack.pbuf_err - before.stack.pbuf_err),
		    (unsigned long)(after.stack.mbox_post_full -
				    before.stack.mbox_post_full),
		    (unsigned)after.pool.skb_used,
		    (unsigned)after.pool.skb_high,
		    (unsigned)after.pool.data_used,
		    (unsigned)after.pool.data_high,
		    (unsigned long)(after.pool.alloc_fail -
				    before.pool.alloc_fail),
		    (unsigned long)(after.pool.data_overflow -
				    before.pool.data_overflow),
		    (unsigned)after.pool.dyalloc_live);
	CASE_ANCHOR("rtl8189fs_c6t: phase %u done ok=%d\n", n, (rc == 0) ? 1 : 0);

	return rc;
}

int
case_rtl8189fs_c6t_run(void)
{
	struct net_device *dev;
	unsigned char bssid[6];
	struct lkpi_net_diag diag;
	unsigned i;
	unsigned ok = 0u;
	int rc;

	dev = dev_get_by_name(&init_net, "wlan0");
	if (dev == NULL) {
		CASE_ANCHOR("rtl8189fs_c6t: FAIL no wlan0\n");
		return 1;
	}

	rc = lkpi_nl80211_connected(dev, bssid);
	if (rc != 1) {
		CASE_ANCHOR("rtl8189fs_c6t: FAIL not connected rc=%d\n", rc);
		return 2;
	}
	CASE_ANCHOR("rtl8189fs_c6t: plan=throughput bssid=%02x:%02x:%02x:%02x:"
		    "%02x:%02x dev=%s\n",
		    bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
		    dev->name);

	if (lkpi_net_ready() != 1) {
		CASE_ANCHOR("rtl8189fs_c6t: FAIL lwIP netif not ready\n");
		return 3;
	}
	lkpi_net_diag(&diag);
	CASE_ANCHOR("rtl8189fs_c6t: ready netif=1 rx_frames=%lu tx_frames=%lu\n",
		    diag.rx_frames, diag.tx_frames);

	rc = lkpi_net_iperf_server_start(C6T_TCP_PORT);
	if (rc == 0)
		rc = lkpi_net_udp_sink_start(C6T_UDP_PORT);
	CASE_ANCHOR("rtl8189fs_c6t: server tcp=%u udp=%u ok=%d\n",
		    C6T_TCP_PORT, C6T_UDP_PORT, (rc == 0) ? 1 : 0);
	if (rc != 0) {
		CASE_ANCHOR("rtl8189fs_c6t: FAIL listener rc=%d\n", rc);
		return 4;
	}

	for (i = 0u; i < (unsigned)C6T_PLAN_LEN; i++) {
		rc = c6t_run_phase(i + 1u, &c6t_plan[i]);
		if (rc == 0) {
			if (c6t_plan[i].kind != C6T_LOOPBACK)
				ok++;
		} else if (c6t_plan[i].kind == C6T_LOOPBACK) {
			/* The board-local session is an attribution instrument,
			 * not a claim: a failed loopback narrows where the wall
			 * is and does not fail the wire phases. */
			CASE_ANCHOR("rtl8189fs_c6t: note loopback failed "
				    "(attribution only)\n");
		}
	}

	if (ok == C6T_TRAFFIC_PHASES) {
		CASE_ANCHOR("rtl8189fs_c6t: PASS C6T\n");
		return 0;
	}
	CASE_ANCHOR("rtl8189fs_c6t: FAIL traffic phases %u/%u\n", ok,
		    C6T_TRAFFIC_PHASES);
	return 5;
}

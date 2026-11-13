/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtw89_c6.c
 * @brief  Case rtw89_c6: the throughput phases over the wl0 netif the rtw89
 *         C5 case already brought up in the same boot.
 *
 * The case is deliberately *not* a second connection bring-up: `rtw89_c5`
 * runs first in the same boot (CONFIG_CASES order), connects to the open bench
 * AP and assembles the lwIP netif.  This case checks that state, then drives
 * the seven phases the throughput round compares against the GT arm:
 *
 *   loopback  board-local TCP (127.0.0.1) - attribution only, never a gate
 *   tcpdn     the board serves; the peer's iperf2 client sends for ~30 s
 *   udpdn     the board counts a 40 Mbit/s UDP stream for ~20 s
 *   tcpdn2    the same as tcpdn; repeat phases catch the cold-first bias
 *   udpdn2    the same as udpdn; the larger of the two is the capacity read
 *   tcpup     the board initiates; lwiperf's client is time-limited to 10 s
 *   udpup     the board blasts 1470-byte datagrams for ~20 s
 *
 * Every phase announces itself BEFORE it waits, so the host driver fires the
 * matching peer command on the anchor rather than on a sleep.  Each phase
 * closes with the adapter's and lwIP's own counter deltas for exactly that
 * window; no per-frame print, no threshold here - the harness applies the
 * pre-registered 80% (runs/2026-11-13-rtw89-8851bu-rtos/c6t-PREREG.md).
 *
 * Anchors:
 *   rtw89_c6: plan=throughput bssid=<mac> dev=<name>
 *   rtw89_c6: ready netif=<0|1> rx_frames=<n> tx_frames=<n>
 *   rtw89_c6: server tcp=<port> udp=<port> ok=<0|1>
 *   rtw89_c6: phase <n> begin proto=<p> role=<r> port=<n> window_ms=<n> name=<n>
 *   rtw89_c6: phase <n> report bytes=<n> ms=<n> kbit=<n> pkts=<n>
 *   rtw89_c6: phase <n> census rx_frames=<n> rx_bytes=<n> tx_frames=<n> ...
 *   rtw89_c6: phase <n> done ok=<0|1>
 *   rtw89_c6: PASS C6T
 *   rtw89_c6: FAIL <reason>
 *
 * What this is not: the stability round.  The 30 min run and the reconnect
 * loop live in the stability profile, because a stability verdict must not
 * share a boot with a tuning verdict.
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"

#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"
#include "linux80211/nl80211_probe.h"
#include "linux80211/net.h"
#include "linux80211/hostap_ap.h"
#include "lkpi_usb_cherryusb.h"

#include <linux/netdevice.h>
#include <net/net_namespace.h>

#ifndef CONFIG_C6_PEER_IP
#define CONFIG_C6_PEER_IP "192.168.99.1"
#endif
static char c6_peer_ip[16] = CONFIG_C6_PEER_IP;

void
lkpi_c6_peer_ip_set(const char *ip)
{
	if (ip == NULL || ip[0] == '\0')
		return;
	strncpy(c6_peer_ip, ip, sizeof(c6_peer_ip) - 1u);
	c6_peer_ip[sizeof(c6_peer_ip) - 1u] = '\0';
}
#define C6_TCP_PORT		5001u
#define C6_UDP_PORT		5002u

/* The window is the phase's ceiling; the peer's own duration is 2 s inside
 * it, so a slow connection still leaves the completion inside the window. */
#define C6_LOOP_MS		12000u
#define C6_TCPDN_MS		30000u
#define C6_UDPDN_MS		20000u
#define C6_TCPUP_MS		12000u
#define C6_UDPUP_MS		20000u

enum c6_phase_kind {
	C6_LOOPBACK,
	C6_TCP_DOWN,
	C6_UDP_DOWN,
	C6_TCP_UP,
	C6_UDP_UP,
};

struct c6_phase {
	const char *name;
	const char *proto;
	const char *role;
	unsigned port;
	unsigned window_ms;
	enum c6_phase_kind kind;
};

static const struct c6_phase c6_plan[] = {
	{ "loopback", "tcp", "client", C6_TCP_PORT, C6_LOOP_MS,
	  C6_LOOPBACK },
	{ "tcpdn",    "tcp", "server", C6_TCP_PORT, C6_TCPDN_MS, C6_TCP_DOWN },
	{ "udpdn",    "udp", "server", C6_UDP_PORT, C6_UDPDN_MS, C6_UDP_DOWN },
	{ "tcpdn2",   "tcp", "server", C6_TCP_PORT, C6_TCPDN_MS, C6_TCP_DOWN },
	{ "udpdn2",   "udp", "server", C6_UDP_PORT, C6_UDPDN_MS, C6_UDP_DOWN },
	{ "tcpup",    "tcp", "client", C6_TCP_PORT, C6_TCPUP_MS, C6_TCP_UP },
	{ "udpup",    "udp", "client", C6_TCP_PORT, C6_UDPUP_MS, C6_UDP_UP },
};

#define C6_PLAN_LEN	(sizeof(c6_plan) / sizeof(c6_plan[0]))
#define C6_TRAFFIC_PHASES	6u

struct c6_snap {
	struct lkpi_net_diag diag;
	struct lkpi_net_stack_diag stack;
	struct lkpi_skb_pool_stats pool;
};

static void
c6_snap_take(struct c6_snap *s)
{
	(void)memset(s, 0, sizeof(*s));
	lkpi_net_diag(&s->diag);
	lkpi_net_stack_diag(&s->stack);
	lkpi_skb_pool_stats(&s->pool);
}

static int
c6_run_phase(unsigned n, const struct c6_phase *ph)
{
	struct c6_snap before;
	struct c6_snap after;
	struct lkpi_net_phase r;
	int rc;

	(void)memset(&r, 0, sizeof(r));
	CASE_ANCHOR("rtw89_c6: phase %u begin proto=%s role=%s port=%u "
		    "window_ms=%u name=%s\n",
		    n, ph->proto, ph->role, ph->port, ph->window_ms, ph->name);
	c6_snap_take(&before);

	switch (ph->kind) {
	case C6_LOOPBACK:
		rc = lkpi_net_loopback(ph->port, ph->window_ms, &r);
		break;
	case C6_TCP_DOWN:
		rc = lkpi_net_iperf_down(ph->port, ph->window_ms, &r);
		break;
	case C6_UDP_DOWN:
		rc = lkpi_net_udp_sink_window(ph->window_ms, &r);
		break;
	case C6_TCP_UP:
		rc = lkpi_net_iperf_up(c6_peer_ip, ph->port, ph->window_ms, &r);
		break;
	case C6_UDP_UP:
		rc = lkpi_net_udp_blast(c6_peer_ip, ph->port, ph->window_ms,
					40000u, &r);
		break;
	default:
		rc = -1;
		break;
	}

	c6_snap_take(&after);
	CASE_ANCHOR("rtw89_c6: phase %u report bytes=%u ms=%u kbit=%u "
		    "pkts=%u\n",
		    n, r.bytes, r.ms, r.kbit, r.pkts);
	/* No PCI interrupt pair on this arm; the generic tx_health
	 * counters are already in the census line below. */
	CASE_ANCHOR("rtw89_c6: phase %u census rx_frames=%lu rx_bytes=%lu "
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
		    (unsigned long)(after.stack.tcp_recv -
				    before.stack.tcp_recv),
		    (unsigned long)(after.stack.tcp_xmit -
				    before.stack.tcp_xmit),
		    (unsigned long)(after.stack.tcp_drop -
				    before.stack.tcp_drop),
		    (unsigned long)(after.stack.tcp_memerr -
				    before.stack.tcp_memerr),
		    (unsigned long)(after.stack.pbuf_err -
				    before.stack.pbuf_err),
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
	{
		/* The pool's own owner rows answer "who holds the 128" when a phase
		 * ends with skb_used at the ceiling: a receive queue that grows and a
		 * completion queue that drains are the same total, different rows. */
		struct lkpi_usb_chusb_stats us;
		struct lkpi_skbq_census rows[LKPI_SKBQ_CENSUS_MAX];
		unsigned int nq, i;

		(void)memset(&us, 0, sizeof(us));
		lkpi_usb_cherryusb_stats(&us);
		CASE_ANCHOR("rtw89_c6: usb phase %u sub=%u done=%u ref=%u "
			    "slot=%u stg=%u rx=%u rxe=%u rxlen=%u\n",
			    n, us.submits, us.completed, us.refused,
			    us.slots_exhausted, us.stage_exhausted,
			    us.rx_completes, us.rx_errors, us.rx_last_len);
		CASE_ANCHOR("rtw89_c6: pool phase %u skb=%u/%u data=%u/%u "
			    "af=%u df=%u sf=%u\n",
			    n, after.pool.skb_used, after.pool.skb_high,
			    after.pool.data_used, after.pool.data_high,
			    after.pool.alloc_fail, after.pool.double_free,
			    after.pool.stray_free);
		nq = lkpi_skb_queue_census(rows, LKPI_SKBQ_CENSUS_MAX);
		for (i = 0u; i < nq; i++)
			CASE_ANCHOR("rtw89_c6: owner phase %u q=%p items=%u "
				    "bytes=%u\n", n, rows[i].q, rows[i].items,
				    rows[i].bytes);
	}
	CASE_ANCHOR("rtw89_c6: phase %u done ok=%d\n", n, (rc == 0) ? 1 : 0);

	return rc;
}

int
case_rtw89_c6_run(void)
{
	struct net_device *dev;
	unsigned char bssid[6];
	struct lkpi_net_diag diag;
	unsigned i;
	unsigned ok = 0u;
	int rc;

	dev = dev_get_by_name(&init_net, "wlan0");
	if (dev == NULL) {
		CASE_ANCHOR("rtw89_c6: FAIL no wlan0\n");
		return 1;
	}

	rc = lkpi_nl80211_connected(dev, bssid);
	if (rc != 1) {
		CASE_ANCHOR("rtw89_c6: FAIL not connected rc=%d\n", rc);
		return 2;
	}
	CASE_ANCHOR("rtw89_c6: plan=throughput bssid=%02x:%02x:%02x:%02x:"
		    "%02x:%02x dev=%s\n",
		    bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
		    dev->name);

	if (lkpi_net_ready() != 1) {
		CASE_ANCHOR("rtw89_c6: FAIL lwIP netif not ready\n");
		return 3;
	}
	lkpi_net_diag(&diag);
	CASE_ANCHOR("rtw89_c6: ready netif=1 rx_frames=%lu tx_frames=%lu\n",
		    diag.rx_frames, diag.tx_frames);

	rc = lkpi_net_iperf_server_start(C6_TCP_PORT);
	if (rc == 0)
		rc = lkpi_net_udp_sink_start(C6_UDP_PORT);
	CASE_ANCHOR("rtw89_c6: server tcp=%u udp=%u ok=%d\n",
		    C6_TCP_PORT, C6_UDP_PORT, (rc == 0) ? 1 : 0);
	if (rc != 0) {
		CASE_ANCHOR("rtw89_c6: FAIL listener rc=%d\n", rc);
		return 4;
	}

	for (i = 0u; i < (unsigned)C6_PLAN_LEN; i++) {
		rc = c6_run_phase(i + 1u, &c6_plan[i]);
		if (rc == 0) {
			if (c6_plan[i].kind != C6_LOOPBACK)
				ok++;
		} else if (c6_plan[i].kind == C6_LOOPBACK) {
			/* The board-local session is an attribution
			 * instrument, not a claim: a failed loopback narrows
			 * where the wall is and does not fail the wire phases. */
			CASE_ANCHOR("rtw89_c6: note loopback failed (attribution "
				    "only)\n");
		}
	}

	if (ok == C6_TRAFFIC_PHASES) {
		CASE_ANCHOR("rtw89_c6: PASS C6T\n");
		return 0;
	}
	CASE_ANCHOR("rtw89_c6: FAIL traffic phases %u/%u\n", ok,
		    C6_TRAFFIC_PHASES);
	return 5;
}

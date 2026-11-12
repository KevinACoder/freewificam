/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   iwl7260_c6.c
 * @brief  Case iwl7260_c6: the throughput phases over the wl0 netif the C5
 *         case already brought up.
 *
 * The case is deliberately *not* a second connection bring-up: `iwl7260_c5`
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
 * pre-registered 80% (see runs/2026-11-11-iwl7260-c6-throughput/PREREG.md).
 *
 * Anchors:
 *   iwl7260_c6: plan=throughput bssid=<mac> dev=<name>
 *   iwl7260_c6: ready netif=<0|1> rx_frames=<n> tx_frames=<n>
 *   iwl7260_c6: server tcp=<port> udp=<port> ok=<0|1>
 *   iwl7260_c6: phase <n> begin proto=<p> role=<r> port=<n> window_ms=<n> name=<n>
 *   iwl7260_c6: phase <n> report bytes=<n> ms=<n> kbit=<n> pkts=<n>
 *   iwl7260_c6: phase <n> census rx_frames=<n> rx_bytes=<n> tx_frames=<n> ...
 *   iwl7260_c6: phase <n> done ok=<0|1>
 *   iwl7260_c6: PASS C6T
 *   iwl7260_c6: FAIL <reason>
 *
 * What this is not: the stability round.  The 30 min run and the reconnect
 * loop live in `iwl7260_stab` (separate profile), because a stability verdict
 * must not share a boot with a tuning verdict.
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
#include "linuxkpi_pci.h"

#include <linux/netdevice.h>
#include <net/net_namespace.h>

#define C6_PEER_IP		"192.168.99.1"
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
	CASE_ANCHOR("iwl7260_c6: phase %u begin proto=%s role=%s port=%u "
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
		rc = lkpi_net_iperf_up(C6_PEER_IP, ph->port, ph->window_ms, &r);
		break;
	case C6_UDP_UP:
		rc = lkpi_net_udp_blast(C6_PEER_IP, ph->port, ph->window_ms,
					40000u, &r);
		break;
	default:
		rc = -1;
		break;
	}

	c6_snap_take(&after);
	CASE_ANCHOR("iwl7260_c6: phase %u report bytes=%u ms=%u kbit=%u "
		    "pkts=%u\n",
		    n, r.bytes, r.ms, r.kbit, r.pkts);
	{
		unsigned int hard = 0u, thread = 0u;
		struct lkpi_tx_health txh;

		lkpi_tx_health_get(&txh);
		lkpi_pci_irq_counters(&hard, &thread);
		CASE_ANCHOR("iwl7260_c6: phase %u txhealth tx_status=%lu "
			    "dq_calls=%lu dq_frames=%lu mgmt=%lu data=%lu "
			    "iwl_tx=%lu hcmd=%lu hcmd_blockq=%lu irq_hard=%u "
			    "irq_thread=%u\n", n, txh.tx_status,
			    txh.dequeue_calls, txh.dequeue_frames,
			    txh.status_mgmt, txh.status_data, txh.iwl_tx,
			    txh.hcmd, txh.hcmd_blockq, hard, thread);
	}
	CASE_ANCHOR("iwl7260_c6: phase %u census rx_frames=%lu rx_bytes=%lu "
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
	CASE_ANCHOR("iwl7260_c6: phase %u done ok=%d\n", n, (rc == 0) ? 1 : 0);

	return rc;
}

int
case_iwl7260_c6_run(void)
{
	struct net_device *dev;
	unsigned char bssid[6];
	struct lkpi_net_diag diag;
	unsigned i;
	unsigned ok = 0u;
	int rc;

	dev = dev_get_by_name(&init_net, "wlan0");
	if (dev == NULL) {
		CASE_ANCHOR("iwl7260_c6: FAIL no wlan0\n");
		return 1;
	}

	rc = lkpi_nl80211_connected(dev, bssid);
	if (rc != 1) {
		CASE_ANCHOR("iwl7260_c6: FAIL not connected rc=%d\n", rc);
		return 2;
	}
	CASE_ANCHOR("iwl7260_c6: plan=throughput bssid=%02x:%02x:%02x:%02x:"
		    "%02x:%02x dev=%s\n",
		    bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
		    dev->name);

	if (lkpi_net_ready() != 1) {
		CASE_ANCHOR("iwl7260_c6: FAIL lwIP netif not ready\n");
		return 3;
	}
	lkpi_net_diag(&diag);
	CASE_ANCHOR("iwl7260_c6: ready netif=1 rx_frames=%lu tx_frames=%lu\n",
		    diag.rx_frames, diag.tx_frames);

	rc = lkpi_net_iperf_server_start(C6_TCP_PORT);
	if (rc == 0)
		rc = lkpi_net_udp_sink_start(C6_UDP_PORT);
	CASE_ANCHOR("iwl7260_c6: server tcp=%u udp=%u ok=%d\n",
		    C6_TCP_PORT, C6_UDP_PORT, (rc == 0) ? 1 : 0);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_c6: FAIL listener rc=%d\n", rc);
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
			CASE_ANCHOR("iwl7260_c6: note loopback failed (attribution "
				    "only)\n");
		}
	}

	if (ok == C6_TRAFFIC_PHASES) {
		CASE_ANCHOR("iwl7260_c6: PASS C6T\n");
		return 0;
	}
	CASE_ANCHOR("iwl7260_c6: FAIL traffic phases %u/%u\n", ok,
		    C6_TRAFFIC_PHASES);
	return 5;
}

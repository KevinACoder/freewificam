/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   iwl7260_ap.c
 * @brief  Case iwl7260_ap: the AC 7260 becomes an AP (A1), the Pi's MT7921AU
 *         joins it (A2), the data plane is proven (A3), then the AP phase
 *         table runs (A4).
 *
 * The bring-up is C4's (core, firmware, PCI, wlan0).  The AP is not a second
 * interface: the default STA interface is changed in place with the real
 * NL80211_CMD_SET_INTERFACE stream.  With the device down mac80211 changes the
 * sdata type without a driver op, so wlan0 keeps its name, ifindex and address
 * and the lwIP netif's "wlan0" bind is untouched (the handoff's DEL+NEW is the
 * fallback, not the path).
 *
 * The AP MLME is hostapd, imported from the NXP wpa_supplicant-rtos port
 * (third-party/wpa_supplicant-rtos): lkpi_hostap_start() brings up the hostapd daemon task,
 * applies this AP's configuration and runs hostapd_setup_interface(); hostapd
 * builds the beacon (its 63-byte head + IE tail go to NL80211_CMD_START_AP),
 * and its ieee802_11.c answers probe/auth/assoc through the port's driver
 * table.  The port only drains the management frames the cfg80211_rx_mgmt_ext
 * wrap queued (lkpi_hostap_poll) and reports status.
 *
 * Direction words are the round's and are printed, not inferred: air=down is
 * AP->STA (the board sends; the Pi's iperf server/sink is the receiver),
 * air=up is STA->AP (the Pi sends; this board's lwIP is the receiver).  The
 * opposite of the C6 case's board roles with the same phase names.
 *
 * What this is not: WPA2, the stability round, or a tuning arm.  There is one
 * clean configuration (the accepted C6 pool128/heap16 profile) and the phases
 * report; the harness applies the pre-registered 80%/1%/5% gates.
 *
 * Anchors:
 *   iwl7260_ap: core ready
 *   iwl7260_ap: fw registered=<0|err>
 *   iwl7260_ap: pci devs=<n>
 *   iwl7260_ap: wlan0 present=1 ifindex=<n> mac=<mac> iftype=<n>
 *   iwl7260_ap: chan freq=<mhz> present=<0|1> flags=0x<hex> can_beacon=<0|1>
 *   iwl7260_ap: set_iface rc=<n> type=<n>
 *   iwl7260_ap: start rc=<n>
 *   iwl7260_ap: netif up name=wl0 ip=<ip>/24 mac=<mac>
 *   iwl7260_ap: security wpa2=<0|1> ssid=<ssid>
 *   iwl7260_ap: hostap start rc=<n> ready=<0|1>
 *   iwl7260_ap: peer window ms=<n>
 *   iwl7260_ap: wpa2 station sta=<n> ptk=<n> gtk=<n> fail=<n> wait_ms=<n>
 *   iwl7260_ap: eapol tx=<n> rx=<n> drop=<n>
 *   iwl7260_ap: hostap up ready=<0|1> sta=<n>
 *   iwl7260_ap: sta add n=<n> mac=<mac>
 *   iwl7260_ap: ping tx=<n> rx=<n> payload=<n> rtt_ms=<min>/<avg>/<max>
 *   iwl7260_ap: phase <n> begin proto=<p> role=<r> air=<down|up> port=<n> window_ms=<n> name=<n> peer_ms=<n>
 *   iwl7260_ap: phase <n> report bytes=<n> ms=<n> kbit=<n> pkts=<n>
 *   iwl7260_ap: phase <n> census ...
 *   iwl7260_ap: phase <n> done ok=<0|1>
 *   iwl7260_ap: PASS AP            (open; A4 phases follow)
 *   iwl7260_ap: PASS APW           (WPA2; stops after A3)
 *   iwl7260_ap: PASS APT           (open, all A4 phases)
 *   iwl7260_ap: FAIL <reason>
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"
#include "linux80211/nl80211_probe.h"
#include "linux80211/hostap_ap.h"
#include "linux80211/net.h"
#include "linux80211/diag.h"
#include "linuxkpi_pci.h"
#include "adapter.h"

#include <linux/etherdevice.h>
#include <linux/nl80211.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <net/net_namespace.h>

#define AP_SSID			"rtos-7260-ap"
#define AP_FREQ_MHZ		2462u

/* The WPA2 variant: one image-level switch, set by
 * rk3568-iwl7260-ap-wpa2.defconfig.  The passphrase is a bench credential,
 * not a secret; the Pi helper carries the same constant. */
#if defined(CONFIG_AP_WPA2) && CONFIG_AP_WPA2
#define AP_WPA2			1
#define AP_PASSPHRASE		"rtos-7260-wpa2-psk"
#else
#define AP_WPA2			0
#define AP_PASSPHRASE		NULL
#endif

/*
 * The A4 stall round's diagnostic variant: one image-level switch, set by
 * rk3568-iwl7260-ap-a4.defconfig.  It runs the same A1-A3 bring-up, then the
 * observation sequence (post-ping probe, short UDP windows, freeze detector,
 * CSR/NAPI snapshots, mechanical verdict) instead of the throughput table.
 * `idle` skips the peer entirely and idles the AP with the same detector.
 */
#if defined(CONFIG_AP_DIAG) && CONFIG_AP_DIAG
#define AP_DIAG			1
#else
#define AP_DIAG			0
#endif
#if AP_DIAG && defined(CONFIG_AP_DIAG_IDLE) && CONFIG_AP_DIAG_IDLE
#define AP_DIAG_IDLE		1
#else
#define AP_DIAG_IDLE		0
#endif
#if AP_DIAG && defined(CONFIG_AP_DIAG_TABLE) && CONFIG_AP_DIAG_TABLE
#define AP_DIAG_TABLE		1
#else
#define AP_DIAG_TABLE		0
#endif
#define AP_WPA_WAIT_MS		10000u
/* After the 4-way: leave the peer's own ping/ARP walk a full window before
 * this case pings and prints PASS.  The AP resets on return, and a peer ping
 * still in flight would be scored as loss by the harness.  This is a timing
 * accommodation, not part of the verdict: the verdict's pings are the two
 * board-originated 20/20 runs below. */
#define AP_WPA_POST_HS_MS	10000u
#define AP_BOARD_IP		"192.168.99.1"
#define AP_BOARD_MASK		"255.255.255.0"
#define AP_PEER_IP		"192.168.99.2"
#define AP_WAIT_MS		40000U
#define AP_POLL_MS		200U
#define AP_CONNECT_WAIT_MS	40000U
#define AP_HOSTAP_POLL_MS	10U
#define AP_SETTLE_MS		3000U
#define AP_PEER_WINDOW_MS	12000U
#define AP_PING_COUNT		20U
#define AP_PING_TIMEOUT_MS	300U
#define AP_PING_SMALL		56U
#define AP_PING_LARGE		1400U
#define AP_TCP_PORT		5001u
#define AP_UDP_PORT		5002u
#define AP_LOG_MAX		160U

/* The phase windows: the lwIP client is time-limited by lwiperf (10 s), so
 * the two board-sent TCP phases use 12 s exactly like the GT arm's -t 10. */
#define AP_LOOP_MS		12000u
#define AP_TCPDN_MS		12000u
#define AP_UDPDN_MS		20000u
#define AP_TCPUP_MS		30000u
#define AP_UDPUP_MS		20000u
#define AP_PEER_MS		1500u

enum ap_phase_kind {
	AP_LOOPBACK,
	AP_TCP_DOWN,
	AP_UDP_DOWN,
	AP_TCP_UP,
	AP_UDP_UP,
};

struct ap_phase {
	const char *name;
	const char *proto;
	const char *role;
	const char *air;
	unsigned port;
	unsigned window_ms;
	enum ap_phase_kind kind;
};

static const struct ap_phase ap_plan[] = {
	{ "loopback", "tcp", "client", "local", AP_TCP_PORT, AP_LOOP_MS,
	  AP_LOOPBACK },
	{ "tcpdn",    "tcp", "client", "down",  AP_TCP_PORT, AP_TCPDN_MS,
	  AP_TCP_DOWN },
	{ "udpdn",    "udp", "client", "down",  AP_UDP_PORT, AP_UDPDN_MS,
	  AP_UDP_DOWN },
	{ "tcpdn2",   "tcp", "client", "down",  AP_TCP_PORT, AP_TCPDN_MS,
	  AP_TCP_DOWN },
	{ "udpdn2",   "udp", "client", "down",  AP_UDP_PORT, AP_UDPDN_MS,
	  AP_UDP_DOWN },
	{ "tcpup",    "tcp", "server", "up",    AP_TCP_PORT, AP_TCPUP_MS,
	  AP_TCP_UP },
	{ "udpup",    "udp", "server", "up",    AP_UDP_PORT, AP_UDPUP_MS,
	  AP_UDP_UP },
};

#define AP_PLAN_LEN	(sizeof(ap_plan) / sizeof(ap_plan[0]))
#define AP_TRAFFIC_PHASES	6u

struct ap_snap {
	struct lkpi_net_diag diag;
	struct lkpi_net_stack_diag stack;
	struct lkpi_skb_pool_stats pool;
};

#if AP_DIAG
/* defined with the diagnostic sequence below; the table arm probes every
 * phase's end through this forward declaration */
static void ap_diag_probe(const char *tag, int full);
#endif

void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));
int lkpi_fw_iwl7260_fw_register(void);

static unsigned ap_log_lines;

static void
ap_sink(ulog_level_t level, const char *line)
{
	(void)level;
	if (line == NULL || ap_log_lines >= AP_LOG_MAX)
		return;
	if (strstr(line, "loaded firmware version") != NULL)
		CASE_ANCHOR("iwl7260_ap: firmware %s\n", line);
	else if (strstr(line, "wlan0:") != NULL ||
		 strstr(line, "iwlwifi") != NULL ||
		 strstr(line, "mac ctxt") != NULL ||
		 strstr(line, "beacon") != NULL ||
		 strstr(line, "station") != NULL ||
		 strstr(line, "sta_state") != NULL ||
		 strstr(line, "hostapd") != NULL ||
		 strstr(line, "AP-ENABLED") != NULL ||
		 strstr(line, "AP-STA") != NULL ||
		 strstr(line, "IEEE 802.11") != NULL ||
		 strstr(line, "WPA") != NULL ||
		 strstr(line, "EAPOL") != NULL ||
		 strstr(line, "RSN") != NULL ||
		 strstr(line, "4-Way") != NULL ||
		 strstr(line, "MIC") != NULL ||
		 strstr(line, "authenticated") != NULL ||
		 strstr(line, "associated") != NULL ||
		 strstr(line, "Failed") != NULL) {
		CASE_INFO("iwl7260_ap: drv %s\n", line);
		ap_log_lines++;
	}
}

static void
ap_snap_take(struct ap_snap *s)
{
	(void)memset(s, 0, sizeof(*s));
	lkpi_net_diag(&s->diag);
	lkpi_net_stack_diag(&s->stack);
	lkpi_skb_pool_stats(&s->pool);
}

static int
ap_run_phase(unsigned n, const struct ap_phase *ph)
{
	struct ap_snap before;
	struct ap_snap after;
	struct lkpi_net_phase r;
	int rc;

	(void)memset(&r, 0, sizeof(r));
	CASE_ANCHOR("iwl7260_ap: phase %u begin proto=%s role=%s air=%s port=%u "
		    "window_ms=%u name=%s peer_ms=%u\n",
		    n, ph->proto, ph->role, ph->air, ph->port, ph->window_ms,
		    ph->name, AP_PEER_MS);

	/* The peer's receiver/sender is started by the harness on this line;
	 * the margin is bench latency, and it sits outside the phase's own
	 * window (which starts below). */
	if (ph->kind != AP_LOOPBACK)
		osDelay(AP_PEER_MS);

	ap_snap_take(&before);
	switch (ph->kind) {
	case AP_LOOPBACK:
		rc = lkpi_net_loopback(ph->port, ph->window_ms, &r);
		break;
	case AP_TCP_DOWN:
		rc = lkpi_net_iperf_up(AP_PEER_IP, ph->port, ph->window_ms, &r);
		break;
	case AP_UDP_DOWN:
		rc = lkpi_net_udp_blast(AP_PEER_IP, ph->port, ph->window_ms,
					40000u, &r);
		break;
	case AP_TCP_UP:
		rc = lkpi_net_iperf_down(ph->port, ph->window_ms, &r);
		break;
	case AP_UDP_UP:
		rc = lkpi_net_udp_sink_window(ph->window_ms, &r);
		break;
	default:
		rc = -1;
		break;
	}
	ap_snap_take(&after);

	CASE_ANCHOR("iwl7260_ap: phase %u report bytes=%u ms=%u kbit=%u "
		    "pkts=%u\n",
		    n, r.bytes, r.ms, r.kbit, r.pkts);
	CASE_ANCHOR("iwl7260_ap: phase %u census rx_frames=%lu rx_bytes=%lu "
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
		unsigned int hard = 0u, thread = 0u;

		lkpi_pci_irq_counters(&hard, &thread);
		CASE_ANCHOR("iwl7260_ap: phase %u irq hard=%u thread=%u\n",
			    n, hard, thread);
	}
	{
		struct lkpi_wq_census wqc[LKPI_WQ_CENSUS_MAX];
		struct lkpi_wq_stats wqs;
		unsigned int i, nw;

		lkpi_wq_stats_get(&wqs);
		nw = lkpi_wq_census(wqc, LKPI_WQ_CENSUS_MAX);
		CASE_ANCHOR("iwl7260_ap: phase %u wq bh_full=%u bh_direct=%u "
			    "bh_runs=%u\n", n, (unsigned)wqs.full,
			    (unsigned)wqs.direct, (unsigned)wqs.runs);
		for (i = 0; i < nw; i++)
			CASE_ANCHOR("iwl7260_ap: phase %u wq[%u] %s runs=%u "
				    "refused=%u depth=%u\n", n, i, wqc[i].name,
				    (unsigned)wqc[i].runs,
				    (unsigned)wqc[i].refused,
				    (unsigned)wqc[i].depth);
	}
	{
		struct lkpi_tx_health txh;

		lkpi_tx_health_get(&txh);
		CASE_ANCHOR("iwl7260_ap: phase %u txhealth tx_status=%lu "
			    "stopq=%lu wakeq=%lu dq_calls=%lu dq_frames=%lu "
			    "mgmt=%lu data=%lu iwl_tx=%lu hcmd=%lu "
			    "hcmd_blockq=%lu hcmd_async=%lu mgmt_tx=%lu "
			    "beacon=%lu ps_sleep=%lu ps_wake=%lu "
			    "ps_buffered=%lu ready=%d sta=%d\n", n,
			    txh.tx_status, txh.stop_queue, txh.wake_queue,
			    txh.dequeue_calls, txh.dequeue_frames,
			    txh.status_mgmt, txh.status_data, txh.iwl_tx,
			    txh.hcmd, txh.hcmd_blockq, txh.hcmd_async,
			    txh.mgmt_tx, txh.mgmt_beacon, txh.ps_sleep,
			    txh.ps_wake, txh.ps_buffered, lkpi_hostap_ready(),
			    lkpi_hostap_sta_count());
	}
#if AP_DIAG && AP_DIAG_TABLE
	{
		char tag[16];

		(void)snprintf(tag, sizeof(tag), "phase-%u", n);
		ap_diag_probe(tag, 2);
	}
#endif
	CASE_ANCHOR("iwl7260_ap: phase %u done ok=%d\n", n, (rc == 0) ? 1 : 0);
	return rc;
}

#if AP_DIAG
/* --- the A4 stall round's observation sequence ----------------------------
 *
 * The question is not "is the data path slow" but "who stopped first".  Every
 * probe reads only counters and the four interrupt-status registers; the
 * freeze detector is a 1 s window on the interrupt delivery count, and the
 * verdict is the pre-registered table from docs/HANDOFF-AP-A4-TX-STALL.md:
 *
 *   thread-hung  the threaded half entered and never returned (started > done)
 *   napi-budget  the mask is zero and a poll returned the whole budget
 *   mask-zero    the mask is zero with no budget hit (unresolved, needs a probe)
 *   delivery     the mask is set and a cause is pending (MSI/GIC, not the device)
 *   fw-silent    the mask is set and nothing is pending (the device is quiet)
 *
 * The idle arm never claims a freeze: with no traffic, a flat interrupt count
 * is the expected baseline.  It reports the interrupt delta over a 60 s idle
 * window instead, which is what tells whether the AP's beacons reach the host
 * as notifications at all.
 */
static void
ap_diag_probe(const char *tag, int full)
{
	struct lkpi_napi_health napi;
	struct lkpi_mmio_health mmio;
	struct lkpi_tx_health txh;
	struct lkpi_skb_pool_stats pool;
	const struct lkpi_pci_stats *pci = lkpi_pci_stats_get();
	struct lkpi_pcie_csr_snap csr;
	struct lkpi_pcie_csr_snap ring[8];
	unsigned int i, n;
	int csr_rc;

	lkpi_napi_health_get(&napi);
	lkpi_mmio_health_get(&mmio);
	lkpi_tx_health_get(&txh);
	lkpi_skb_pool_stats(&pool);

	CASE_ANCHOR("iwl7260_ap: diag probe tag=%s irq_hard=%u irq_thread=%u "
		    "thread_done=%u napi_sched=%lu napi_polls=%lu "
		    "napi_budget=%lu napi_comp=%lu napi_ret=%lu napi_rx=%lu "
		    "skb=%u/%u tx_status=%lu iwl_tx=%lu dq=%lu\n",
		    tag, pci->irqs, pci->irq_threads, pci->irq_threads_done,
		    napi.schedules, napi.polls, napi.budget_hits,
		    napi.completes, napi.last_ret, napi.rounds_exhausted,
		    (unsigned)pool.skb_used,
		    (unsigned)pool.skb_high, txh.tx_status, txh.iwl_tx,
		    txh.dequeue_calls);
	CASE_ANCHOR("iwl7260_ap: diag mmio tag=%s mask_wr=%lu mask0=%lu "
		    "ack=%lu doorbell=%lu int_rd=%lu fh_rd=%lu en=%lu dis=%lu\n",
		    tag, mmio.mask_writes, mmio.mask_zero_writes,
		    mmio.ack_writes, mmio.doorbell_writes, mmio.int_reads,
		    mmio.fh_reads, mmio.int_enable_calls, mmio.int_disable_calls);
	/*
	 * The breadcrumbs go out before the CSR read: the last mask write is
	 * the one fact the task-context read could otherwise take with it if the
	 * device is wedged (the 2026-11-12 read hung the board once).
	 */
	if (full == 1) {
		for (i = 0; i < mmio.ring_count; i++) {
			unsigned int idx = (mmio.ring_next + LKPI_MMIO_RING_MAX -
			    mmio.ring_count + i) % LKPI_MMIO_RING_MAX;
			struct lkpi_mmio_op *op = &mmio.ring[idx];

			CASE_ANCHOR("iwl7260_ap: diag mmio-op tag=%s i=%u w=%u "
				    "ofs=0x%03x val=0x%08x ctx=%u tick=%lu "
				    "caller=0x%lx\n",
				    tag, i, (unsigned)op->write, (unsigned)op->ofs,
				    (unsigned)op->val, (unsigned)op->ctx, op->tick,
				    (unsigned long)(uintptr_t)op->caller);
		}
	}
	if (full == 2) {
		/* counters only: the table arm's per-phase probe must not add a
		 * device read between phases */
		return;
	}
	csr_rc = lkpi_pcie_csr_snapshot(&csr);
	CASE_ANCHOR("iwl7260_ap: diag csr tag=%s rc=%d int=0x%08x mask=0x%08x "
		    "fh=0x%08x reset=0x%08x tick=%lu\n",
		    tag, csr_rc,
		    csr_rc == 0 ? (unsigned)csr.int_status : 0u,
		    csr_rc == 0 ? (unsigned)csr.int_mask : 0u,
		    csr_rc == 0 ? (unsigned)csr.fh_status : 0u,
		    csr_rc == 0 ? (unsigned)csr.reset : 0u,
		    csr_rc == 0 ? csr.tick : 0ul);
	if (full != 1)
		return;
	n = lkpi_pcie_csr_irq_ring(ring, 8u);
	for (i = 0; i < n; i++)
		CASE_ANCHOR("iwl7260_ap: diag csr-ring tag=%s i=%u tick=%lu "
			    "int=0x%08x mask=0x%08x fh=0x%08x reset=0x%08x\n",
			    tag, i, ring[i].tick,
			    (unsigned)ring[i].int_status,
			    (unsigned)ring[i].int_mask,
			    (unsigned)ring[i].fh_status,
			    (unsigned)ring[i].reset);
}

static int
ap_diag_frozen(unsigned int hold_ms, unsigned int *hard_out)
{
	unsigned long t0 = jiffies;
	unsigned int h0, h;

	lkpi_pci_irq_counters(&h0, NULL);
	while ((jiffies - t0) < (unsigned long)hold_ms) {
		osDelay(50u);
		lkpi_pci_irq_counters(&h, NULL);
		if (h != h0) {
			if (hard_out != NULL)
				*hard_out = h;
			return 0;
		}
	}
	if (hard_out != NULL)
		*hard_out = h0;
	return 1;
}

static void
ap_diag_verdict(const char *where, unsigned int hard)
{
	const struct lkpi_pci_stats *pci = lkpi_pci_stats_get();
	struct lkpi_napi_health napi;
	struct lkpi_pcie_csr_snap csr;
	const char *v;
	int csr_rc;

	lkpi_napi_health_get(&napi);
	csr_rc = lkpi_pcie_csr_snapshot(&csr);
	if (pci->irq_threads > pci->irq_threads_done)
		v = "thread-hung";
	else if (csr_rc == 0 && csr.int_mask == 0u &&
		 napi.budget_hits != 0ul)
		v = "napi-budget";
	else if (csr_rc == 0 && csr.int_mask == 0u)
		v = "mask-zero";
	else if (csr_rc == 0 &&
		 (csr.int_status != 0u || csr.fh_status != 0u))
		v = "delivery";
	else if (csr_rc == 0)
		v = "fw-silent";
	else
		v = "no-csr";
	CASE_ANCHOR("iwl7260_ap: diag verdict=%s where=%s irq_hard=%u "
		    "thread=%u done=%u csr_rc=%d budget=%lu\n",
		    v, where, hard, pci->irq_threads,
		    pci->irq_threads_done, csr_rc, napi.budget_hits);
}

static int
ap_diag_run(void)
{
	unsigned int hard = 0u;

	CASE_ANCHOR("iwl7260_ap: diag begin mode=%s\n",
		    AP_DIAG_IDLE ? "idle" : "traffic");
#if AP_DIAG_IDLE
	{
		unsigned int h0, h1;
		unsigned long t0 = jiffies;

		lkpi_pci_irq_counters(&h0, NULL);
		ap_diag_probe("idle-start", 0);
		while ((jiffies - t0) < 60000ul) {
			osDelay(5000u);
			ap_diag_probe("idle", 0);
		}
		lkpi_pci_irq_counters(&h1, NULL);
		CASE_ANCHOR("iwl7260_ap: diag idle delta=%u over_ms=%lu "
			    "hard_end=%u\n", h1 - h0,
			    (unsigned long)(jiffies - t0), h1);
	}
	(void)hard;
#else
	ap_diag_probe("after-a3", 0);
	if (ap_diag_frozen(1000u, &hard)) {
		ap_diag_probe("stall", 1);
		ap_diag_verdict("after-a3", hard);
		return 0;
	}
	/*
	 * The original round's quiet window: A3 ends at ~22 s, the phase-1
	 * census at ~32 s already showed the freeze, and phase 1 itself ran no
	 * over-the-air data.  A 20 s idle here is the same shape (only the AP's
	 * own beacon/management activity); the detector runs continuously so a
	 * freeze inside the window is caught where it happens, not at its end.
	 */
	{
		unsigned int i;

		for (i = 0u; i < 20u; i++) {
			if (ap_diag_frozen(1000u, &hard)) {
				ap_diag_probe("stall", 1);
				ap_diag_verdict("idle-window", hard);
				return 0;
			}
		}
	}
	ap_diag_probe("idle20", 0);
	/*
	 * Then the handoff's minimal repro: one sustained 20 s UDP downlink at
	 * the preregistered 40 Mbit/s.  Two windows, because the original
	 * freeze was not tied to a frame count.
	 */
	{
		unsigned int r;

		for (r = 1u; r <= 2u; r++) {
			struct lkpi_net_phase rep;

			(void)memset(&rep, 0, sizeof(rep));
			(void)lkpi_net_udp_blast(AP_PEER_IP, AP_UDP_PORT,
						 20000u, 40000u, &rep);
			CASE_ANCHOR("iwl7260_ap: diag blast round=%u bytes=%u "
				    "kbit=%u\n", r, rep.bytes, rep.kbit);
			ap_diag_probe("udp", 0);
			if (ap_diag_frozen(1000u, &hard)) {
				ap_diag_probe("stall", 1);
				ap_diag_verdict("udp", hard);
				return 0;
			}
		}
	}
	osDelay(3000u);
	ap_diag_probe("idle", 0);
	if (ap_diag_frozen(1000u, &hard)) {
		ap_diag_probe("stall", 1);
		ap_diag_verdict("idle", hard);
		return 0;
	}
#endif
	CASE_ANCHOR("iwl7260_ap: diag verdict=none where=%s\n",
		    AP_DIAG_IDLE ? "idle" : "traffic");
	return 0;
}
#endif /* AP_DIAG */

int
case_iwl7260_ap_run(void)
{
	struct net_device *dev = NULL;
	struct wireless_dev *wdev;
	struct ieee80211_channel *chan;
	struct cfg80211_chan_def chandef;
	unsigned char mac[6];
	unsigned char sta_macs[4][6];
	char netif_mac[6];
	struct lkpi_hostap_cfg ap_cfg;
	unsigned int waited;
	unsigned int sta_n = 0u;
	unsigned int ping_rx = 0u;
	unsigned int rtt_min = 0U, rtt_avg = 0U, rtt_max = 0U;
	unsigned i;
	unsigned ok = 0u;
	int can_beacon;
	int rc;
	int ap_ok = 0;

	lkpi_core_init();
	CASE_ANCHOR("iwl7260_ap: core ready\n");
	lkpi_log_sink_register(ap_sink);

	lkpi_pcie_port_install();

	rc = lkpi_fw_iwl7260_fw_register();
	CASE_ANCHOR("iwl7260_ap: fw registered=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_ap: FAIL firmware registration rc=%d\n", rc);
		return 1;
	}

	lkpi_module_init_run();
	CASE_ANCHOR("iwl7260_ap: initcalls run\n");

	lkpi_pci_start();
	CASE_ANCHOR("iwl7260_ap: pci devs=%u\n",
		    (unsigned)lkpi_pci_dev_count_get());

	for (waited = 0U; waited < AP_WAIT_MS; waited += AP_POLL_MS) {
		dev = dev_get_by_name(&init_net, "wlan0");
		if (dev != NULL)
			break;
		osDelay(AP_POLL_MS);
	}
	if (dev == NULL) {
		CASE_ANCHOR("iwl7260_ap: FAIL no wlan0 after %u ms\n", waited);
		return 2;
	}
	wdev = dev->ieee80211_ptr;
	if (wdev == NULL || wdev->wiphy == NULL) {
		CASE_ANCHOR("iwl7260_ap: FAIL no wireless_dev\n");
		return 3;
	}
	memcpy(mac, dev->dev_addr, 6);
	CASE_ANCHOR("iwl7260_ap: wlan0 present=1 ifindex=%d mac=%02x:%02x:%02x:%02x:%02x:%02x iftype=%d\n",
		    dev->ifindex, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
		    (int)wdev->iftype);

	/* The channel must be in the driver's band and beaconable: this is the
	 * R6 discriminator when START_AP returns -EINVAL. */
	chan = ieee80211_get_channel(wdev->wiphy, (int)AP_FREQ_MHZ);
	can_beacon = 0;
	if (chan != NULL) {
		cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_HT20);
		can_beacon = cfg80211_reg_can_beacon(wdev->wiphy, &chandef,
						     NL80211_IFTYPE_AP) ? 1 : 0;
	}
	CASE_ANCHOR("iwl7260_ap: chan freq=%u present=%d flags=0x%08x "
		    "can_beacon=%d\n",
		    AP_FREQ_MHZ, chan != NULL ? 1 : 0,
		    chan != NULL ? (unsigned)chan->flags : 0u, can_beacon);
	if (chan == NULL || !can_beacon) {
		CASE_ANCHOR("iwl7260_ap: FAIL channel not beaconable\n");
		return 4;
	}

	/* --- A1: the default STA interface becomes the AP ---------------- */
	if (netif_running(dev))
		(void)dev_close(dev);
	rc = lkpi_nl80211_set_interface(dev, NL80211_IFTYPE_AP);
	CASE_ANCHOR("iwl7260_ap: set_iface rc=%d type=%d\n", rc,
		    lkpi_nl80211_iftype(dev));
	if (rc != 0 || lkpi_nl80211_iftype(dev) != NL80211_IFTYPE_AP) {
		CASE_ANCHOR("iwl7260_ap: FAIL set_iface rc=%d type=%d\n", rc,
			    lkpi_nl80211_iftype(dev));
		return 5;
	}

	rc = dev_open(dev);
	CASE_ANCHOR("iwl7260_ap: start rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_ap: FAIL dev_open rc=%d\n", rc);
		return 6;
	}

#if AP_DIAG
	/*
	 * The task-context CSR read's sanity check, taken while the device is
	 * known awake and before any traffic.  If this line does not appear the
	 * read itself is unsafe on this port and the boot is invalid; if it does,
	 * the same read after a freeze is credible.
	 */
	{
		struct lkpi_pcie_csr_snap early;
		int early_rc = lkpi_pcie_csr_snapshot(&early);

		CASE_ANCHOR("iwl7260_ap: diag csr-early rc=%d int=0x%08x "
			    "mask=0x%08x fh=0x%08x reset=0x%08x\n",
			    early_rc,
			    early_rc == 0 ? (unsigned)early.int_status : 0u,
			    early_rc == 0 ? (unsigned)early.int_mask : 0u,
			    early_rc == 0 ? (unsigned)early.fh_status : 0u,
			    early_rc == 0 ? (unsigned)early.reset : 0u);
	}
#endif

	/*
	 * The lwIP netif comes before hostapd: the NXP entry derives the
	 * interface name it configures from the port's uAP netif
	 * (net_get_uap_interface(), wm_net.h).
	 */
	rc = lkpi_net_wlan_up(AP_BOARD_IP, AP_BOARD_MASK, NULL, netif_mac);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_ap: FAIL netif up rc=%d\n", rc);
		return 7;
	}
	CASE_ANCHOR("iwl7260_ap: netif up name=wl0 ip=%s/24 mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    AP_BOARD_IP, (unsigned char)netif_mac[0],
		    (unsigned char)netif_mac[1], (unsigned char)netif_mac[2],
		    (unsigned char)netif_mac[3], (unsigned char)netif_mac[4],
		    (unsigned char)netif_mac[5]);

	/* --- A1: hostapd brings the AP up (beacon via src/ap/beacon.c) --- */
	ap_cfg.ifname = "wlan0";
	ap_cfg.ssid = AP_SSID;
	ap_cfg.channel = 11u;
	ap_cfg.beacon_int = 100u;
	ap_cfg.dtim = 2u;
	ap_cfg.passphrase = AP_PASSPHRASE;
	CASE_ANCHOR("iwl7260_ap: security wpa2=%d ssid=%s\n", AP_WPA2,
		    AP_SSID);
	rc = lkpi_hostap_start(&ap_cfg);
	CASE_ANCHOR("iwl7260_ap: hostap start rc=%d ready=%d\n", rc,
		    lkpi_hostap_ready());
	if (rc != 0 || !lkpi_hostap_ready()) {
		CASE_ANCHOR("iwl7260_ap: FAIL hostap start rc=%d\n", rc);
		return 10;
	}

	/*
	 * The harness starts the Pi's scan/join on the hostap start anchor.
	 * hostapd has to be serviced from here on, not after a blind sleep:
	 * the Pi's auth/assoc arrive inside this window.  Each poll hands the
	 * queued management frames to hostapd's event queue, which the
	 * imported eloop task dispatches in task context (NL80211_CMD_FRAME et
	 * al. may sleep; the RX path may not).
	 */
	CASE_ANCHOR("iwl7260_ap: peer window ms=%u\n", AP_PEER_WINDOW_MS);

#if AP_DIAG && AP_DIAG_IDLE
	/*
	 * The idle arm: no peer, no station, no traffic.  It answers whether the
	 * AP's own beacons reach the host as notifications at all, which is the
	 * baseline the traffic arm's freeze is read against.
	 */
	(void)ap_diag_run();
	CASE_ANCHOR("iwl7260_ap: PASS APD\n");
	return 0;
#endif

	/*
	 * --- A2: hand the queued management frames to hostapd -----------
	 * The wrap only copies them out of the RX path; here they go to the
	 * imported event queue, and hostapd's own ieee802_11.c answers.  The
	 * station's arrival is read from hostapd's table (its MLME owns it).
	 */
	sta_n = 0u;
	for (waited = 0U; waited < AP_CONNECT_WAIT_MS;
	     waited += AP_HOSTAP_POLL_MS) {
		(void)lkpi_hostap_poll();
		sta_n = (unsigned)lkpi_hostap_sta_count();
		if (sta_n != 0u)
			break;
		osDelay(AP_HOSTAP_POLL_MS);
	}
	if (sta_n == 0u) {
		CASE_ANCHOR("iwl7260_ap: FAIL no station after %u ms "
			    "(ready=%d)\n", AP_CONNECT_WAIT_MS,
			    lkpi_hostap_ready());
		return 12;
	}
	CASE_ANCHOR("iwl7260_ap: hostap up ready=%d sta=%u\n",
		    lkpi_hostap_ready(), sta_n);
	if (lkpi_nl80211_sta_count(dev, sta_macs, 4u, &sta_n) == 0 &&
	    sta_n != 0u)
		CASE_ANCHOR("iwl7260_ap: sta add n=%u mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
			    sta_n, sta_macs[0][0], sta_macs[0][1],
			    sta_macs[0][2], sta_macs[0][3], sta_macs[0][4],
			    sta_macs[0][5]);
	if (AP_WPA2) {
		unsigned ptk = 0u, gtk = 0u, fail = 0u;
		unsigned waited_wpa;

		/* hostapd's station table shows the association before the
		 * 4-way completes; wait for the AP's own pairwise state, not a
		 * sleep.  EAPOL events are posted by the RX sink; poll only
		 * drives the management queue the wrap filled. */
		for (waited_wpa = 0U; waited_wpa < AP_WPA_WAIT_MS;
		     waited_wpa += AP_HOSTAP_POLL_MS) {
			(void)lkpi_hostap_poll();
			if (lkpi_hostap_wpa2_sta_count() > 0)
				break;
			osDelay(AP_HOSTAP_POLL_MS);
		}
		lkpi_hostap_key_counts(&ptk, &gtk, &fail);
		CASE_ANCHOR("iwl7260_ap: wpa2 station sta=%d ptk=%u gtk=%u "
			    "fail=%u wait_ms=%u\n",
			    lkpi_hostap_wpa2_sta_count(), ptk, gtk, fail,
			    waited_wpa);
		if (lkpi_hostap_wpa2_sta_count() <= 0 || ptk == 0u ||
		    gtk == 0u || fail != 0u) {
			CASE_ANCHOR("iwl7260_ap: FAIL wpa2 handshake\n");
			return 17;
		}
		osDelay(AP_WPA_POST_HS_MS);
	}

	/* The station exists before the Pi's own iw connect returns; give the
	 * Pi a moment to finish and to answer ARP before the pings. */
	osDelay(AP_SETTLE_MS);

	/* --- A3: this board pings the peer (the Pi->AP arm is the harness's) */
	rc = lkpi_net_ping(AP_PEER_IP, AP_PING_COUNT, AP_PING_SMALL,
			   AP_PING_TIMEOUT_MS, &ping_rx, &rtt_min, &rtt_avg,
			   &rtt_max);
	CASE_ANCHOR("iwl7260_ap: ping tx=%u rx=%u payload=%u rtt_ms=%u/%u/%u\n",
		    AP_PING_COUNT, ping_rx, AP_PING_SMALL, rtt_min, rtt_avg,
		    rtt_max);
	if (rc != 0 || ping_rx != AP_PING_COUNT) {
		CASE_ANCHOR("iwl7260_ap: FAIL ping small rc=%d rx=%u\n", rc,
			    ping_rx);
		return 13;
	}
	rc = lkpi_net_ping(AP_PEER_IP, AP_PING_COUNT, AP_PING_LARGE,
			   AP_PING_TIMEOUT_MS, &ping_rx, &rtt_min, &rtt_avg,
			   &rtt_max);
	CASE_ANCHOR("iwl7260_ap: ping tx=%u rx=%u payload=%u rtt_ms=%u/%u/%u\n",
		    AP_PING_COUNT, ping_rx, AP_PING_LARGE, rtt_min, rtt_avg,
		    rtt_max);
	if (rc != 0 || ping_rx != AP_PING_COUNT) {
		CASE_ANCHOR("iwl7260_ap: FAIL ping large rc=%d rx=%u\n", rc,
			    ping_rx);
		return 14;
	}
	CASE_ANCHOR("iwl7260_ap: hostap stats ready=%d sta=%d\n",
		    lkpi_hostap_ready(), lkpi_hostap_sta_count());
	if (AP_WPA2) {
		unsigned etx = 0u, erx = 0u, edrop = 0u;

		/* The encryption gate stops here: the handshake and the data
		 * plane are proven; A4 is the next round's problem, not this
		 * profile's phase table. */
		lkpi_hostap_eapol_counts(&etx, &erx, &edrop);
		CASE_ANCHOR("iwl7260_ap: eapol tx=%u rx=%u drop=%u\n",
			    etx, erx, edrop);
		CASE_ANCHOR("iwl7260_ap: PASS APW\n");
		return 0;
	}
	CASE_ANCHOR("iwl7260_ap: PASS AP\n");
	ap_ok = 1;

#if AP_DIAG && !AP_DIAG_IDLE && !AP_DIAG_TABLE
	/*
	 * The traffic arm: the A4 diagnostic replaces the throughput table.  It
	 * runs the same A3 pings first (the freeze has been observed before the
	 * UDP phase), probes once, then drives short UDP windows while watching
	 * the interrupt-delivery count.  The verdict is the pre-registered
	 * table; the throughput table is the fixed image's arm, not this one.
	 */
	(void)ap_diag_run();
	CASE_ANCHOR("iwl7260_ap: PASS APD\n");
	return 0;
#endif
#if AP_DIAG && AP_DIAG_TABLE
	/* The table arm keeps the original round's exact phase sequence and
	 * probes at every boundary, so the freeze lands on a named phase. */
	ap_diag_probe("pre-table", 0);
#endif

	/* --- A4: the phase table ---------------------------------------- */
	rc = lkpi_net_iperf_server_start(AP_TCP_PORT);
	if (rc == 0)
		rc = lkpi_net_udp_sink_start(AP_UDP_PORT);
	CASE_ANCHOR("iwl7260_ap: server tcp=%u udp=%u ok=%d\n",
		    AP_TCP_PORT, AP_UDP_PORT, (rc == 0) ? 1 : 0);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260_ap: FAIL listener rc=%d\n", rc);
		return 15;
	}

	for (i = 0u; i < (unsigned)AP_PLAN_LEN; i++) {
		rc = ap_run_phase(i + 1u, &ap_plan[i]);
		if (rc == 0) {
			if (ap_plan[i].kind != AP_LOOPBACK)
				ok++;
		} else if (ap_plan[i].kind == AP_LOOPBACK) {
			CASE_ANCHOR("iwl7260_ap: note loopback failed "
				    "(attribution only)\n");
		}
	}

#if AP_DIAG && AP_DIAG_TABLE
	{
		unsigned int hard = 0u;

		/*
		 * A verdict is only meaningful at a freeze: on a healthy table the
		 * mask is non-zero and nothing is pending, which the table would
		 * otherwise mislabel as fw-silent.  The detector is the same 1 s
		 * window the traffic arm uses.
		 */
		if (ap_diag_frozen(1000u, &hard)) {
			ap_diag_probe("post-table", 1);
			ap_diag_verdict("table", hard);
		} else {
			ap_diag_probe("post-table", 0);
			CASE_ANCHOR("iwl7260_ap: diag verdict=none "
				    "where=table\n");
		}
		CASE_ANCHOR("iwl7260_ap: PASS APD\n");
		return 0;
	}
#endif
	if (ok == AP_TRAFFIC_PHASES && ap_ok) {
		CASE_ANCHOR("iwl7260_ap: PASS APT\n");
		return 0;
	}
	CASE_ANCHOR("iwl7260_ap: FAIL traffic phases %u/%u\n", ok,
		    AP_TRAFFIC_PHASES);
	return 16;
}

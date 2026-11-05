/* SPDX-License-Identifier: Apache-2.0 */
/*
 * @file   net_mdk_netio.c
 * @brief  Case net_mdk_netio (rk3568-itx): NETIO benchmark server over lwIP,
 *         ported from Arm's MDK-Middleware netio test
 *         (Test/Network/netio/netio.c, MIT, Copyright (c) 2004-2024 Arm Ltd).
 *
 * Why this one and not upstream: both speak the *same* wire protocol (TCP
 * 18767 control, UDP 18768 data, CMD_QUIT/C2S/S2C/RES), so the unmodified
 * upstream client on the Pi drives either. This one is the version that has a
 * UDP side that is known to run on an RTOS: Arm ships it as an RTX5 test with
 * its own socket layer, and the shape it uses - a control thread per protocol,
 * and a receive thread spawned only after the client asks for a C2S run - is
 * the part worth copying. Upstream netio starts UDP_Receiver unconditionally
 * from main and on this board that takes the kernel down before it prints.
 *
 * What was changed from Arm's file (and nothing else):
 *   - rl_net.h / RTE_Components.h -> lwIP sockets.h. The MDK names map
 *     one-to-one: SOCKADDR->struct sockaddr, SOCKADDR_IN->struct sockaddr_in,
 *     closesocket->lwip_close, BSD_ERROR->-1, BSD_EMSGSIZE->EMSGSIZE.
 *   - netInitialize() is the board's GMAC bring-up (same routine net.c uses).
 *   - The 4 KiB test buffer stays static (Arm's shape), so no heap is touched
 *     at all - the whole reason it is the safe reference for a second thread.
 *   - printf goes to log_emit: the board console only carries log anchors.
 *   - Arm compiles this with -mgeneral-regs-only here, so nothing needs the
 *     FPU; the timer that ends a 6 s window is the CMSIS-RTOS2 one-shot tick,
 *     as upstream.
 *
 * The measurement: the client sends C2S (it transmits, the board receives into
 * the buffer and counts), then S2C (the board transmits for 6 s, the client
 * receives). The board reports packet counts; the client reports rates. Both
 * ends are printed, and both are stored in the run directory.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cases.h"

#include "os/cmsis2/cmsis2.h"

#include "lwip/init.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/sockets.h"
#include "lwip/tcpip.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"

#include "log.h"

#include "lwip/stats.h"
#include "lwip/memp.h"

#include "generic_timer.h"
#include "net_prof.h"
#include "cpu_freq.h"
#ifdef NET_WATCH
#include "net_watch.h"
#endif

/* Three backends share this file: the GMAC one (the wired ruler) and the WLAN
 * ones (the per-line presentation layers over the net80211 bridge).  They
 * share the wire protocol and the accounting seams, not the line: the case
 * symbol selects the backend and an image never contains two.  Keeping the
 * GMAC path byte-for-byte where it was is what makes the wired regression
 * meaningful after this change. */
#if defined(RTOS_CASE_NET_MDK_NETIO)
#define NETIO_GMAC	1
#elif defined(RTOS_CASE_RTL8XXXU_NETIO)
#define NETIO_GMAC	0
#elif defined(RTOS_CASE_RTW88_NETIO)
#define NETIO_GMAC	0
#elif defined(RTOS_CASE_RTW89_NETIO)
#define NETIO_GMAC	0
#else
#error "net_mdk_netio.c needs RTOS_CASE_NET_MDK_NETIO, RTOS_CASE_RTL8XXXU_NETIO, RTOS_CASE_RTW88_NETIO or RTOS_CASE_RTW89_NETIO"
#endif

#if NETIO_GMAC
#include "dwc_eqos.h"
#include "eqos_types.h"
#include "eqos_rk3568.h"
#include "rtl8211f.h"
#include "ethnetif.h"
#else
#include "wlan_bridge.h"
#if defined(RTOS_CASE_RTW88_NETIO)
#include "rtw88_net.h"
#elif defined(RTOS_CASE_RTW89_NETIO)
#include "rtw89_net.h"
#else
#include "rtl8xxxu_net.h"
#endif
/* The receive-path split (RTOS-03): the transfer layer's completion census and
 * the interface layer's queue counters, so one anchor names the leg that ate
 * the A-MPDUs instead of inferring it from the bridge's totals. */
#include "linuxkpi_usb.h"
#include "lkpi_usb_cherryusb.h"
/* the socket-buffer supply, for the pool ledger the anchors print */
#include "skbuff_pool.h"

/* Same seam rtl8xxxu_join.c uses: the counters are exported unconditionally,
 * but their header lives in the mac80211 substitute's world, which this file
 * does not otherwise include. */
void lkpi80211_rx_boundary_counters(uint32_t *in, uint32_t *refused,
				    uint32_t *drops, uint32_t *frames,
				    uint32_t *qlen);
/* The transmit leg's two supplies, exported by the same substitute: how many
 * frames its txq engine is still holding, and whether the driver's transmit
 * gate is closed.  Declared here for the same reason as the pair above. */
void lkpi_txq_census(unsigned long *queued, unsigned long *handed,
		     unsigned long *orphans, unsigned long *refusals);
void lkpi80211_tx_gate_counters(uint32_t *stopped, uint32_t *wakes);
#endif

/* ---------------------------------------------------------------------------
 * The ported file (Arm's netio.c, with the MDK seams filled by lwIP).
 * ------------------------------------------------------------------------- */

#define BUF_SIZE        16384

#define NETIO_PORT      18767
#define NETIO_AUXPORT   18768

#if NETIO_GMAC
/* The bench's wired port: gmac1 (fe010000). gmac0 has no cable on this board. */
#define MN_GMAC		1u
#define MN_IP3		200u
#define MN_GW3		1u
#endif

#define CMD_QUIT        0
#define CMD_C2S         1
#define CMD_S2C         2
#define CMD_RES         3

#define BSD_ERROR       (-1)
#define BSD_EMSGSIZE    (EMSGSIZE)

#define MDK_ANCHOR(...) log_emit(ULOG_ALWAYS_LEVEL, __VA_ARGS__)

typedef struct {
	uint32_t cmd;
	uint32_t data;
} CONTROL;

/* The board's own IPv4 address in network order; the UDP data socket binds to
 * it so data datagrams cannot be matched by the control connection's stream. */
static uint32_t g_local_ip;

static struct {
	int32_t command;
	uint32_t pkt_size;
	osTimerId_t timer;
	struct sockaddr_in *client;
	uint8_t *buf;
	uint32_t buf_size;
	int32_t udp_sock;
	uint32_t pkt_count;
	uint32_t rx_bytes;
	uint32_t tx_bytes;
	bool timeover;
	int32_t err_line;
	int32_t err_bsd;
} io;

/* Transmit refusals the last S2C cell waited through (WLAN only).  The cell's
 * done line carries it so the console shows whether the uplink was running
 * against backpressure or against the wire: the flag W6-03's U1 asks for. */
static unsigned long netio_s2c_refusals;

static void cb_timer(void *arg) { (void)arg; io.timeover = true; }

static uint32_t rand32(void)
{
	static uint32_t rnd_state;

	rnd_state *= 1103515245;
	rnd_state += 12345;
	return rnd_state;
}

/* ---------------------------------------------------------------------------
 * Per-cell layered accounting (K2-01 W1.1).
 *
 * One line per completed measurement cell, cumulative since boot, so the
 * sweep's report diffs consecutive lines and each layer of the receive path
 * gets its own column. The point is that a lost datagram has to show up in
 * exactly one of these, and the one it shows up in names the layer:
 *
 *   mmc_ovf      the MAC's FIFO/DMA ring was full  -> ring depth / drain rate
 *   rx_frames    completed descriptors               (driver saw it)
 *   fed          frames handed to the stack          (bridge saw it)
 *   pool_fail    pbuf refused                        (lwIP pool)
 *   input_fail   netif->input said no                (stack/mbox)
 *   udp_in/drop  reached UDP, or was discarded       (transport)
 *
 * Nothing here sits on the data path: it is called at cell boundaries only.
 * ------------------------------------------------------------------------- */
/* K2-14: a spot sample of the CPU clock, taken from inside the same window
 * the cell ratio (`netio: cpu ... mhz_x100`) is drawn from.
 *
 * The per-cell ratio is cycles over wall time, so it is only a frequency if
 * the counter advances with the core clock and the wall clock runs at 24 MHz
 * throughout.  The busy-spin probe at case start (`netio: cpu tag=spin`) says
 * the 1.992 GHz request lands at 1.689 GHz, while the uplink cell ratio of the
 * same boot reads 2.09-2.39 GHz - two numbers for one fixed OPP, which is the
 * question this line exists to settle: a spot sample taken *inside* the burst
 * says whether the core really ran faster there, or whether the ratio is an
 * accounting artifact.  The sample is a 1 ms busy spin, never an idle delay,
 * so a stopped counter cannot hide in it. */
static void emit_cpu_spot(const char *tag)
{
#if defined(CONFIG_NETIO_CPU_CALIB) && CONFIG_NETIO_CPU_CALIB
	uint32_t t0;
	uint32_t t1;
	uint64_t c0;
	uint64_t c1;

	if (!net_prof_cpu_probe_ok())
		return;
	t0 = (uint32_t)gtimer_count_get();
	c0 = net_prof_cpu_cycles();
	do {
		t1 = (uint32_t)gtimer_count_get();
	} while ((uint32_t)(t1 - t0) < (24u * 1000u));
	c1 = net_prof_cpu_cycles();
	MDK_ANCHOR("netio: cpuspot tag=%s mhz_x100=%lu ticks=%lu cycles=%lu\n",
		   tag,
		   (unsigned long)(((c1 - c0) * 24ull * 100ull) /
				   (uint32_t)(t1 - t0)),
		   (unsigned long)(t1 - t0), (unsigned long)(c1 - c0));
#else
	(void)tag;
#endif
}

#if !NETIO_GMAC
/*
 * The receive-path ledger for the WLAN arm.  Four layers, one line each, so a
 * frozen receive names its leg:
 *
 *   usb_*  the controller/backend: completions, errors, and the length shape
 *	 (a full-buffer completion says the transfer outgrew the URB or was cut
 *	 at its end; a short one says the chip delivered less than it asked for)
 *   ep_*   the endpoint engine and the driver's re-arm: qlen + in_flight is
 *	 the driver's RX URB pool (32 at boot); a shrinking pool is the driver
 *	 retiring buffers on error completions
 *   rx_*   the interface layer: frames handed to ieee80211_rx_irqsafe, ones
 *	 the linearizer refused, ones the queue dropped, and ones the worker
 *	 finally delivered to the bridge
 *   desc   the first 16 bytes of the last successful bulk-IN transfer, i.e.
 *	 the receive descriptor the driver will parse (pktlen/drvinfo/shift)
 *
 * It deliberately does not sample the controller's registers: AGENTS.md's
 * rule is that only a one-shot probe may touch them (a periodic sampler read
 * the xHCI into silence once), and everything above comes from counters.
 */
static void emit_rxpath(const char *tag)
{
#if defined(CONFIG_NETIO_RXPATH) && CONFIG_NETIO_RXPATH
	struct lkpi_usb_chusb_stats cs;
	struct lkpi_usb_engine_stats es;
	struct lkpi_usb_ep_stats ep;
	uint32_t rx_in = 0, rx_ref = 0, rx_drop = 0, rx_frames = 0, rx_q = 0;
	unsigned int i, best_comp = 0;
	bool have_ep = false;

	memset(&cs, 0, sizeof(cs));
	memset(&es, 0, sizeof(es));
	memset(&ep, 0, sizeof(ep));
	lkpi_usb_cherryusb_stats(&cs);
	lkpi_usb_engine_stats_get(&es);
	lkpi80211_rx_boundary_counters(&rx_in, &rx_ref, &rx_drop, &rx_frames,
					&rx_q);

	/* The receive endpoint is the IN endpoint that moved the most frames;
	 * an OUT endpoint and an idle extra IN endpoint must not shadow it. */
	for (i = 0; i < lkpi_usb_ep_count(); i++) {
		struct lkpi_usb_ep_stats cand;

		if (lkpi_usb_ep_stats_get(i, &cand) != 0)
			continue;
		if ((cand.ep_addr & 0x80u) == 0u || cand.enqueued == 0u)
			continue;
		if (!have_ep || cand.completions >= best_comp) {
			ep = cand;
			best_comp = cand.completions;
			have_ep = true;
		}
	}

	/* ULOG caps one message at 256 bytes, so the ledger is three lines:
	 * the transfer census, the endpoint/interface split, and the descriptor
	 * the driver will parse. */
	MDK_ANCHOR("netio: rxpath tag=%s usb_rx=%u usb_err=%u usb_last=%u "
		   "usb_lasterr=%d usb_full=%u usb_max=%u "
		   "usb_hist=%u/%u/%u/%u/%u/%u\n",
		   tag, cs.rx_completes, cs.rx_errors, cs.rx_last_len,
		   cs.rx_last_err, cs.rx_full, cs.rx_max_len,
		   cs.rx_len_hist[0], cs.rx_len_hist[1], cs.rx_len_hist[2],
		   cs.rx_len_hist[3], cs.rx_len_hist[4], cs.rx_len_hist[5]);
	MDK_ANCHOR("netio: rxconn tag=%s rx_in=%lu rx_ref=%lu rx_drop=%lu "
		   "rx_frames=%lu rx_q=%lu ep=%02x ep_q=%u ep_inf=%u "
		   "ep_enq=%u ep_comp=%u ep_err=%u ep_last=%u\n",
		   tag, (unsigned long)rx_in, (unsigned long)rx_ref,
		   (unsigned long)rx_drop, (unsigned long)rx_frames,
		   (unsigned long)rx_q,
		   have_ep ? ep.ep_addr : 0u, ep.qlen, ep.in_flight,
		   ep.enqueued, ep.completions, ep.errors, ep.last_in_len);
	MDK_ANCHOR("netio: rxdesc tag=%s w=%08x,%08x,%08x,%08x\n",
		   tag, ep.last_in_words[0], ep.last_in_words[1],
		   ep.last_in_words[2], ep.last_in_words[3]);
	MDK_ANCHOR("netio: usbeng tag=%s polls=%u pdrain=%u kicks=%u "
		   "eng_comp=%u eng_err=%u parked=%u parked_now=%u\n",
		   tag, es.polls, es.poll_drained, es.kicks, es.completions,
		   es.errors, es.parked_now, es.in_flight);
#else
	(void)tag;
#endif
}
#endif /* !NETIO_GMAC */

/*
 * The transmit leg's census: frames still inside the substitute's txq engine,
 * the engine's own counters, and the driver's transmit gate.  This is the one
 * thing the receive-side ledger cannot say - a cell that fills the socket
 * buffer pool and leaves nothing in flight at the controller has its frames in
 * `q` (the driver never dequeued them) or the gate is shut (`stopped`), and
 * those two have different fixes.
 */
static void emit_tx_census(const char *tag)
{
	unsigned long q = 0, handed = 0, orph = 0, refu = 0;
	uint32_t stopped = 0, wakes = 0;

	lkpi_txq_census(&q, &handed, &orph, &refu);
	lkpi80211_tx_gate_counters(&stopped, &wakes);
	MDK_ANCHOR("netio: tx tag=%s q=%lu handed=%lu orph=%lu refu=%lu "
		   "stopped=%08x wakes=%u\n",
		   tag, q, handed, orph, refu, (unsigned)stopped,
		   (unsigned)wakes);
}

/* The controller's own view of the transmit leg, on its own for the same
 * reason: a cell that stalls needs "what is still in flight at the device"
 * beside "what the stack still holds". */
static void emit_usb(const char *tag)
{
	struct lkpi_usb_engine_stats es;

	memset(&es, 0, sizeof(es));
	lkpi_usb_engine_stats_get(&es);
	{
		struct lkpi_usb_chusb_stats cs;

		memset(&cs, 0, sizeof(cs));
		lkpi_usb_cherryusb_stats(&cs);
		MDK_ANCHOR("netio: usb tag=%s polls=%u drained=%u completions=%u "
			   "errors=%u in_flight=%u parked=%u dph=%u idleq=%u "
			   "recov=%u\n",
			   tag, es.polls, es.poll_drained, es.completions,
			   es.errors, es.in_flight, es.parked_now,
			   es.done_pending, es.idle_queued, es.recovered);
		/* The backend's own supply: `slot_exh` climbing means a submit
		 * was refused for want of a transfer slot (the engine then
		 * parks, and a parked endpoint only moves when a drain batch
		 * ends); `stg_exh` is the same for the DMA stage; `refused` is
		 * the controller rejecting the urb outright. */
		MDK_ANCHOR("netio: usbc tag=%s sub=%u done=%u slot=%u "
			   "slot_exh=%u stg_exh=%u stg_ovf=%u refuse=%u "
			   "zlp=%u hin=%u\n",
			   tag, cs.submits, cs.completed,
			   (unsigned)CONFIG_LKPI_USB_CHERRYUSB_SLOTS,
			   cs.slots_exhausted, cs.stage_exhausted,
			   cs.stage_overflow, cs.refused,
			   cs.zlp_added, cs.zero_packet_ignored);
	}
	/*
	 * Which endpoints are carrying the transfers, when a stall leaves some
	 * of them armed forever.  The address says the direction (0x8x is an IN
	 * endpoint, the driver's receive; the OUT endpoint is its transmit), and
	 * `enq` against `comp` separates "the driver stopped submitting" from
	 * "the controller stopped completing".  IN endpoints are listed only
	 * while busy; the OUT endpoint is listed every sample even when idle,
	 * because its cumulative enq/comp is the TX aggregation census
	 * (frames per URB = wlan_done / comp).  `d` is the done handoff's own
	 * state: 1 with `f == 0` means a completion sits in the slot and no
	 * later transfer on this endpoint can be claimed (see the header's ep
	 * stats); `l` says whether the drain has it listed.
	 */
	{
		unsigned int i;

		for (i = 0; i < es.eps; i++) {
			struct lkpi_usb_ep_stats ep;

			memset(&ep, 0, sizeof(ep));
			if (lkpi_usb_ep_stats_get(i, &ep) != 0)
				break;
			if ((ep.ep_addr & 0x80u) != 0u &&
			    ep.qlen == 0 && ep.in_flight == 0 &&
			    ep.done_pending == 0)
				continue;
			MDK_ANCHOR("netio: usbep tag=%s i=%u addr=0x%02x q=%u f=%u "
				   "d=%u l=%u enq=%u comp=%u err=%u dup=%u "
				   "recov=%u idle=%u\n",
				   tag, ep.index, ep.ep_addr, ep.qlen,
				   ep.in_flight, ep.done_pending, ep.done_listed,
				   ep.enqueued, ep.completions,
				   ep.errors, ep.done_dup, ep.recovered,
				   ep.idle_kicked);
		}
	}
	/* The probe's own ledger: whether it ran, and how each of its transfers
	 * came out.  It rides the heartbeat rather than living only on the
	 * probe's own lines because a console can drop lines under a pool
	 * refusal storm, and this count cannot be reconstructed after the fact. */
	{
		struct lkpi_usb_chusb_stats cs;

		memset(&cs, 0, sizeof(cs));
		lkpi_usb_cherryusb_stats(&cs);
		if (cs.probe_runs != 0)
			MDK_ANCHOR("netio: usbprobe tag=%s runs=%u desc_ok=%u "
				   "desc_fail=%u other_ok=%u other_fail=%u\n",
				   tag, cs.probe_runs, cs.probe_desc_ok,
				   cs.probe_desc_fail, cs.probe_other_ok,
				   cs.probe_other_fail);
	}
	/*
	 * The one aggregated bulk-OUT buffer the seam kept, in hex.  The driver's
	 * own dump cannot be relied on: the console takes ~9 ms per line, so a
	 * burst that starts inside a heartbeat loses the first lines.  This rides
	 * the anchor path instead (written past the log ring), and it is the only
	 * host-side record of what the chip was handed - the Linux ko's usbmon is
	 * its known answer.
	 */
	{
		static int printed;
		struct lkpi_usb_agg_snap as;

		memset(&as, 0, sizeof(as));
		lkpi_usb_cherryusb_agg_snap(&as);
		if (as.valid && !printed) {
			unsigned int o;

			printed = 1;
			MDK_ANCHOR("netio: aggbuf tag=%s len=%u act=%u err=%d n=%u\n",
				   tag, as.len, as.act, as.err, as.used);
			for (o = 0; o < as.used; o += 32) {
				char hex[65];
				unsigned int i;
				unsigned int m = as.used - o < 32u ? as.used - o : 32u;

				for (i = 0; i < m; i++)
					snprintf(hex + i * 2u, 3, "%02x",
						 as.buf[o + i]);
				hex[m * 2u] = 0;
				MDK_ANCHOR("netio: aggbuf tag=%s o=%u %s\n",
					   tag, o, hex);
			}
		}
	}
}

/* The socket-buffer supply, on its own so the boot heartbeat can print it
 * without the rest of the ledger. */
static int stall_reported;

static void emit_pool(const char *tag)
{
	struct lkpi_skb_pool_stats ps;

	memset(&ps, 0, sizeof(ps));
	lkpi_skb_pool_stats(&ps);
	/*
	 * The first refusal is the wedge, and the controller's schedule is what
	 * decides it: a qTD still marked active on the stalled endpoint says the
	 * device is not answering, its active bit cleared says this port never
	 * retired a finished transfer.  One shot, marker first - the read itself
	 * is the only thing that could hang, and a missing "census done" line
	 * names it.
	 */
	if (ps.alloc_fail != 0 && !stall_reported) {
		stall_reported = 1;
		MDK_ANCHOR("netio: stall tag=%s first refusal, controller census follows\n",
			   tag);
		lkpi_usb_controller_report();
		MDK_ANCHOR("netio: stall tag=%s controller census done\n", tag);
		/* What the bridge had already refused or stashed at the same
		 * instant: a refusal storm that the bridge absorbed (stash) or
		 * turned into a permanent drop is a different fix from one it
		 * never saw. */
		wlan_bridge_link_dump();
	}
	MDK_ANCHOR("netio: pool tag=%s hdr=%lu/%lu heap_hdr=%lu/%u "
		   "data=%lu/%lu high=%lu/%lu heap_live=%lu af=%lu "
		   "af_hdr=%lu af_s=%lu af_l=%lu n_alloc=%lu n_free=%lu\n",
		   tag, (unsigned long)ps.skb_used,
		   (unsigned long)ps.hdr_total,
		   (unsigned long)ps.heap_hdr_live, ps.heap_hdr_cap,
		   (unsigned long)ps.data_used, (unsigned long)ps.data_total,
		   (unsigned long)ps.skb_high, (unsigned long)ps.data_high,
		   (unsigned long)ps.dyalloc_live,
		   (unsigned long)ps.alloc_fail,
		   (unsigned long)ps.fail_hdr,
		   (unsigned long)ps.fail_small,
		   (unsigned long)ps.fail_large,
		   (unsigned long)ps.n_alloc,
		   (unsigned long)ps.n_free);
#if !NETIO_GMAC
	/*
	 * Who holds the live objects, and whether the driver's workqueue is
	 * dropping schedules.  The pool total says how many are held; only the
	 * owner rows say whether the receive buffer queue or the delivery queue
	 * is the one growing.  A nonzero `wq_full` beside a growing owner is the
	 * dropped-schedule signature: the queue_work() the driver never checks.
	 */
	{
		struct lkpi_skbq_census c[LKPI_SKBQ_CENSUS_MAX];
		struct lkpi_wq_stats wq;
		unsigned int n, i;

		memset(c, 0, sizeof(c));
		n = lkpi_skb_queue_census(c, 3u);
		for (i = 0; i < n; i++) {
			if (c[i].items == 0u)
				continue;
			MDK_ANCHOR("netio: skbq tag=%s q=%lx n=%lu b=%lu\n",
				   tag, (unsigned long)c[i].q,
				   (unsigned long)c[i].items,
				   (unsigned long)c[i].bytes);
		}
		memset(&wq, 0, sizeof(wq));
		lkpi_wq_stats_get(&wq);
		/*
		 * Per queue: a driver workqueue whose runs stop growing while
		 * its refused count stays flat was never asked again - the
		 * frames behind it are the pool's live objects.  One line: the
		 * name and the three numbers are the whole account.
		 */
		{
			struct lkpi_wq_census wc[LKPI_WQ_CENSUS_MAX];
			char line[248];
			unsigned int nw, k;
			size_t at;

			memset(wc, 0, sizeof(wc));
			nw = lkpi_wq_census(wc, LKPI_WQ_CENSUS_MAX);
			at = (size_t)snprintf(line, sizeof(line),
					      "netio: wq tag=%s", tag);
			for (k = 0; k < nw && at < sizeof(line) - 1u; k++) {
				int wrote = snprintf(line + at, sizeof(line) - at,
						     " %s=%lu/%lu/%lu",
						     wc[k].name,
						     (unsigned long)wc[k].runs,
						     (unsigned long)wc[k].refused,
						     (unsigned long)wc[k].depth);

				if (wrote < 0)
					break;
				at += (size_t)wrote;
			}
			MDK_ANCHOR("%s\n", line);
		}
		MDK_ANCHOR("netio: supply tag=%s wq_runs=%lu wq_full=%lu "
			   "wq_qmax=%lu wq_direct=%lu rxfail=%lu\n",
			   tag, (unsigned long)wq.runs,
			   (unsigned long)wq.full,
			   (unsigned long)wq.queue_max,
			   (unsigned long)wq.direct,
#if defined(RTOS_CASE_RTW88_NETIO)
			   (unsigned long)rtw88_net_rx_fail());
#else
			   0ul);
#endif
	}
#endif
}

static void emit_layer_counters(const char *tag)
{
#if NETIO_GMAC
	struct dwc_eqos_stats ds;
	struct dwc_eqos_mmc_stats mmc;
	struct dwc_eqos_diag dg;
	unsigned long polls = 0, pool_fail = 0, input_fail = 0, fed = 0;
	unsigned long pbuf_err = 0, mem_err = 0, udp_in = 0, udp_inerr = 0;
	unsigned long ip_drop = 0, tcp_retrans = 0;

	memset(&ds, 0, sizeof(ds));
	memset(&mmc, 0, sizeof(mmc));
	memset(&dg, 0, sizeof(dg));
	dwc_eqos_get_stats(MN_GMAC, &ds);
	dwc_eqos_get_mmc_stats(MN_GMAC, &mmc);
	dwc_eqos_get_diag(MN_GMAC, &dg);
	ethnetif_get_diag(&polls, &pool_fail, &input_fail, &fed);

#if LWIP_STATS
	mem_err = (unsigned long)lwip_stats.mem.err;
#endif
#if LWIP_STATS && MEMP_STATS
	if (lwip_stats.memp[MEMP_PBUF] != NULL)
		pbuf_err = (unsigned long)lwip_stats.memp[MEMP_PBUF]->err;
#endif
#if MIB2_STATS
	udp_in = (unsigned long)lwip_stats.mib2.udpindatagrams;
	udp_inerr = (unsigned long)lwip_stats.mib2.udpinerrors;
	ip_drop = (unsigned long)lwip_stats.mib2.ipindiscards;
	tcp_retrans = (unsigned long)lwip_stats.mib2.tcpretranssegs;
#endif

	{
		uint32_t zc_wrap = 0u, zc_free = 0u, zc_dead = 0u;

		/* Zero-copy lifetime audit (K2-16 B): all three zero on a
		 * correct run; a non-zero count names the owner that lost
		 * track before lwIP's pbuf ref assert fires. */
		ethnetif_zc_audit(&zc_wrap, &zc_free, &zc_dead);
		MDK_ANCHOR("netio: zc tag=%s wrap_live=%lu free_dead=%lu "
			   "dead_handle=%lu\n",
			   tag, (unsigned long)zc_wrap, (unsigned long)zc_free,
			   (unsigned long)zc_dead);
	}
	MDK_ANCHOR("netio: layers tag=%s mmc_ovf=%lu mmc_crc=%lu rx_frames=%lu rx_drop=%lu "
		   "rx_ru=%lu csum_drop=%lu rx_err=%lu dma_err=%lu bridge_poll=%lu fed=%lu "
		   "pool_fail=%lu input_fail=%lu mem_err=%lu pbuf_err=%lu\n",
		   tag,
		   (unsigned long)mmc.rx_fifo_overflow,
		   (unsigned long)mmc.rx_crc_error,
		   /* Order must match the labels: staged frames, staging-ring
		    * drops, ring-buffer-unavailable. The K2-06 first pass had
		    * rx_buf_unavail in the rx_frames slot, which printed the
		    * staged count as "rx_drop" and made a clean receive path
		    * look like a 135k-frame loss; a reader must be able to
		    * trust a field name without re-deriving the arg list. */
		   (unsigned long)ds.rx_frames,
		   (unsigned long)ds.rx_dropped,
		   (unsigned long)ds.rx_buf_unavail,
		   (unsigned long)ds.rx_csum_drops,
		   (unsigned long)ds.rx_errors,
		   (unsigned long)ds.dma_errors,
		   polls, fed, pool_fail, input_fail,
		   mem_err, pbuf_err);

	/* The transmit leg, on its own line: one line per leg survives the
	 * 256-char message limit, and these are the numbers that decide between
	 * "the ring's completion signal is under-counted" (tx_waits high,
	 * tx_sem drained to 0), "one send is too expensive" (tx_occ_max small,
	 * no waits) and "the DMA never retires" (tx_waits high, irq low). */
	{
		struct net_prof_snap pc;

		memset(&pc, 0, sizeof(pc));
		net_prof_take(&pc);
		/* mac_tx/mac_uf are the MAC's own count of frames transmitted
		 * and of MTL underflows: mac_tx well below tx_frames with mac_uf
		 * climbing means the transmit leg is starving the MTL, not
		 * dropping at the bridge (tx_drop) - the two failure modes have
		 * opposite fixes. */
		/* mac_uf is gone from this line on purpose: dwc_eqos.h records
		 * the TX MMC offset as wrong on this IP (0x748 is carrier
		 * error), so a zero there is not "no underflow". txsweephits
		 * counts transmit reclaims that found another context inside
		 * the walk - the race the guard serialises. */
		MDK_ANCHOR("netio: tx tag=%s tx_frames=%lu tx_drop=%lu mac_tx=%lu "
			   "mac_tx_oct=%lu txsweephits=%lu rx_irq=%lu "
			   "tx_waits=%lu tx_wait_ticks=%lu tx_occ_max=%lu tx_sem=%lu "
			   "irq=%lu irq_tx=%lu\n",
			   tag,
			   (unsigned long)ds.tx_frames,
			   (unsigned long)ds.tx_dropped,
			   (unsigned long)mmc.tx_pkts_gb,
			   (unsigned long)mmc.tx_octets_gb,
			   (unsigned long)dg.tx_sweep_busy_hits,
			   (unsigned long)ds.rx_irqs,
			   (unsigned long)dg.tx_waits,
			   (unsigned long)dg.tx_wait_ticks,
			   (unsigned long)dg.tx_occ_max,
			   (unsigned long)dg.tx_sem_count,
			   (unsigned long)dg.irq_n,
			   (unsigned long)pc.irq_tx);
	}

	/* K2-14: split "waited a few dozen polls and a slot came free" (healthy -
	 * 287k of those in the K2-07 run) from "gave up because the completion
	 * never came back" (the 475-frame abort). Both used to be one tx_waits
	 * tick. This line is what the 475-frame verdict rests on, so it prints
	 * whenever anyone had to wait at all. */
	if (dg.tx_full_episodes > 0u) {
		MDK_ANCHOR("netio: txfull tag=%s episodes=%lu spins=%lu zero=%lu "
			   "freed=%lu giveups=%lu wait24=%lu head=%lu tail=%lu "
			   "mac_tx=%lu ch0=%08lx curtx=%08lx base=%08lx mtl=%08lx\n",
			   tag,
			   (unsigned long)dg.tx_full_episodes,
			   (unsigned long)dg.tx_full_spins,
			   (unsigned long)dg.tx_full_zero,
			   (unsigned long)dg.tx_full_freed,
			   (unsigned long)dg.tx_full_giveups,
			   (unsigned long)dg.tx_full_last_wait24,
			   (unsigned long)dg.tx_full_last_head,
			   (unsigned long)dg.tx_full_last_tail,
			   (unsigned long)dg.tx_full_last_mac_tx,
			   (unsigned long)dg.tx_full_last_ch0_status,
			   (unsigned long)dg.tx_full_last_cur_tx_desc,
			   (unsigned long)dg.tx_full_last_tx_base,
			   (unsigned long)dg.tx_full_last_mtl_int);
		/* Separate line: the enable bits that would explain a stalled
		 * ring without a code defect (TE bit 1 of MAC_CONFIGURATION,
		 * TXQEN bits 3:2 of the MTL queue mode, ST bit 0 of the DMA TX
		 * control) plus RFE, which decides whether a received PAUSE
		 * frame can stop transmission at all. */
		MDK_ANCHOR("netio: txstall tag=%s maccfg=%08lx rxfc=%08lx "
			   "txqom=%08lx txctrl=%08lx intren=%08lx\n",
			   tag,
			   (unsigned long)dg.tx_full_last_mac_conf,
			   (unsigned long)dg.tx_full_last_rx_fc,
			   (unsigned long)dg.tx_full_last_txq_mode,
			   (unsigned long)dg.tx_full_last_tx_ctrl,
			   (unsigned long)dg.tx_full_last_intr_en);
	}

	MDK_ANCHOR("netio: mib tag=%s udp_in=%lu udp_inerr=%lu ip_drop=%lu tcp_retrans=%lu\n",
		   tag, udp_in, udp_inerr, ip_drop, tcp_retrans);
#else /* WLAN backend */
	unsigned long pbuf_err = 0, mem_err = 0, udp_in = 0, udp_inerr = 0;
	unsigned long ip_drop = 0, tcp_retrans = 0;
	unsigned long ip_in = 0, link_drop = 0;
	struct wlan_bridge_counters wc;
	struct lkpi_usb_engine_stats es;

	/* The WLAN receive path's ledger is the bridge's, not the MAC's: the
	 * net80211 port owns the driver's descriptor/MAC counters and does not
	 * export them through this seam.  A lost frame therefore has to show up
	 * in bridge_rx vs bridge_rxdata, in the stack's own mib, or on the AP
	 * (the pcap / station dump are the independent witness).  The transmit
	 * leg is data_tx vs tx_completes (the completion gap is a stall, not a
	 * drop) plus the AP's receive side. */
	wlan_bridge_stats(&wc);
	memset(&es, 0, sizeof(es));
	lkpi_usb_engine_stats_get(&es);
#if LWIP_STATS
	mem_err = (unsigned long)lwip_stats.mem.err;
#endif
#if LWIP_STATS && MEMP_STATS
	if (lwip_stats.memp[MEMP_PBUF] != NULL)
		pbuf_err = (unsigned long)lwip_stats.memp[MEMP_PBUF]->err;
#endif
#if MIB2_STATS
	udp_in = (unsigned long)lwip_stats.mib2.udpindatagrams;
	udp_inerr = (unsigned long)lwip_stats.mib2.udpinerrors;
	ip_drop = (unsigned long)lwip_stats.mib2.ipindiscards;
	tcp_retrans = (unsigned long)lwip_stats.mib2.tcpretranssegs;
#endif

	/* ULOG caps one message at 256 bytes; this ledger used to be a single
	 * line and the transmit half fell off the end of it under load (the
	 * 'wlan_pen' truncation), which is exactly the state a stalled cell
	 * needs read.  Two lines, the receive half and the transmit half. */
	MDK_ANCHOR("netio: layers tag=%s wlan_tx=%u wlan_done=%u wlan_rx=%u "
		   "wlan_rxdata=%u wlan_rxenc=%u wlan_txplain=%u wlan_txerr=%u "
		   "wlan_attach=%d wlan_freq=%u\n",
		   tag, wc.data_tx, wc.tx_completes, wc.rx_frames, wc.rx_data,
		   wc.rx_data_enc, wc.data_tx_plain, wc.tx_errors, wc.attached,
		   (unsigned)wc.tuned_freq);
	MDK_ANCHOR("netio: layers tag=%s wlan_gated=%u wlan_wakes=%u "
		   "wlan_stash=%u wlan_stashok=%u wlan_xdrop=%u "
		   "wlan_qhi=%u wlan_pending=%u mem_err=%lu pbuf_err=%lu\n",
		   tag, wc.tx_gated, wc.tx_wakes,
		   wc.tx_stash, wc.tx_stash_ok, wc.tx_perm_drop, wc.txq_high,
		   wc.tx_pending, mem_err, pbuf_err);
	/*
	 * The block-ack fingerprint, one grep-able record per cell: whether the
	 * driver armed a TX session (ba_tx), whether the AP answered
	 * (ba_oper), and how many sessions ended (ba_stop).  The same four
	 * fields are in the kernel module's stats parameter, so the two ports'
	 * ledgers are read the same way.
	 */
	MDK_ANCHOR("netio: ba tag=%s ba_rx=%u ba_tx=%u ba_oper=%u ba_stop=%u\n",
		   tag, wc.ba_rx_start, wc.ba_tx_start, wc.ba_tx_operational,
		   wc.ba_stop);
	/*
	 * The transmit handoff's own census, between the socket buffer and the
	 * controller: `q` is what the txq engine still holds (frames handed down
	 * that no driver dequeue took), and `stopped` is the driver's transmit
	 * gate.  A cell whose pool fills and whose USB shows nothing in flight
	 * has its frames in exactly one of these, and the answer decides the
	 * fix: a stop without a wake, or a drain the driver never runs.
	 */
	emit_tx_census(tag);
	/* The lost-interrupt fallback's own evidence: `polls` is how often the
	 * drain ran the controller's completion path because a transfer was
	 * overdue, `drained` how many completions that path delivered.  A
	 * nonzero drained on a cell that previously timed out is the direct
	 * proof the fix recovered a lost EHCI completion. */
	emit_usb(tag);
	/* The bridge's own receive decode: per-rate frames, retries and FCS
	 * failures since the last dump.  On the air this is the rate/retry
	 * fingerprint the AP's station dump cannot give (the dump reports the
	 * rates the AP chose, not what was received). */
	wlan_bridge_link_dump();
#if LWIP_STATS
	/* The two stops between the 802.11 seam and a UDP socket: `ip_in` counts
	 * frames that reached ip4_input at all (a frame that never did was
	 * dropped by ethernet_input - wrong destination or ethertype), and
	 * `link_drop` is the link layer's own refusal count.  Read beside
	 * `udp_in` they separate "the stack never saw it" from "the stack saw it
	 * and had no socket". */
	ip_in = (unsigned long)lwip_stats.ip.recv;
	link_drop = (unsigned long)lwip_stats.link.drop;
#endif
	MDK_ANCHOR("netio: mib tag=%s udp_in=%lu udp_inerr=%lu ip_drop=%lu "
		   "tcp_retrans=%lu ip_in=%lu link_drop=%lu\n",
		   tag, udp_in, udp_inerr, ip_drop, tcp_retrans, ip_in,
		   link_drop);
	/* The socket-buffer supply is the data path's real limit: a refusal is
	 * backpressure, and a live count that only ever climbs is a leak. The
	 * two are told apart here, at every anchor, because a storm of refusal
	 * lines is exactly what those anchors exist to avoid. */
	emit_pool(tag);
#endif
	emit_cpu_spot(tag);
}

static void init_buffer(uint8_t *buf, int32_t size)
{
	uint32_t i;
	uint8_t ch;

	buf[0] = 0;
	for (i = 1; i < size; i++) {
		do
			ch = (uint8_t)rand32();
		while (ch == 0);
		buf[i] = ch;
	}
}

static int32_t recv_data(int32_t sock, void *buf, int32_t size)
{
	int32_t rc = recv(sock, buf, size, 0);

	if (rc < 0)
		return rc;
	if (rc != size)
		return BSD_ERROR;
	return 0;
}

static int32_t send_data(int32_t sock, const void *buf, int32_t size)
{
	int32_t rc = send(sock, buf, size, 0);

	if (rc < 0)
		return rc;
	if (rc != size)
		return BSD_ERROR;
	return 0;
}

/* Arm's thread_exit: record where a thread died, print it, end the thread.
 * On this board the console sentence is the evidence. */
static void thread_exit(int32_t line, int32_t bsd_error)
{
	io.err_line = line;
	io.err_bsd = bsd_error;
	MDK_ANCHOR("netio: error line=%ld code=%ld\n",
		   (long)line, (long)bsd_error);
	osThreadExit();
}

#define __THREAD_EXIT(err) thread_exit(__LINE__, (err))

/* Thread attributes are written out rather than passed as NULL: CMSIS-RTOS2
 * then takes defaults, and on this image a default-priority thread was what
 * stopped the scheduler. Explicit stack and priority, always. */
/* Above the control threads, not below them: at 1440-byte datagrams the
 * receive path is a continuous interrupt storm, and a below-normal reader was
 * never scheduled once in six seconds - the mailbox filled to its 16-slot
 * ceiling and every further datagram was dropped before the reader saw one.
 * Measured (2026-10-29): iters=0 over the whole C2S window with
 * osPriorityBelowNormal. */
/* Receive-thread priority. A lever, not a preference: the cost instrument
 * measures the producer's feed span with the wall clock, so a consumer that
 * preempts it (the UDP receiver runs at AboveNormal) has its own processing
 * charged to the producer's feed bucket. Raising the producer above the
 * consumer is the discriminating experiment for that contamination - and,
 * if it also raises throughput, a fix in its own right. */
#ifndef MN_RX_PRIO
#define MN_RX_PRIO osPriorityNormal
#endif
/* Consumer priority (K2-03 H2): one lever down from the producer to see how
 * much of the feed bucket is the consumer preempting the drain thread. */
#ifndef UDP_RX_PRIO
#define UDP_RX_PRIO osPriorityAboveNormal
#endif
/* How long a UDP transmit cell waits through WLAN netif backpressure before it
 * treats the refusal as an abort.  The bridge gates the driver's transmit queue
 * while it is full and wakes it on the next completion (milliseconds), so a
 * non-blocking `sendto` blast sees ERR_MEM and must wait for the wake the way
 * TCP's window would make it wait.  A refusal that outlives this window is the
 * stall class the cell reports.  The wired arm's 256-slot ring never refuses,
 * and it is compiled without the retry so its bytes stay the measured bytes. */
#ifndef S2C_REFUSAL_MAX
#define S2C_REFUSAL_MAX 200
#endif

static const osThreadAttr_t udp_rx_attr = {
	.name = "netio-udp-rx",
	.priority = UDP_RX_PRIO,
	.stack_size = 4096U,
};

/* UDP receiver thread: drains the UDP data socket into the shared buffer.
 * Spawned only after the client asked for a C2S run (Arm's shape).
 *
 * Polled, not blocked-on-socket: an lwIP socket close with a thread parked in
 * its receive mbox takes the kernel down. lwIP's sys_mbox_free deletes the
 * queue unconditionally and FreeRTOS asserts on a queue that still has tasks
 * waiting on it (queue.c:2265, configASSERT(listLIST_IS_EMPTY(
 * &pxQueue->xTasksWaitingToReceive))). So this thread is always *not*
 * blocked in lwip_recv when the control thread decides to stop it: it polls a
 * short receive timeout, and the control thread clears io_rx_stop first and
 * waits one poll interval before touching the socket. */
static volatile int io_rx_stop;

/* Datagrams this receiver has consumed since boot. The cost decomposition
 * needs a cumulative denominator: a cell is two snapshots apart, exactly like
 * the layered loss counters, so a phase cannot inherit the previous phase's
 * frames. */
static uint32_t io_rx_total;
static uint32_t io_empty_polls;
#define APP_SAMPLE_PERIOD 4096u
static uint32_t io_app_blocked;
static uint32_t io_app_charged;

/* ---------------------------------------------------------------------------
 * K2-07 raw-API receive path (CONFIG_NETIO_RAW_UDP).
 *
 * The socket data plane is udp_input -> recv_udp() -> recvmbox (16 deep) ->
 * a second thread's recvfrom() -> a 1440-byte copy. The middle is an
 * invisible loss point: recv_udp()'s mbox-full branch frees the pbuf and
 * returns WITHOUT touching any lwIP counter (api_msg.c, both refusal
 * branches), so a socket-side drop cannot be seen in the ledger at all -
 * exactly the hole the 2026-10-30 prio arm fell into (498k frames fed,
 * zero counters moved, app saw 0). The raw path removes the whole middle:
 * the PCB's recv callback runs on the RX thread's input call and only
 * counts, remembers the peer and frees the pbuf. Nothing queues, nothing
 * copies.
 * ------------------------------------------------------------------------- */
#if defined(CONFIG_NETIO_RAW_UDP) && CONFIG_NETIO_RAW_UDP
static struct udp_pcb *g_raw_pcb;
static volatile int g_raw_in_window;
static ip_addr_t g_raw_peer;
static uint16_t g_raw_peer_port;
static int g_raw_peer_valid;

static void raw_udp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
			 const ip_addr_t *addr, u16_t port)
{
	(void)arg;
	(void)pcb;

	if (p == NULL) {
		return;
	}
	if (g_raw_in_window) {
		io.pkt_count++;
		io.rx_bytes += p->tot_len;
		io_rx_total++;
		if (!g_raw_peer_valid && addr != NULL) {
			ip_addr_copy(g_raw_peer, *addr);
			g_raw_peer_port = port;
			g_raw_peer_valid = 1;
		}
	}
	pbuf_free(p);
}

/* One S2C datagram out of the raw PCB: allocate, copy, send, free. */
static int32_t raw_udp_send(const void *buf, uint32_t len)
{
	struct pbuf *p;
	ip_addr_t dst;
	err_t err;

	p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
	if (p == NULL) {
		return -1;
	}
	if (pbuf_take(p, buf, (u16_t)len) != ERR_OK) {
		pbuf_free(p);
		return -1;
	}
	ip_addr_set_ip4_u32(&dst, io.client->sin_addr.s_addr);
	err = udp_sendto(g_raw_pcb, p, &dst, NETIO_AUXPORT);
	pbuf_free(p);
	return (err == ERR_OK) ? (int32_t)len : -1;
}
#endif /* CONFIG_NETIO_RAW_UDP */

#if NETIO_GMAC
/* ---------------------------------------------------------------------------
 * K2-07 receive window: every counter that can swallow a frame, diffed over
 * the C2S cell so boot, ARP and the control plane cannot leak into the ledger.
 * `drops` is the strong verdict: a clean receive window has it at zero.
 * ------------------------------------------------------------------------- */
struct rx_win {
	unsigned long mmc_ovf;
	unsigned long rx_ru;
	unsigned long rx_dropped;
	unsigned long rx_frames;
	unsigned long fed;
	unsigned long udp_drop;
	unsigned long ip_drop;
	unsigned long link_drop;
	unsigned long mem_err;
	unsigned long pbuf_err;
};

static struct rx_win g_win_a;

static void rx_win_take(struct rx_win *w)
{
	struct dwc_eqos_stats ds;
	struct dwc_eqos_mmc_stats mmc;
	unsigned long polls = 0, pool_fail = 0, input_fail = 0, fed = 0;

	memset(w, 0, sizeof(*w));
	memset(&ds, 0, sizeof(ds));
	memset(&mmc, 0, sizeof(mmc));
	dwc_eqos_get_stats(MN_GMAC, &ds);
	dwc_eqos_get_mmc_stats(MN_GMAC, &mmc);
	ethnetif_get_diag(&polls, &pool_fail, &input_fail, &fed);

	w->mmc_ovf = (unsigned long)mmc.rx_fifo_overflow;
	w->rx_ru = (unsigned long)ds.rx_buf_unavail;
	w->rx_dropped = (unsigned long)ds.rx_dropped;
	w->rx_frames = (unsigned long)ds.rx_frames;
	w->fed = fed;
#if LWIP_STATS
	w->udp_drop = (unsigned long)lwip_stats.udp.drop;
	w->ip_drop = (unsigned long)lwip_stats.ip.drop;
	w->link_drop = (unsigned long)lwip_stats.link.drop;
	w->mem_err = (unsigned long)lwip_stats.mem.err;
#endif
#if LWIP_STATS && MEMP_STATS
	if (lwip_stats.memp[MEMP_PBUF] != NULL) {
		w->pbuf_err = (unsigned long)lwip_stats.memp[MEMP_PBUF]->err;
	}
#endif
}

static void rx_win_report(const char *tag, const struct rx_win *a,
			  const struct rx_win *b)
{
	unsigned long drops;

	drops = (b->mmc_ovf - a->mmc_ovf) + (b->rx_ru - a->rx_ru) +
		(b->rx_dropped - a->rx_dropped) + (b->udp_drop - a->udp_drop) +
		(b->ip_drop - a->ip_drop) + (b->link_drop - a->link_drop) +
		(b->mem_err - a->mem_err) + (b->pbuf_err - a->pbuf_err);
	MDK_ANCHOR("netio: win tag=%s pkts=%lu rx_frames=%lu fed=%lu drops=%lu "
		   "mmc_ovf=%lu rx_ru=%lu rx_dropped=%lu udp_drop=%lu ip_drop=%lu "
		   "link_drop=%lu mem_err=%lu pbuf_err=%lu\n",
		   tag, (unsigned long)io.pkt_count,
		   b->rx_frames - a->rx_frames, b->fed - a->fed, drops,
		   b->mmc_ovf - a->mmc_ovf, b->rx_ru - a->rx_ru,
		   b->rx_dropped - a->rx_dropped, b->udp_drop - a->udp_drop,
		   b->ip_drop - a->ip_drop, b->link_drop - a->link_drop,
		   b->mem_err - a->mem_err, b->pbuf_err - a->pbuf_err);
}
#endif /* NETIO_GMAC */

static void UDP_Receiver(void *arg)
{
	int32_t rc;

	(void)arg;
	MDK_ANCHOR("netio: udp recv start\n");

	while (!io_rx_stop) {
#if defined(CONFIG_GMAC_PROF) && CONFIG_GMAC_PROF
		uint64_t t_recv = gtimer_count_get();
#endif

		/* Two-step receive, and the reason matters more than the code.
		 *
		 * The application bucket must be CPU cost, so it must not
		 * include time spent waiting for the next datagram. But the
		 * waiting itself is part of the vehicle: a pure polling loop
		 * with a one-tick sleep changed the measured system (throughput
		 * fell from 53 to 22 MB/s), which disqualifies the numbers.
		 *
		 * So: ask the socket whether a datagram is already queued.
		 * If yes, the receive cannot block and is charged. If not,
		 * block exactly as the original code did - that call is
		 * counted but charged zero, because its duration is the wait. */
#if defined(CONFIG_GMAC_PROF) && CONFIG_GMAC_PROF
		{
			int avail = 0;

			(void)lwip_ioctl(io.udp_sock, FIONREAD, &avail);
			if (avail > 0) {
				/* A datagram is already queued, so this receive
				 * cannot block: its whole duration is work and
				 * belongs to the application bucket. */
				rc = recv(io.udp_sock, (char *)io.buf,
					  io.pkt_size, 0);
				if (rc > 0) {
					net_prof_app_charge(gtimer_count_get() -
							    t_recv);
					io_app_charged++;
				}
			} else {
				rc = recv(io.udp_sock, (char *)io.buf,
					  io.pkt_size, 0);
				if (rc > 0) {
					/* Woke up with data: the duration is
					 * the wait, not the work. Counted, not
					 * charged. */
					io_app_blocked++;
				} else {
					io_empty_polls++;
					osDelay(1u);
					continue;
				}
			}
		}
#else
		/* Instrument compiled out: the receive is the plain one.
		 * There is nothing to charge, and the FIONREAD step exists
		 * only to tell work from waiting, so the A/B compares two
		 * systems that differ in the instrument alone. */
		rc = recv(io.udp_sock, (char *)io.buf, io.pkt_size, 0);
#endif
		if (rc <= 0)
			continue;
		io.rx_bytes += (uint32_t)rc;
		io.pkt_count++;
		io_rx_total++;

		/* Sample window for the application bucket. In steady state the
		 * consumer is faster than the producer, so every receive finds
		 * an empty queue, blocks, and its duration is the wait rather
		 * than the work - the bucket would read zero. Pausing for two
		 * ticks every few thousand frames lets a small backlog form, so
		 * the next receives find data already queued and their (purely
		 * working) duration can be charged. The perturbation is ~1% of
		 * the cell and is identical in every arm, which is what makes
		 * the number comparable; the app_empty count still reports how
		 * often the queue really was empty. */
		if ((io_rx_total % APP_SAMPLE_PERIOD) == 0u)
			osDelay(2u);
	}
	MDK_ANCHOR("netio: udp recv end pkts=%lu\n", (unsigned long)io.pkt_count);
	osThreadExit();
}

/* Ask the receiver to stop and let it leave lwip_recv on its own. */
#if !NETIO_GMAC
/* The in-cell sampler.  The BA-broken receive path backs the AP's queue up far
 * enough that the Pi client blocks in sendmmsg and never sends CMD_RES, so the
 * cell never closes and a begin/end pair would only ever show its begin half.
 * A 2 s timer samples the same ledger while the window is open; it reads the
 * substitute's counters only (no controller registers), so sampling cannot
 * disturb the thing it watches. */
static osTimerId_t rxpath_timer;

static void rxpath_tick(void *arg)
{
	(void)arg;
	emit_rxpath("cell");
	/* The cell's own timeline.  A stall under load prints nothing (the
	 * traffic path only speaks on events), so silence cannot tell "the
	 * system died" from "nothing to report".  This pair is the cadence:
	 * while it keeps coming the scheduler and this timer are alive, and
	 * the counters it carries show which side froze first - completions,
	 * the receive boundary, or the transmit leg. */
	emit_layer_counters("cell");
}

/*
 * The boot's own heartbeat.  The 2 s cell cadence above only runs while a cell
 * is open, so the state *after* a cell aborts is exactly the state nobody was
 * sampling - and that is where the socket-buffer pool stayed full for half a
 * minute with no further ledger at all.  This samples three lines every 5 s
 * for the whole boot, starting when the server does: the pool, the txq engine
 * and the controller.  Short lines only, so it survives a console that is
 * being flooded.
 */
static osTimerId_t hb_timer;

/*
 * The stall experiment.  When the data plane stops moving (the bridge's own
 * receive counter unchanged for two heartbeats) ask the one question that the
 * counters cannot: is the device still answering on its control pipe?  Then
 * restart the controller's asynchronous schedule once, without touching the
 * queue heads on it:
 *
 *   EP0 answers, bulk is stuck -> the device's bulk endpoint, nothing host-side
 *   EP0 dead, sibling device ok -> the device's whole USB interface is gone
 *   EP0 dead, sibling dead      -> this controller's completion path is gone
 *   completions resume          -> the port had stopped retiring finished transfers
 *   nothing moves               -> the transfer is still with the device (it is NAKing)
 *
 * One shot per boot, and every marker is printed before the action it names, so
 * a console that dies inside the probe still says which action it died in.  The
 * probe can block for up to ~1.5 s per transfer (the stack retries three times
 * at CONFIG_USBHOST_CONTROL_TRANSFER_TIMEOUT), so the next heartbeat arriving
 * late is expected and is not a symptom.
 */
static unsigned int hb_last_rx;
static unsigned int hb_rx_flat;
static unsigned int hb_rearm_done;

/* The endpoint probe's console face: one line per transfer, because the verdict
 * is read off which of them answered.  Field names are the ledger's. */
static void hb_ep0_probe(unsigned int rx_now)
{
	struct lkpi_usb_chusb_probe p;
	int rc;

	memset(&p, 0, sizeof(p));
	MDK_ANCHOR("netio: probe tag=ep0 begin rx=%u\n", rx_now);
	rc = lkpi_usb_cherryusb_ep0_probe(&p);
	if (rc != 0) {
		MDK_ANCHOR("netio: probe tag=ep0 not-run rc=%d\n", rc);
		return;
	}
	MDK_ANCHOR("netio: probe tag=ep0 present=%d bus=%u addr=%u "
		   "desc_rc=%d len=%u ms=%u id=%04x:%04x\n",
		   p.present, (unsigned)p.busid, (unsigned)p.dev_addr,
		   p.desc_rc, p.desc_len, p.desc_ms,
		   p.desc_len >= 12u ? (unsigned)(p.desc[8] | (p.desc[9] << 8)) : 0u,
		   p.desc_len >= 12u ? (unsigned)(p.desc[10] | (p.desc[11] << 8)) : 0u);
	MDK_ANCHOR("netio: probe tag=ep0 status_rc=%d len=%u ms=%u word=%04x\n",
		   p.status_rc, p.status_len, p.status_ms, p.status_word);
	if (p.other_rc == LKPI_USB_PROBE_UNAVAILABLE)
		MDK_ANCHOR("netio: probe tag=ep0 other devs=%d rc=unavailable\n",
			   p.other_count);
	else
		MDK_ANCHOR("netio: probe tag=ep0 other devs=%d rc=%d len=%u "
			   "ms=%u id=%04x:%04x\n",
			   p.other_count, p.other_rc, p.other_len, p.other_ms,
			   (unsigned)((p.other_id >> 16) & 0xffffu),
			   (unsigned)(p.other_id & 0xffffu));
	MDK_ANCHOR("netio: probe tag=ep0 done\n");
}

/*
 * A flat receive counter is only a stall if the plane ever moved.  The first
 * board arm of W7-06 armed the probe on an idle image: no load was running, so
 * `rx` had been 2 since join, two heartbeats later it was "flat", and the
 * probe fired before any bulk traffic had ever been submitted.  The threshold
 * below is what tells "this plane has carried traffic and stopped" from "this
 * plane has not started yet" - a stall is a transition, not a value.
 */
#define HB_STALL_FLOOR_FRAMES	256u

static void hb_rearm_probe(unsigned int rx_now)
{
	struct lkpi_usb_engine_stats es;

	if (hb_rearm_done || rx_now < HB_STALL_FLOOR_FRAMES)
		return;
	if (rx_now == hb_last_rx) {
		hb_rx_flat++;
	} else {
		hb_rx_flat = 0;
		hb_last_rx = rx_now;
	}
	if (hb_rx_flat < 2)
		return;

	hb_rearm_done = 1;
	memset(&es, 0, sizeof(es));
	lkpi_usb_engine_stats_get(&es);
	MDK_ANCHOR("netio: rearm tag=hb why=rx_flat rx=%u done=%lu in_flight=%u\n",
		   rx_now, (unsigned long)es.completions, es.in_flight);
	lkpi_usb_controller_report();
	hb_ep0_probe(rx_now);
	{
		int n = lkpi_usb_controller_rearm();

		memset(&es, 0, sizeof(es));
		lkpi_usb_engine_stats_get(&es);
		MDK_ANCHOR("netio: rearm tag=hb rearmed=%d done=%lu\n", n,
			   (unsigned long)es.completions);
	}
}

static void hb_tick(void *arg)
{
	uint32_t rx_in = 0, rx_ref = 0, rx_drop = 0, rx_frames = 0, rx_q = 0;
	struct lkpi_usb_engine_stats es;
	struct wlan_bridge_counters wc;

	(void)arg;
	memset(&es, 0, sizeof(es));
	memset(&wc, 0, sizeof(wc));
	lkpi_usb_engine_stats_get(&es);
	lkpi80211_rx_boundary_counters(&rx_in, &rx_ref, &rx_drop, &rx_frames,
				       &rx_q);
	wlan_bridge_stats(&wc);
	/*
	 * The escape hatch, and the FIRST thing this heartbeat does: written
	 * straight to the polled UART with interrupts masked, past the log
	 * ring and its lock.  If the normal lines stop but this one keeps
	 * coming, the console (or the log path) died and the system did not;
	 * if it stops too, the timer never ran.  Its position matters: a
	 * heartbeat that dies inside the log path must still have left the
	 * proof that it fired.
	 */
	{
		struct lkpi_skb_pool_stats ps;
		unsigned long q = 0, handed = 0, orph = 0, refu = 0;
		uint32_t stopped = 0, wakes = 0;

		memset(&ps, 0, sizeof(ps));
		lkpi_skb_pool_stats(&ps);
		lkpi_txq_census(&q, &handed, &orph, &refu);
		lkpi80211_tx_gate_counters(&stopped, &wakes);
		board_log_emergency("netio: alive rx_in=%lu rxdata=%u "
				    "usbc=%u usbd=%u txq=%lu txf=%u "
				    "hdr=%lu/%lu af=%lu t=%lu ph=%u "
				    "idleq=%u\n",
				    (unsigned long)rx_in, wc.rx_data,
				    (unsigned)es.completions,
				    (unsigned)es.done_pending, q,
				    (unsigned)es.in_flight,
				    (unsigned long)ps.skb_used,
				    (unsigned long)ps.hdr_total,
				    (unsigned long)ps.alloc_fail,
				    (unsigned long)refu,
				    lkpi_usb_drain_phase(), es.idle_queued);
	}
	emit_pool("hb");
	emit_tx_census("hb");
	emit_usb("hb");
	/* The same boundary ledgers the cell prints, on every heartbeat.  A
	 * stall that starts after a cell's window closed leaves its cause
	 * (frames entering lkpi80211 and the queue's own drops, the done
	 * handoff's fill/listing state) unwritten until the next boot; these
	 * two lines are what makes the stall's onset visible in the same
	 * console capture as its symptom. */
	MDK_ANCHOR("netio: rxconn tag=hb rx_in=%lu rx_ref=%lu "
		   "rx_drop=%lu rx_frames=%lu rx_q=%lu\n",
		   (unsigned long)rx_in, (unsigned long)rx_ref,
		   (unsigned long)rx_drop, (unsigned long)rx_frames,
		   (unsigned long)rx_q);
	MDK_ANCHOR("netio: usbeng tag=hb dph=%u phase=%u idleq=%u "
		   "recov=%u idlekick=%u done=%u\n",
		   es.done_pending, lkpi_usb_drain_phase(),
		   es.idle_queued, es.recovered,
		   es.idle_kicked, es.completions);
	/* The bridge's own four counters, short enough to ride the heartbeat.
	 * They separate the two directions at the layer that owns both: a
	 * downlink that arrives but never leaves again shows rx climbing with tx
	 * flat, and a downlink that never arrives shows rx flat while the
	 * controller's own URBs keep completing. */
	MDK_ANCHOR("netio: wlan tag=hb tx=%u done=%u rx=%u rxdata=%u "
		   "pend=%u q=%u\n",
		   wc.data_tx, wc.tx_completes, wc.rx_frames,
		   wc.rx_data, wc.tx_pending, wc.txq_high);
	hb_rearm_probe(wc.rx_data);
}

static void hb_start(void)
{
	if (hb_timer == NULL)
		hb_timer = osTimerNew(hb_tick, osTimerPeriodic, NULL, NULL);
	if (hb_timer != NULL)
		osTimerStart(hb_timer, 5000u);
}

static void rxpath_window(int open)
{
#if defined(CONFIG_NETIO_RXPATH) && CONFIG_NETIO_RXPATH
	if (rxpath_timer == NULL)
		rxpath_timer = osTimerNew(rxpath_tick, osTimerPeriodic, NULL,
					  NULL);
	if (rxpath_timer == NULL)
		return;
	if (open)
		osTimerStart(rxpath_timer, 2000u);
	else
		osTimerStop(rxpath_timer);
#else
	(void)open;
#endif
}
#else
#define rxpath_window(open)	do { (void)(open); } while (0)
#endif

static void udp_rx_quiesce(void)
{
	io_rx_stop = 1;
	osDelay(5u);
	io_rx_stop = 0;
}

/* One measurement cell: the C2S window opens when the receiver thread is
 * created and closes when the client's RES arrives (after the receiver has
 * been quiesced), so the wall clock and every bucket cover exactly the same
 * frames. */
#if NETIO_GMAC
static struct net_prof_snap prof_a;
#endif
static uint64_t prof_t0;
/* The same cell's CPU cycle count at the open, so the report can say how many
 * cycles one frame cost and what the core's effective clock was (P0.2). */
static uint64_t prof_c0;

/* --- K2-16 transmit-stall probe -------------------------------------------
 *
 * The high-OPP uplink stops with a full ring and no completion. A single
 * give-up snapshot cannot say whether the DMA stopped fetching, the MAC
 * stopped draining, the write-back is being lost, or a write never landed;
 * that needs the same registers over time. The console cannot carry a 1 ms
 * stream (115200 baud is ~11.5 kB/s, one 80-byte line is 7 ms), so samples
 * land in RAM and are dumped after the failure, while the cell waits out its
 * window.
 *
 * Two windows, because the stall happens inside the driver's full-ring wait:
 *  - pre: sampled at frame boundaries from the send loop, at most one per ms,
 *    so the last ~128 ms of healthy traffic before the ring filled is kept;
 *  - post: sampled here in the abort path (the sender is idle, so a real
 *    1 ms cadence), which is the window where "does it ever recover" is
 *    answered - including one explicit tail-pointer write.
 *
 * Tax: one 24 MHz counter read per sent frame (the gate) plus one sample per
 * ms (~10 register reads). Label the arm instrumented; its throughput is
 * not the acceptance number. */
#if NETIO_GMAC && defined(CONFIG_NETIO_TX_STALL_PROBE) && CONFIG_NETIO_TX_STALL_PROBE
#define STALL_PRE_SAMPLES	128
#define STALL_POST_SAMPLES	96
#define STALL_GATE_TICKS	24000u	/* 1 ms at 24 MHz */

static struct dwc_eqos_tx_sample stall_pre[STALL_PRE_SAMPLES];
static struct dwc_eqos_tx_sample stall_post[STALL_POST_SAMPLES];
static unsigned stall_pre_head;
static unsigned stall_pre_count;
static volatile int stall_armed;
static volatile int stall_frozen;
static uint32_t stall_last_gt;

static void stall_begin(void)
{
	stall_pre_head = 0u;
	stall_pre_count = 0u;
	stall_frozen = 0;
	stall_last_gt = (uint32_t)gtimer_count_get() - STALL_GATE_TICKS;
	stall_armed = 1;
}

static void stall_end(void)
{
	stall_armed = 0;
}

static void stall_tick(void)
{
	uint32_t now;

	if ((stall_armed == 0) || (stall_frozen != 0)) {
		return;
	}
	now = (uint32_t)gtimer_count_get();
	if ((uint32_t)(now - stall_last_gt) < STALL_GATE_TICKS) {
		return;
	}
	stall_last_gt = now;
	if (dwc_eqos_tx_sample(MN_GMAC, &stall_pre[stall_pre_head]) != 0) {
		return;
	}
	stall_pre_head = (stall_pre_head + 1u) % STALL_PRE_SAMPLES;
	if (stall_pre_count < STALL_PRE_SAMPLES) {
		stall_pre_count++;
	}
}

static void stall_print(const char *tag, const struct dwc_eqos_tx_sample *s)
{
	MDK_ANCHOR("netio: txp %s t=%lu head=%lu tail=%lu cur=%08lx end=%08lx "
		   "mtl=%08lx irq=%lu irqtx=%lu spins=%lu zero=%lu freed=%lu\n",
		   tag, (unsigned long)s->tick, (unsigned long)s->head,
		   (unsigned long)s->tail, (unsigned long)s->cur_tx_desc,
		   (unsigned long)s->tx_end, (unsigned long)s->mtl_txq_dbg,
		   (unsigned long)s->irq_n, (unsigned long)s->irq_tx,
		   (unsigned long)s->spins, (unsigned long)s->zero,
		   (unsigned long)s->freed);
}

/* The abort path. Order matters: freeze the pre-ring, snapshot ownership,
 * kick once, then watch the channel for ~96 ms before dumping everything. */
static void stall_capture(void)
{
	uint32_t own[8];
	uint32_t mmc[24];
	uint32_t kick;
	unsigned i;

	stall_frozen = 1;

	memset(own, 0, sizeof(own));
	memset(mmc, 0, sizeof(mmc));
	(void)dwc_eqos_tx_scan_own(MN_GMAC, own, 8);
	(void)dwc_eqos_mmc_raw(MN_GMAC, 0x700u, mmc, 24);

	kick = dwc_eqos_tx_tail_kick(MN_GMAC);

	for (i = 0u; i < STALL_POST_SAMPLES; i++) {
		if (dwc_eqos_tx_sample(MN_GMAC, &stall_post[i]) != 0) {
			break;
		}
		osDelay(1u);
	}

	MDK_ANCHOR("netio: txown n=%u own=%08lx.%08lx.%08lx.%08lx"
		   ".%08lx.%08lx.%08lx.%08lx kick=%08lx pre=%u post=%u\n",
		   (unsigned)((DWC_EQOS_TX_DESC_COUNT + 31u) / 32u),
		   (unsigned long)own[0], (unsigned long)own[1],
		   (unsigned long)own[2], (unsigned long)own[3],
		   (unsigned long)own[4], (unsigned long)own[5],
		   (unsigned long)own[6], (unsigned long)own[7],
		   (unsigned long)kick, stall_pre_count, i);
	MDK_ANCHOR("netio: txmmc base=700 %08lx %08lx %08lx %08lx %08lx %08lx "
		   "%08lx %08lx %08lx %08lx %08lx %08lx\n",
		   (unsigned long)mmc[0], (unsigned long)mmc[1],
		   (unsigned long)mmc[2], (unsigned long)mmc[3],
		   (unsigned long)mmc[4], (unsigned long)mmc[5],
		   (unsigned long)mmc[6], (unsigned long)mmc[7],
		   (unsigned long)mmc[8], (unsigned long)mmc[9],
		   (unsigned long)mmc[10], (unsigned long)mmc[11]);
	MDK_ANCHOR("netio: txmmc base=730 %08lx %08lx %08lx %08lx %08lx %08lx "
		   "%08lx %08lx %08lx %08lx %08lx %08lx\n",
		   (unsigned long)mmc[12], (unsigned long)mmc[13],
		   (unsigned long)mmc[14], (unsigned long)mmc[15],
		   (unsigned long)mmc[16], (unsigned long)mmc[17],
		   (unsigned long)mmc[18], (unsigned long)mmc[19],
		   (unsigned long)mmc[20], (unsigned long)mmc[21],
		   (unsigned long)mmc[22], (unsigned long)mmc[23]);

	for (i = 0u; i < stall_pre_count; i++) {
		unsigned idx = (stall_pre_head + STALL_PRE_SAMPLES -
				stall_pre_count + i) % STALL_PRE_SAMPLES;

		stall_print("pre", &stall_pre[idx]);
	}
	for (i = 0u; i < STALL_POST_SAMPLES; i++) {
		stall_print("post", &stall_post[i]);
	}
}
#else
#define stall_begin()		do { } while (0)
#define stall_end()		do { } while (0)
#define stall_tick()		do { } while (0)
#define stall_capture()		do { } while (0)
#endif /* CONFIG_NETIO_TX_STALL_PROBE */

static void UDP_Server(void *arg)
{
	struct sockaddr_in sa;
	CONTROL ctl;
	int32_t sock, csock, rc;
	socklen_t sa_len;
	int32_t nb;
	struct timeval tv;
	fd_set fds;

	(void)arg;
	MDK_ANCHOR("netio: udp server start\n");

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		__THREAD_EXIT(sock);

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(NETIO_AUXPORT);
	sa.sin_addr.s_addr = INADDR_ANY;
	rc = bind(sock, (struct sockaddr *)&sa, sizeof(sa));
	if (rc < 0)
		__THREAD_EXIT(rc);

	rc = listen(sock, 1);
	if (rc < 0)
		__THREAD_EXIT(rc);

	MDK_ANCHOR("netio: udp listening port=%d\n", NETIO_AUXPORT);
	hb_start();

	for (;;) {
		FD_ZERO(&fds);
		FD_SET(sock, &fds);
		tv.tv_sec = 120;
		tv.tv_usec = 0;

		rc = select(sock + 1, &fds, NULL, NULL, &tv);
		if (rc < 0)
			__THREAD_EXIT(rc);
		if ((rc == 0) || (FD_ISSET(sock, &fds) == 0))
			continue;

		sa_len = sizeof(*io.client);
		csock = accept(sock, (struct sockaddr *)io.client, &sa_len);
		if (csock < 0)
			continue;

		MDK_ANCHOR("netio: udp client connected\n");

		for (;;) {
			rc = recv_data(csock, &ctl, sizeof(ctl));
			if (rc < 0) {
#if defined(CONFIG_NETIO_RXPATH) && CONFIG_NETIO_RXPATH
				MDK_ANCHOR("netio: ctl recv rc=%d\n", (int)rc);
#endif
				__THREAD_EXIT(rc);
			}

			io.command = ntohl(ctl.cmd);
#if defined(CONFIG_NETIO_RXPATH) && CONFIG_NETIO_RXPATH
			/* Control-plane ledger: the cell's close is a TCP command,
			 * and a receive path that blocks the client can make the
			 * thread look dead when the command never arrived. */
			MDK_ANCHOR("netio: ctl cmd=%08lx data=%lu\n",
				   (unsigned long)io.command,
				   (unsigned long)ntohl(ctl.data));
#endif

			if (io.command == CMD_C2S) {
				io.pkt_size = ntohl(ctl.data);
				io.pkt_count = 0;
				io.rx_bytes = 0;
				MDK_ANCHOR("netio: udp c2s pkt=%lu\n",
					   (unsigned long)io.pkt_size);

				if ((int32_t)io.pkt_size > (int32_t)io.buf_size)
					__THREAD_EXIT(BSD_EMSGSIZE);

#if !(defined(CONFIG_NETIO_RAW_UDP) && CONFIG_NETIO_RAW_UDP)
				rc = socket(PF_INET, SOCK_DGRAM, 0);
				if (rc < 0)
					__THREAD_EXIT(rc);
				io.udp_sock = rc;

				/* Bound to the board's own address, not ANY: with
				 * ANY, lwIP matched the client's 1440-byte data
				 * datagrams against this unconnected socket and
				 * delivered them ahead of the control connection's
				 * data stream, so both streams interleaved and the
				 * C2S window counted zero packets. */
				memset(&sa, 0, sizeof(sa));
				sa.sin_family = AF_INET;
				sa.sin_port = htons(NETIO_AUXPORT);
				sa.sin_addr.s_addr = g_local_ip;
				rc = bind(io.udp_sock, (struct sockaddr *)&sa,
					  sizeof(sa));
				if (rc < 0)
					__THREAD_EXIT(rc);
				{
					struct timeval rtv;
					rtv.tv_sec = 0;
					rtv.tv_usec = 50000;
					(void)setsockopt(io.udp_sock, SOL_SOCKET,
							 SO_RCVTIMEO, &rtv,
							 sizeof(rtv));
				}
#else
				io.udp_sock = -1;
#endif

				/* Cell opens here. The application bucket is
				 * zeroed first so the snapshot that starts
				 * the cell carries a zero for it and the delta
				 * at the far end is the cell's own cost. */
#if NETIO_GMAC
				net_prof_bind(MN_GMAC);
				net_prof_app_reset();
				net_prof_take(&prof_a);
				prof_a.frames = io_rx_total;
				prof_a.app_empty = io_empty_polls + io_app_blocked;
				prof_a.app_charged = io_app_charged;
				rx_win_take(&g_win_a);
#endif
				prof_t0 = gtimer_count_get();
				prof_c0 = net_prof_cpu_cycles();
				/* The begin/end pair brackets the cell on the
				 * board side; the analyzer diffs it, so the
				 * boot, the ARP and the previous cell cannot
				 * leak into this one's layer ledger. */
				emit_layer_counters("begin");
#if !NETIO_GMAC
				emit_rxpath("begin");
				rxpath_window(1);
#endif

#if defined(CONFIG_NETIO_RAW_UDP) && CONFIG_NETIO_RAW_UDP
				/* The window opens with the cell: from here the
				 * input-path callback counts, and RES closes it. */
				g_raw_peer_valid = 0;
				g_raw_in_window = 1;
#else
				io_rx_stop = 0;
				if (osThreadNew(UDP_Receiver, NULL,
						&udp_rx_attr) == NULL)
					__THREAD_EXIT(BSD_ERROR);
#endif

				/* The RES the client waits for: Arm sends the echoed
				 * control block back after the receiver is up. */
				rc = send_data(csock, (char *)&ctl, sizeof(ctl));
				if (rc < 0)
					__THREAD_EXIT(rc);
			} else if (io.command == CMD_RES) {
#if defined(CONFIG_NETIO_RXPATH) && CONFIG_NETIO_RXPATH
				MDK_ANCHOR("netio: ctl res start\n");
#endif
#if NETIO_GMAC
				struct net_prof_snap prof_b;
#endif
				uint64_t wall_ticks;

#if defined(CONFIG_NETIO_RAW_UDP) && CONFIG_NETIO_RAW_UDP
				g_raw_in_window = 0;
#else
				udp_rx_quiesce();
#endif
				MDK_ANCHOR("netio: udp rx done pkts=%lu bytes=%lu\n",
					   (unsigned long)io.pkt_count,
					   (unsigned long)io.rx_bytes);
#if !NETIO_GMAC
				/* The cell's far end: until RTOS-03 the WLAN C2S
				 * cell closed without a single board snapshot,
				 * so a "post-cell" claim could only quote the
				 * begin line.  The GMAC backend's output stays
				 * byte-identical to its measured baseline.
				 * The rxpath sampler keeps running past this
				 * point: the stall this round chases happens
				 * after the window (S2C or the idle that
				 * follows it), and a sampler that stops with
				 * the cell has never seen one. */
				emit_layer_counters("udp-c2s-end");
				emit_rxpath("udp-c2s-end");
#endif
#if NETIO_GMAC
				{
					struct rx_win win_b;

					rx_win_take(&win_b);
					rx_win_report("udp-c2s", &g_win_a,
						      &win_b);
				}
#endif

				/* Cell closes here, after the receiver is out of
				 * its receive call, so no bucket is read
				 * mid-flight. */
				wall_ticks = gtimer_count_get() - prof_t0;
#if NETIO_GMAC
				net_prof_take(&prof_b);
				prof_b.frames = io_rx_total;
				prof_b.app_empty = io_empty_polls + io_app_blocked;
				prof_b.app_charged = io_app_charged;
				net_prof_report("udp-c2s", &prof_a, &prof_b,
						wall_ticks / 24u);
#endif
				/* The same cell on the wall clock: microseconds
				 * per frame (gtimer, 24 MHz) is the ruler; the cycle
				 * count is reported beside it as a raw number, not
				 * as a frequency (K2-14: the cycle counter stops in
				 * WFI and the requested OPP is not the delivered
				 * one, so a cycles-over-wall ratio is not a clock). */
				net_prof_cpu_cell("udp-c2s",
				                   net_prof_cpu_cycles() - prof_c0,
				                   wall_ticks,
				                   io.pkt_count);
				ctl.cmd = htonl(CMD_RES);
				ctl.data = htonl(io.pkt_count);
				rc = send_data(csock, (char *)&ctl, sizeof(ctl));
				if (rc < 0)
					__THREAD_EXIT(rc);
			} else if (io.command == CMD_S2C) {
#if !NETIO_GMAC
				/* rxpath stays on: this is the phase the
				 * stall lives in (see the C2S-end note). */
				emit_rxpath("udp-s2c-begin");
#endif
#if defined(CONFIG_NETIO_RAW_UDP) && CONFIG_NETIO_RAW_UDP
				g_raw_in_window = 0;
#else
				udp_rx_quiesce();
#endif
				io.pkt_size = ntohl(ctl.data);
				io.tx_bytes = 0;
				io.pkt_count = 0;
				netio_s2c_refusals = 0;
				if ((int32_t)io.pkt_size > (int32_t)io.buf_size)
					__THREAD_EXIT(BSD_EMSGSIZE);

				MDK_ANCHOR("netio: udp s2c pkt=%lu\n",
					   (unsigned long)io.pkt_size);

				io_rx_stop = 1;
				osDelay(5u);
				io.client->sin_port = htons(NETIO_AUXPORT);
				io.timeover = false;
				init_buffer(io.buf, io.buf_size);
				if (io.timer != NULL)
					osTimerStart(io.timer, 6000U);

				io.buf[0] = 0;
				{
					/* The transmit cell opens here, after the
					 * buffer and timer are armed, so the cell
					 * covers the send loop only - not the 5 ms
					 * quiesce and not the abort's wait-out. */
#if NETIO_GMAC
					struct net_prof_snap prof_a;
					struct net_prof_snap prof_b;
#endif
					uint64_t s2c_t0 = gtimer_count_get();
					uint64_t s2c_c0 = net_prof_cpu_cycles();
					int s2c_reported = 0;
#if !NETIO_GMAC
					/* Consecutive netif backpressure refusals the
					 * transmit cell has waited through (WLAN only;
					 * see S2C_REFUSAL_MAX). */
					int s2c_refusals = 0;
#endif

#if NETIO_GMAC
					net_prof_take(&prof_a);
#endif
					stall_begin();

					while (!io.timeover) {
						for (nb = 0; nb < (int32_t)io.pkt_size;) {
#if defined(CONFIG_NETIO_RAW_UDP) && CONFIG_NETIO_RAW_UDP
							rc = raw_udp_send(&io.buf[nb],
									  io.pkt_size - nb);
#else
							rc = sendto(io.udp_sock,
								    (char *)&io.buf[nb],
								    io.pkt_size - nb, 0,
								    (struct sockaddr *)io.client,
								    sizeof(*io.client));
#endif
#if !NETIO_GMAC
							/* WLAN netif backpressure is not an
							 * abort: the bridge gates the driver's
							 * queue while it is full and wakes it
							 * on the next completion, so a
							 * non-blocking blast sees ERR_MEM
							 * between wakes.  Wait for the wake
							 * and resend the same frame; a refusal
							 * that persists past S2C_REFUSAL_MAX
							 * milliseconds still aborts the cell.
							 * WLAN-only so the wired arm's bytes
							 * remain what its numbers were measured
							 * with (its 256-slot ring never
							 * refuses). */
							if (rc < 0 && ++s2c_refusals < S2C_REFUSAL_MAX) {
								netio_s2c_refusals++;
								osDelay(1);
								continue;
							}
							if (rc >= 0)
								s2c_refusals = 0;
#endif
							if (rc < 0) {
								/* Close the cell at
								 * the abort, not after
								 * the wait-out: the
								 * idle delay would turn
								 * "cost per frame" into
								 * "idle cycles per
								 * second". */
								if (!s2c_reported) {
									uint64_t ticks = gtimer_count_get() - s2c_t0;

#if NETIO_GMAC
									net_prof_take(&prof_b);
									net_prof_report("udp-s2c", &prof_a,
											&prof_b, ticks / 24u);
#endif
									net_prof_cpu_cell("udp-s2c",
									                   net_prof_cpu_cycles() - s2c_c0,
									                   ticks, io.pkt_count);
								}
								s2c_reported = 1;
								/* K2-14: the ledger has to be on the
								 * console at the moment of the abort.
								 * The cell then idles out its window
								 * and the udp-end report only follows
								 * when the client closes the control
								 * socket - a client left waiting for a
								 * receive stream that never comes does
								 * not close it, and the numbers are
								 * lost exactly when they matter.
								 *
								 * The ledger comes BEFORE the stall
								 * probe: the probe reads the raw MMC
								 * block, and on a revision whose MMC
								 * counters are read-clear that read
								 * would zero the numbers this line is
								 * about to report. */
								emit_layer_counters("udp-s2c-abort");
								/* K2-16: after the ledger, in the idle
								 * window this branch waits out. The
								 * probe freezes its pre-trigger ring,
								 * scans the TX descriptors, kicks the
								 * tail pointer once, samples the
								 * channel for ~96 ms and dumps. */
								stall_capture();
								stall_end();
								while (!io.timeover)
									osDelay(2);
								goto done_send;
							}
							nb += rc;
							stall_tick();
						}
						io.tx_bytes += io.pkt_size;
						io.pkt_count++;
					}
					if (!s2c_reported) {
						uint64_t ticks = gtimer_count_get() - s2c_t0;

#if NETIO_GMAC
						net_prof_take(&prof_b);
						net_prof_report("udp-s2c", &prof_a, &prof_b,
								ticks / 24u);
#endif
						net_prof_cpu_cell("udp-s2c",
						                   net_prof_cpu_cycles() - s2c_c0,
						                   ticks, io.pkt_count);
					}
				}
done_send:
				stall_end();
#if !(defined(CONFIG_NETIO_RAW_UDP) && CONFIG_NETIO_RAW_UDP)
				lwip_close(io.udp_sock);
#endif
				MDK_ANCHOR("netio: udp tx done pkts=%lu bytes=%lu "
					   "refusals=%lu\n",
					   (unsigned long)io.pkt_count,
					   (unsigned long)io.tx_bytes,
					   netio_s2c_refusals);

				io.udp_sock = -1;
				ctl.cmd = htonl(CMD_RES);
				ctl.data = htonl(io.pkt_count);
				rc = send_data(csock, (char *)&ctl, sizeof(ctl));
				if (rc < 0)
					__THREAD_EXIT(rc);
			} else {
				break;
			}
		}
		emit_layer_counters("udp-end");
		lwip_close(csock);
	}
	lwip_close(sock);
}

/* --- K2-16 TCP receive-direction probe -------------------------------------
 *
 * The peer-to-board TCP direction collapses (K2-16 defect B: 41 kB/s, the
 * c2s phase ends after ~256 packets). Which layer it collapses in - the
 * driver's receive ring, the bridge, the stack's input, the socket wake, or
 * the window the peer is throttled by - cannot be told from the socket API,
 * and the console cannot carry a timeline. So: a low-priority sampler every
 * 250 ms into RAM, dumped after the phase.
 *
 * What it reads, and why:
 *  - the PCB's windows (ethnetif_tcp_state): a zero advertised window is the
 *    peer being throttled by us; an open window with no data arrival is the
 *    input path;
 *  - the socket read-path histogram (updated by the c2s loop itself): calls,
 *    full reads, short reads, zero reads, errors;
 *  - lwIP's tcp and MIB2 counters (retransmits are the direct read on
 *    "were segments lost or just late");
 *  - the driver's software counters and the quiet ring snapshot (no
 *    read-clear status read: an observer that eats an RX interrupt would
 *    reproduce the symptom it is looking for).
 *
 * Requires -DCONFIG_GMAC_TCP_PROBE=1 (turns MIB2_STATS on in lwipopts.h).
 * Instrumented arm: its throughput is not the acceptance number. */

/* Socket read-path counters. Compiled in every build: the c2s loop updates
 * them unconditionally (five increments per read, noise) and the probe is the
 * only reader, so the default image's network path is unchanged. */
static uint32_t tcp_recv_calls;
static uint32_t tcp_recv_full;
static uint32_t tcp_recv_short;
static uint32_t tcp_recv_zero;
static uint32_t tcp_recv_err;

#if NETIO_GMAC && defined(CONFIG_NETIO_TCP_PROBE) && CONFIG_NETIO_TCP_PROBE
#define TCP_PROBE_SAMPLES	64
#define TCP_PROBE_PERIOD_MS	250u

struct tcp_probe_sample {
	uint32_t t_ms;
	uint32_t rcv_wnd;
	uint32_t rcv_ann_wnd;
	uint32_t snd_wnd;
	uint32_t snd_queuelen;
	uint32_t state;
	uint32_t flags;
	uint32_t recv_calls;
	uint32_t recv_full;
	uint32_t recv_short;
	uint32_t recv_zero;
	uint32_t recv_err;
	uint32_t tp_xmit;
	uint32_t tp_recv;
	uint32_t tp_drop;
	uint32_t tp_chkerr;
	uint32_t tp_memerr;
	uint32_t mib_in;
	uint32_t mib_out;
	uint32_t mib_retrans;
	uint32_t mib_inerrs;
	uint32_t rx_frames;
	uint32_t rx_dropped;
	uint32_t rx_buf_unavail;
	uint32_t rx_irqs;
	uint32_t ring_armed;
	uint32_t own_clear;
	uint32_t cur_rx_desc;
	uint32_t rx_end;
	uint32_t zc_wrap_live;
	uint32_t zc_free_dead;
	uint32_t zc_dead_handle;
};

static struct tcp_probe_sample tcp_probe[TCP_PROBE_SAMPLES];
static unsigned tcp_probe_count;
static uint64_t tcp_probe_t0;
static volatile int tcp_probe_on;

/* The socket read path, updated by the c2s loop (single writer). */
static void tcp_probe_begin(void)
{
	tcp_probe_count = 0u;
	tcp_recv_calls = 0u;
	tcp_recv_full = 0u;
	tcp_recv_short = 0u;
	tcp_recv_zero = 0u;
	tcp_recv_err = 0u;
	tcp_probe_t0 = gtimer_count_get();
	tcp_probe_on = 1;
}

static void tcp_probe_take(void)
{
	struct tcp_probe_sample *s;
	struct ethnetif_tcp_state st;
	struct dwc_eqos_stats ds;
	struct dwc_eqos_ring ring;

	if ((tcp_probe_on == 0) || (tcp_probe_count >= TCP_PROBE_SAMPLES)) {
		return;
	}
	s = &tcp_probe[tcp_probe_count];
	memset(s, 0, sizeof(*s));
	s->t_ms = (uint32_t)(((uint64_t)(gtimer_count_get() - tcp_probe_t0) *
			      1000u) / 24000000u);

	if (ethnetif_tcp_state(NETIO_PORT, &st, 1) > 0) {
		s->rcv_wnd = st.rcv_wnd;
		s->rcv_ann_wnd = st.rcv_ann_wnd;
		s->snd_wnd = st.snd_wnd;
		s->snd_queuelen = st.snd_queuelen;
		s->state = st.state;
		s->flags = st.flags;
	}

	s->recv_calls = tcp_recv_calls;
	s->recv_full = tcp_recv_full;
	s->recv_short = tcp_recv_short;
	s->recv_zero = tcp_recv_zero;
	s->recv_err = tcp_recv_err;

#if LWIP_STATS
	s->tp_xmit = (uint32_t)lwip_stats.tcp.xmit;
	s->tp_recv = (uint32_t)lwip_stats.tcp.recv;
	s->tp_drop = (uint32_t)lwip_stats.tcp.drop;
	s->tp_chkerr = (uint32_t)lwip_stats.tcp.chkerr;
	s->tp_memerr = (uint32_t)lwip_stats.tcp.memerr;
#endif
#if MIB2_STATS
	s->mib_in = (uint32_t)lwip_stats.mib2.tcpinsegs;
	s->mib_out = (uint32_t)lwip_stats.mib2.tcpoutsegs;
	s->mib_retrans = (uint32_t)lwip_stats.mib2.tcpretranssegs;
	s->mib_inerrs = (uint32_t)lwip_stats.mib2.tcpinerrs;
#endif

	memset(&ds, 0, sizeof(ds));
	dwc_eqos_get_stats(MN_GMAC, &ds);
	s->rx_frames = ds.rx_frames;
	s->rx_dropped = ds.rx_dropped;
	s->rx_buf_unavail = ds.rx_buf_unavail;
	s->rx_irqs = ds.rx_irqs;

	ethnetif_zc_audit(&s->zc_wrap_live, &s->zc_free_dead, &s->zc_dead_handle);

	memset(&ring, 0, sizeof(ring));
	if (dwc_eqos_get_ring_quiet(MN_GMAC, &ring) == 0) {
		s->ring_armed = ring.ring_armed;
		s->own_clear = ring.own_clear;
		s->cur_rx_desc = ring.cur_rx_desc;
		s->rx_end = ring.rx_end;
	}

	tcp_probe_count++;
}

static void tcp_probe_dump(void)
{
	unsigned i;

	MDK_ANCHOR("netio: tcpprobe n=%u period_ms=%u\n", tcp_probe_count,
		   (unsigned)TCP_PROBE_PERIOD_MS);
	for (i = 0u; i < tcp_probe_count; i++) {
		const struct tcp_probe_sample *s = &tcp_probe[i];

		/* Two lines per sample: the console's line buffer is 256 bytes
		 * and one line with every field would sit on that ceiling. */
		MDK_ANCHOR("netio: tcp t=%lu wnd=%lu/%lu/%lu q=%lu st=%lu fl=%02lx "
			   "recv=%lu/%lu/%lu/%lu/%lu\n",
			   (unsigned long)s->t_ms,
			   (unsigned long)s->rcv_wnd,
			   (unsigned long)s->rcv_ann_wnd,
			   (unsigned long)s->snd_wnd,
			   (unsigned long)s->snd_queuelen,
			   (unsigned long)s->state,
			   (unsigned long)s->flags,
			   (unsigned long)s->recv_calls,
			   (unsigned long)s->recv_full,
			   (unsigned long)s->recv_short,
			   (unsigned long)s->recv_zero,
			   (unsigned long)s->recv_err);
		MDK_ANCHOR("netio: tcpc t=%lu seg=%lu/%lu/%lu/%lu tp=%lu/%lu/%lu/%lu "
			   "drv=%lu/%lu/%lu/%lu ring=%lu/%lu/%08lx/%08lx\n",
			   (unsigned long)s->t_ms,
			   (unsigned long)s->mib_in,
			   (unsigned long)s->mib_out,
			   (unsigned long)s->mib_retrans,
			   (unsigned long)s->mib_inerrs,
			   (unsigned long)s->tp_xmit,
			   (unsigned long)s->tp_recv,
			   (unsigned long)s->tp_drop,
			   (unsigned long)s->tp_chkerr,
			   (unsigned long)s->rx_frames,
			   (unsigned long)s->rx_dropped,
			   (unsigned long)s->rx_buf_unavail,
			   (unsigned long)s->rx_irqs,
			   (unsigned long)s->ring_armed,
			   (unsigned long)s->own_clear,
			   (unsigned long)s->cur_rx_desc,
			   (unsigned long)s->rx_end);
		MDK_ANCHOR("netio: zca t=%lu wrap_live=%lu free_dead=%lu "
			   "dead_handle=%lu\n",
			   (unsigned long)s->t_ms,
			   (unsigned long)s->zc_wrap_live,
			   (unsigned long)s->zc_free_dead,
			   (unsigned long)s->zc_dead_handle);
	}
}

static void tcp_probe_end(void)
{
	tcp_probe_on = 0;
	tcp_probe_dump();
}

static void tcp_probe_thread(void *arg)
{
	(void)arg;
	for (;;) {
		if (tcp_probe_on != 0) {
			tcp_probe_take();
			osDelay(TCP_PROBE_PERIOD_MS);
		} else {
			osDelay(20u);
		}
	}
}

static const osThreadAttr_t tcp_probe_attr = {
	.name = "netio-tcp-probe",
	.priority = osPriorityLow,
	.stack_size = 2048U,
};
#else
/* Probe off: the read-path counters still reset per phase (they are printed
 * by the "tcp rx done" line), nothing is sampled and nothing is dumped. */
#define tcp_probe_begin()						\
	do {								\
		tcp_recv_calls = 0u;					\
		tcp_recv_full = 0u;					\
		tcp_recv_short = 0u;					\
		tcp_recv_zero = 0u;					\
		tcp_recv_err = 0u;					\
	} while (0)
#define tcp_probe_end()		do { } while (0)
#endif /* CONFIG_NETIO_TCP_PROBE */

static void TCP_Server(void *arg)
{
	struct sockaddr_in sa;
	CONTROL ctl;
	int32_t sock, csock, rc;
	socklen_t sa_len;
	int32_t nb;
	struct timeval tv;
	fd_set fds;

	(void)arg;
	MDK_ANCHOR("netio: tcp server start\n");

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		__THREAD_EXIT(sock);

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(NETIO_PORT);
	sa.sin_addr.s_addr = INADDR_ANY;
	rc = bind(sock, (struct sockaddr *)&sa, sizeof(sa));
	if (rc < 0)
		__THREAD_EXIT(rc);

	rc = listen(sock, 1);
	if (rc < 0)
		__THREAD_EXIT(rc);

	MDK_ANCHOR("netio: tcp listening port=%d\n", NETIO_PORT);

	for (;;) {
		FD_ZERO(&fds);
		FD_SET(sock, &fds);
		tv.tv_sec = 120;
		tv.tv_usec = 0;

		rc = select(sock + 1, &fds, NULL, NULL, &tv);
		if (rc < 0)
			__THREAD_EXIT(rc);
		if ((rc == 0) || (FD_ISSET(sock, &fds) == 0))
			continue;

		sa_len = sizeof(*io.client);
		csock = accept(sock, (struct sockaddr *)io.client, &sa_len);
		if (csock < 0)
			continue;

		MDK_ANCHOR("netio: tcp client connected\n");
#ifdef NET_WATCH
		/* the watchdog only calls a frozen counter set a stall while a
		 * cell is in flight (a quiet link has quiet counters) */
		net_watch_session(1);
#endif

		for (;;) {
			rc = recv_data(csock, &ctl, sizeof(ctl));
			if (rc < 0)
				__THREAD_EXIT(rc);

			io.command = ntohl(ctl.cmd);

			if (io.command == CMD_C2S) {
				io.pkt_size = ntohl(ctl.data);
				io.pkt_count = 0;
				io.rx_bytes = 0;
				MDK_ANCHOR("netio: tcp c2s pkt=%lu\n",
					   (unsigned long)io.pkt_size);
				if ((int32_t)io.pkt_size > (int32_t)io.buf_size)
					__THREAD_EXIT(BSD_EMSGSIZE);

				emit_layer_counters("begin");

				{
					uint64_t c2s_t0 = gtimer_count_get();
					uint64_t c2s_c0 = net_prof_cpu_cycles();
					uint32_t partial = 0u;

					tcp_probe_begin();

					/* K2-16: the accounting is per byte actually
					 * read, and a packet counts only when the whole
					 * pkt_size arrived. The previous shape added
					 * pkt_size even after a short read and could
					 * spin forever on a zero read (peer close
					 * mid-packet): both make a stalled receive
					 * look like a clean one in the one number this
					 * defect is reported by. The loop still ends on
					 * the peer's end marker (buf[0] != 0). */
					do {
						for (nb = 0; nb < (int32_t)io.pkt_size;) {
							rc = recv(csock,
								  (char *)&io.buf[nb],
								  io.pkt_size - nb, 0);
							tcp_recv_calls++;
							if (rc < 0) {
								tcp_recv_err++;
								break;
							}
							if (rc == 0) {
								tcp_recv_zero++;
								break;
							}
							if (rc < ((int32_t)io.pkt_size - nb)) {
								tcp_recv_short++;
							} else {
								tcp_recv_full++;
							}
							nb += rc;
						}
						io.rx_bytes += (uint32_t)nb;
						if (nb == (int32_t)io.pkt_size) {
							io.pkt_count++;
						} else {
							partial++;
							break;
						}
					} while (io.buf[0] == 0 && rc > 0);

					tcp_probe_end();

					MDK_ANCHOR("netio: tcp rx done pkts=%lu bytes=%lu "
						   "partial=%lu recv_calls=%lu err=%lu "
						   "zero=%lu short=%lu\n",
						   (unsigned long)io.pkt_count,
						   (unsigned long)io.rx_bytes,
						   (unsigned long)partial,
						   (unsigned long)tcp_recv_calls,
						   (unsigned long)tcp_recv_err,
						   (unsigned long)tcp_recv_zero,
						   (unsigned long)tcp_recv_short);
					/* Exact c2s window for the layer ledger: the
					 * "tcp-end" snapshot would also carry the s2c
					 * phase, which is the direction that works. */
					emit_layer_counters("tcp-c2s-end");
					net_prof_cpu_cell("tcp-c2s",
					                   net_prof_cpu_cycles() - c2s_c0,
					                   gtimer_count_get() - c2s_t0,
					                   io.pkt_count);
				}
			} else if (io.command == CMD_S2C) {
				io.pkt_size = ntohl(ctl.data);
				io.pkt_count = 0;
				io.tx_bytes = 0;
				if (io.pkt_size > io.buf_size)
					__THREAD_EXIT(BSD_EMSGSIZE);
				MDK_ANCHOR("netio: tcp s2c pkt=%lu\n",
					   (unsigned long)io.pkt_size);

				init_buffer(io.buf, io.buf_size);
				io.timeover = false;
				if (io.timer != NULL)
					osTimerStart(io.timer, 6000U);

				io.buf[0] = 0;
				{
					uint64_t s2c_t0 = gtimer_count_get();
					uint64_t s2c_c0 = net_prof_cpu_cycles();

					while (!io.timeover) {
						for (nb = 0; nb < (int32_t)io.pkt_size;) {
							rc = send(csock,
								  (char *)&io.buf[nb],
								  io.pkt_size - nb, 0);
							if (rc < 0)
								break;
							nb += rc;
						}
						io.tx_bytes += io.pkt_size;
						io.pkt_count++;
					}
					net_prof_cpu_cell("tcp-s2c",
					                   net_prof_cpu_cycles() - s2c_c0,
					                   gtimer_count_get() - s2c_t0,
					                   io.pkt_count);
				}
				io.buf[0] = 1;
				rc = send_data(csock, io.buf, io.pkt_size);
				if (rc < 0)
					__THREAD_EXIT(rc);

				MDK_ANCHOR("netio: tcp tx done pkts=%lu bytes=%lu\n",
					   (unsigned long)io.pkt_count,
					   (unsigned long)io.tx_bytes);
			} else {
				break;
			}
		}
		emit_layer_counters("tcp-end");
#ifdef NET_WATCH
		net_watch_session(0);
#endif
		lwip_close(csock);
	}
	lwip_close(sock);
}

/* ---------------------------------------------------------------------------
 * Board glue: the GMAC line and the lwIP stack, the same sequence net.c runs.
 * ------------------------------------------------------------------------- */

static int g_line_up;

#if NETIO_GMAC
static struct netif g_netif;
static osSemaphoreId_t g_rx_sem;

static void rx_notify(unsigned int index)
{
	(void)index;
	if (g_rx_sem != NULL)
		(void)osSemaphoreRelease(g_rx_sem);
}

/* With CONFIG_GMAC_RX_BH the ISR only signals: somebody has to run the
 * completion sweep. Both the interrupt thread and a 100 ms service thread do
 * it, exactly as net.c has done since 2026-10-24. */
static void rx_thread(void *arg)
{
	(void)arg;
	for (;;) {
		(void)osSemaphoreAcquire(g_rx_sem, osWaitForever);
		dwc_eqos_watchdog(MN_GMAC);
		(void)ethnetif_poll(&g_netif);
	}
}

static void service_thread(void *arg)
{
	(void)arg;
	for (;;) {
		osDelay(100u);
		dwc_eqos_watchdog(MN_GMAC);
	}
}

static int line_up(void)
{
	const struct dwc_eqos_plat *plat;
	eqos_link_info_t link;
	uint32_t configure;
	ip4_addr_t ip, mask, gw;
	uint8_t mac[6];

	if (g_line_up)
		return 0;

	plat = eqos_board_plat(MN_GMAC);
	if (plat == NULL || dwc_eqos_initialize(MN_GMAC, plat) != 0) {
		CASE_ANCHOR("net: netio FAIL eqos init\n");
		return -1;
	}
#if defined(CONFIG_GMAC_RX_ZC) && CONFIG_GMAC_RX_ZC
	/* Arm zero copy before the port powers on: a frame that completes with
	 * no lend-out hook installed is refused and dropped by the driver, so
	 * arming late turns the first burst into receive loss. (net.c has done
	 * this since 2026-10-25; the netio case did not, which made the first
	 * ZC boot here look like "no route to host" - the board dropped every
	 * ARP request.) */
	ethnetif_zc_arm();
#endif
	if (dwc_eqos_power_control(MN_GMAC, EQOS_POWER_FULL) != EQOS_OK) {
		CASE_ANCHOR("net: netio FAIL gmac power\n");
		return -1;
	}
	if (rtl8211f_initialize(MN_GMAC, (void *)(uintptr_t)MN_GMAC,
				eqos_board_mdio_read,
				eqos_board_mdio_write) != EQOS_OK) {
		CASE_ANCHOR("net: netio FAIL phy init\n");
		return -1;
	}
	if (rtl8211f_power_control(MN_GMAC, EQOS_POWER_FULL) != EQOS_OK) {
		CASE_ANCHOR("net: netio FAIL phy power\n");
		return -1;
	}
	(void)rtl8211f_set_interface(MN_GMAC, EQOS_INTERFACE_RGMII);
	if (rtl8211f_set_mode(MN_GMAC,
			      EQOS_PHY_AUTO_NEGOTIATE |
			      EQOS_PHY_SPEED_1G) != EQOS_OK) {
		CASE_ANCHOR("net: netio FAIL phy autoneg\n");
		return -1;
	}
	{
		unsigned int waited = 0u;

		for (;;) {
			link = rtl8211f_get_link_info(MN_GMAC);
			if (link.speed != 0u)
				break;
			if (waited >= 15000u) {
				CASE_ANCHOR("net: netio FAIL link timeout\n");
				return -1;
			}
			(void)osDelay(100u);
			waited += 100u;
		}
	}
	/* EQOS_DUPLEX_FULL is 0 in this driver's mask encoding: only HALF sets
	 * a bit, so OR-ing the duplex value would program half duplex. */
	configure = link.speed | (link.duplex == EQOS_DUPLEX_HALF ?
				  EQOS_DUPLEX_Msk : 0u);
	(void)dwc_eqos_control(MN_GMAC, DWC_EQOS_CONFIGURE, configure);
	(void)dwc_eqos_control(MN_GMAC, DWC_EQOS_CONTROL_TX, 1);
	(void)dwc_eqos_control(MN_GMAC, DWC_EQOS_CONTROL_RX, 1);

	CASE_ANCHOR("net: netio link speed=%lu duplex=%lu\n",
		    (unsigned long)link.speed, (unsigned long)link.duplex);

	g_rx_sem = osSemaphoreNew(0xFFFFu, 0u, NULL);
	if (g_rx_sem == NULL) {
		CASE_ANCHOR("net: netio FAIL rx sem\n");
		return -1;
	}
	dwc_eqos_set_rx_notify(rx_notify);

	if (osThreadNew(rx_thread, NULL,
			&(const osThreadAttr_t){ .name = "netio-rx",
						 .priority = MN_RX_PRIO,
						 .stack_size = 4096U }) == NULL ||
	    osThreadNew(service_thread, NULL,
			&(const osThreadAttr_t){ .name = "netio-svc",
						 .priority = osPriorityLow,
						 .stack_size = 2048U }) == NULL) {
		CASE_ANCHOR("net: netio FAIL threads\n");
		return -1;
	}

	if (case_net_stack_up() != 0) {
		CASE_ANCHOR("net: netio FAIL stack\n");
		return -1;
	}

	IP4_ADDR(&ip, 192, 168, 0, MN_IP3);
	g_local_ip = ip.addr;
	IP4_ADDR(&mask, 255, 255, 255, 0);
	IP4_ADDR(&gw, 192, 168, 0, MN_GW3);

	LOCK_TCPIP_CORE();
	ethnetif_add(&g_netif, MN_GMAC, &ip, &mask, &gw);
	UNLOCK_TCPIP_CORE();

#if defined(CONFIG_NETIO_RAW_UDP) && CONFIG_NETIO_RAW_UDP
	/* K2-07: the data plane is a raw PCB, not a socket. The UDP control
	 * connection is TCP on the same port number (different protocol
	 * space), so this cannot collide with it. */
	LOCK_TCPIP_CORE();
	g_raw_pcb = udp_new();
	if (g_raw_pcb != NULL) {
		if (udp_bind(g_raw_pcb, IP_ADDR_ANY, NETIO_AUXPORT) != ERR_OK) {
			udp_remove(g_raw_pcb);
			g_raw_pcb = NULL;
		} else {
			udp_recv(g_raw_pcb, raw_udp_recv, NULL);
		}
	}
	UNLOCK_TCPIP_CORE();
	if (g_raw_pcb == NULL) {
		CASE_ANCHOR("net: netio FAIL raw pcb\n");
		return -1;
	}
#endif

	(void)dwc_eqos_get_mac_address(MN_GMAC, mac);
	CASE_ANCHOR("net: netio addr 192.168.0.%u/24 "
		    "mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    MN_IP3, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	g_line_up = 1;
	return 0;
}
#else /* WLAN backend */
static int line_up(void)
{
	if (g_line_up)
		return 0;
#if defined(RTOS_CASE_RTW88_NETIO)
	if (rtw88_net_bringup() != 0) {
		CASE_ANCHOR("net: netio FAIL wlan bringup\n");
		return -1;
	}
	g_local_ip = rtw88_net_ip4();
#elif defined(RTOS_CASE_RTW89_NETIO)
	if (rtw89_net_bringup() != 0) {
		CASE_ANCHOR("net: netio FAIL wlan bringup\n");
		return -1;
	}
	g_local_ip = rtw89_net_ip4();
#else
	if (rtl8xxxu_net_bringup() != 0) {
		CASE_ANCHOR("net: netio FAIL wlan bringup\n");
		return -1;
	}
	g_local_ip = rtl8xxxu_net_ip4();
#endif
	CASE_ANCHOR("net: netio addr 192.168.99.2/24\n");
	g_line_up = 1;
	return 0;
}
#endif

/* Arm's app_main_thread: static buffer, one-shot timer, then one control
 * thread per protocol. */
static uint8_t g_io_buf[BUF_SIZE];
static struct sockaddr_in g_sa_client;

static const osThreadAttr_t tcp_srv_attr = {
	.name = "netio-tcp",
	.priority = osPriorityNormal,
	.stack_size = 4096U,
};

static const osThreadAttr_t udp_srv_attr = {
	.name = "netio-udp",
	.priority = osPriorityNormal,
	.stack_size = 4096U,
};

/* The frequency is part of the ruler, not a tuning knob: the RTOS can run the
 * same core at a different OPP than the Linux arms, and a throughput or
 * cycles-per-frame comparison across arms is only meaningful at a known
 * actual clock.  The SCMI readback is what the firmware says; the PMCCNTR
 * spin below is what the core actually did.  Both are recorded; the spin is
 * the number the analysis uses.
 *
 * It is also a build-time lever (K2-14), because the OPP turned out to decide
 * whether a defect shows up: the 475-frame uplink abort only appears once the
 * sender outruns the wire. CONFIG_NETIO_CPU_KHZ=0 keeps the boot OPP and
 * still prints the probe and the readback, so the two arms differ in exactly
 * one bit of the ruler and nothing else.
 *
 * DEFAULT 0 (K2-14, 2026-11-01): a measurement arm must not silently change
 * the machine it measures. Requesting 1.992 GHz here cost the wired uplink
 * cell a factor of twenty in frame rate and ended it in dropped frames, with
 * the same tree, the same client and the downlink cell of the same boot
 * unaffected (runs/plat-01/K2-14/A7 against A5/A6). An arm that wants a known
 * clock asks for it in its own configs/*.mk; the wired netio arms leave the
 * boot OPP alone, which is also the clock the K2-07 reference was taken at. */
#ifndef CONFIG_NETIO_CPU_KHZ
#define CONFIG_NETIO_CPU_KHZ 0
#endif

static void emit_cpu_freq_line(void)
{
	int rc;
	int src = -100;
	uint32_t rb = 0;
	unsigned long req_khz = (unsigned long)CONFIG_NETIO_CPU_KHZ;

	rc = cpu_freq_probe();
	if ((rc == 0) && cpu_freq_supported()) {
		if (req_khz == 0ul) {
			/* Ruler-off arm: leave the OPP the bootloader chose. */
			src = 0;
		} else {
			/* Hz, not kHz: 1.992 GHz is an OPP, 1.992 MHz is not.  The first
			 * W1-01 boot asked for 1992000 (Hz) and set_ret=-1 left the CPU
			 * at the boot OPP (396 MHz actual), which made the C2S look
			 * CPU-bound; the readback (816 MHz) and the PMCCNTR (396 MHz)
			 * disagreeing is what caught it. */
			src = cpu_freq_set_hz((uint32_t)(req_khz * 1000ul));
		}
		rb = cpu_freq_get_hz();
	}
	CASE_ANCHOR("netio: cpu req_khz=%lu readback_khz=%lu ret=%d "
		    "set_ret=%d\n", req_khz, (unsigned long)(rb / 1000u), rc, src);
}

/* K2-16: a second, late frequency request, applied after the line is up and
 * before the ready line. It exists to separate "the higher clock breaks the
 * transmit path" from "the OPP *procedure* leaves the machine in a state that
 * breaks it": set 1992 kHz here, then immediately set 816 and run the cell at
 * the boot clock. If that arm still stalls, the value was never the variable.
 * 0 (the default) is a no-op and costs one branch. */
#ifndef CONFIG_NETIO_CPU_KHZ_LATE
#define CONFIG_NETIO_CPU_KHZ_LATE 0
#endif

static void emit_cpu_late_line(void)
{
	unsigned long req_khz = (unsigned long)CONFIG_NETIO_CPU_KHZ_LATE;
	int rc;
	uint32_t rb;

	if (req_khz == 0ul) {
		return;
	}
	rc = cpu_freq_set_hz((uint32_t)(req_khz * 1000ul));
	rb = cpu_freq_get_hz();
	CASE_ANCHOR("netio: cpu late req_khz=%lu readback_khz=%lu set_ret=%d\n",
		    req_khz, (unsigned long)(rb / 1000u), rc);
}

static int netio_case_run(void)
{
	/* P0.2: a self-checking CPU clock probe, run first and printed before
	 * anything else can fault. If the PMU is trapped by the hypervisor, the
	 * last line on the console is "cpu probe begin" and the board needs a
	 * manual power cycle (AGENTS §6.1: after an exception, send nothing).
	 * The probe is on by default; CONFIG_CPU_PROBE=0 compiles it out. The
	 * sample is a 1 ms busy spin, not an idle delay, so the reported MHz is
	 * the frequency the core actually runs at (WFI would under-report). */
	emit_cpu_freq_line();
	emit_cpu_late_line();
#if NET_PROF_CPU_PROBE
	{
		uint32_t pt0;
		uint32_t pt1;
		uint64_t pc0;
		uint64_t pc1;

		CASE_ANCHOR("netio: cpu probe begin\n");
		if (net_prof_cpu_probe_init() == 0) {
			pt0 = gtimer_count_get();
			pc0 = net_prof_cpu_cycles();
			do {
				pt1 = gtimer_count_get();
			} while ((uint32_t)(pt1 - pt0) < (24u * 1000u));
			pc1 = net_prof_cpu_cycles();
			CASE_ANCHOR("netio: cpu probe ok=1\n");
			net_prof_cpu_cell("spin", pc1 - pc0,
			                  (uint32_t)(pt1 - pt0), 0);
		} else {
			CASE_ANCHOR("netio: cpu probe ok=0 (PMU not accessible)\n");
		}
	}
#endif /* NET_PROF_CPU_PROBE */

	/* K2-14, PMU question: does the cycle counter keep counting while the
	 * core sits in WFI?  Everything the case reports about "effective MHz"
	 * is cycles over wall time, so a counter that runs through a clock-gated
	 * sleep inflates every window that contains idle time - and the uplink
	 * cell is exactly such a window once its transmit ring stalls.
	 *
	 * Two windows of the same wall length, one variable: `busy` spins for
	 * the whole 100 ms, `sleepy` spins for the first and last 10 ms and
	 * sleeps 90 ms in between (osDelay blocks, so the idle task runs and the
	 * core enters WFI).  A cycle counter tied to the core clock gives
	 * sleepy = busy * 20/110; a counter that runs while the core is gated
	 * gives sleepy = busy; a counter faster than the core gives more. */
#if defined(CONFIG_NETIO_CPU_CALIB) && CONFIG_NETIO_CPU_CALIB
	if (net_prof_cpu_probe_ok()) {
		uint32_t t0;
		uint32_t t1;
		uint64_t c0;
		uint64_t c1;

		t0 = (uint32_t)gtimer_count_get();
		c0 = net_prof_cpu_cycles();
		do {
			t1 = (uint32_t)gtimer_count_get();
		} while ((uint32_t)(t1 - t0) < (24u * 100000u));
		c1 = net_prof_cpu_cycles();
		MDK_ANCHOR("netio: cpumix tag=busy mhz_x100=%lu ticks=%lu "
			   "cycles=%lu\n",
			   (unsigned long)(((c1 - c0) * 24ull * 100ull) /
					   (uint32_t)(t1 - t0)),
			   (unsigned long)(t1 - t0), (unsigned long)(c1 - c0));

		t0 = (uint32_t)gtimer_count_get();
		c0 = net_prof_cpu_cycles();
		do {
			t1 = (uint32_t)gtimer_count_get();
		} while ((uint32_t)(t1 - t0) < (24u * 10000u));
		osDelay(90u);
		{
			uint32_t t2 = (uint32_t)gtimer_count_get();

			do {
				t1 = (uint32_t)gtimer_count_get();
			} while ((uint32_t)(t1 - t2) < (24u * 10000u));
		}
		c1 = net_prof_cpu_cycles();
		MDK_ANCHOR("netio: cpumix tag=sleepy mhz_x100=%lu ticks=%lu "
			   "cycles=%lu\n",
			   (unsigned long)(((c1 - c0) * 24ull * 100ull) /
					   (uint32_t)(t1 - t0)),
			   (unsigned long)(t1 - t0), (unsigned long)(c1 - c0));

		/* The same question asked with an instruction instead of a
		 * scheduler call: 100 WFI executions, woken by the 1 ms tick.
		 * If the cycle counter is tied to the core's gated clock this
		 * window accumulates almost nothing; if it is in an always-on
		 * domain it accumulates the full wall time, and then every
		 * "effective MHz" drawn from a window that contains idle time
		 * is a wall-clock ratio wearing a frequency's clothes. */
		t0 = (uint32_t)gtimer_count_get();
		c0 = net_prof_cpu_cycles();
		{
			uint32_t i;

			for (i = 0u; i < 100u; i++)
				__asm__ volatile("wfi");
		}
		c1 = net_prof_cpu_cycles();
		t1 = (uint32_t)gtimer_count_get();
		MDK_ANCHOR("netio: cpumix tag=wfi mhz_x100=%lu ticks=%lu "
			   "cycles=%lu\n",
			   (unsigned long)(((c1 - c0) * 24ull * 100ull) /
					   (uint32_t)(t1 - t0)),
			   (unsigned long)(t1 - t0), (unsigned long)(c1 - c0));
	}
#endif /* CONFIG_NETIO_CPU_CALIB */

	io.buf = &g_io_buf[0];
	io.buf_size = sizeof(g_io_buf);
	io.client = &g_sa_client;
	io.udp_sock = -1;
	io.timer = osTimerNew(cb_timer, osTimerOnce, NULL, NULL);
	if (io.timer == NULL) {
		CASE_ANCHOR("net: netio FAIL timer\n");
		return 1;
	}

	/* Sequential on purpose: TCP first, and UDP only after it is parked in
	 * select(). A kernel that cannot host the second thread says so with a
	 * console line instead of taking the whole board down silently. */
	if (osThreadNew(TCP_Server, NULL, &tcp_srv_attr) == NULL) {
		CASE_ANCHOR("net: netio FAIL tcp thread\n");
		return 1;
	}
	osDelay(500u);
	if (osThreadNew(UDP_Server, NULL, &udp_srv_attr) == NULL) {
		CASE_ANCHOR("net: netio FAIL udp thread\n");
		return 1;
	}

#if NETIO_GMAC && defined(CONFIG_NETIO_TCP_PROBE) && CONFIG_NETIO_TCP_PROBE
	/* Sleeps until a c2s phase arms it (tcp_probe_begin), so its presence
	 * costs one idle thread in every other phase. */
	if (osThreadNew(tcp_probe_thread, NULL, &tcp_probe_attr) == NULL) {
		CASE_ANCHOR("net: netio FAIL tcp probe thread\n");
		return 1;
	}
#endif

#ifdef NET_WATCH
	/* start the ledger before the ready line, so the run's timeline from
	 * "ready" onward is sampled: the wedge lands one to five seconds into
	 * the first cell, and the samples on both sides of it are the evidence */
	net_watch_start();
#endif
	emit_layer_counters("boot");
	CASE_ANCHOR("net: netio ready port=%d aux=%d buf=%u\n",
		    NETIO_PORT, NETIO_AUXPORT, (unsigned)BUF_SIZE);

	for (;;) {
		osDelay(1000u);
	}
	return 0;
}

#if NETIO_GMAC
int case_net_mdk_netio_run(void)
{
	CASE_ANCHOR("rtos: case net_mdk_netio start\n");
	if (line_up() != 0) {
		CASE_ANCHOR("net: netio FAIL line-up\n");
		return 1;
	}
	return netio_case_run();
}
#elif defined(RTOS_CASE_RTL8XXXU_NETIO)
int case_rtl8xxxu_netio_run(void)
{
	CASE_ANCHOR("rtos: case rtl8xxxu_netio start\n");
	if (line_up() != 0) {
		CASE_ANCHOR("net: netio FAIL line-up\n");
		return 1;
	}
	return netio_case_run();
}
#elif defined(RTOS_CASE_RTW89_NETIO)
int case_rtw89_netio_run(void)
{
	CASE_ANCHOR("rtos: case rtw89_netio start\n");
	if (line_up() != 0) {
		CASE_ANCHOR("net: netio FAIL line-up\n");
		return 1;
	}
	return netio_case_run();
}
#else
int case_rtw88_netio_run(void)
{
	CASE_ANCHOR("rtos: case rtw88_netio start\n");
	if (line_up() != 0) {
		CASE_ANCHOR("net: netio FAIL line-up\n");
		return 1;
	}
	return netio_case_run();
}
#endif
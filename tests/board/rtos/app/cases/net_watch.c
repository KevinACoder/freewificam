/* SPDX-License-Identifier: Apache-2.0 */
/*
 * @file   net_watch.c
 * @brief  The wedge ledger for the 8188EU NETIO arm (observation image only).
 *
 * The symptom this exists for: a TCP cell on the WLAN arm freezes one to five
 * seconds in; the client gets no more bytes, pings stop, ARP stops, and the
 * board's own console goes silent - while the scheduler is demonstrably alive
 * (the earlier round's 1 Hz heartbeat kept printing for 300 s).  That shape has
 * several possible causes that all look identical from outside, so the job here
 * is only to place the freeze on a layer:
 *
 *   usb  the completion engine: the drain thread's phase, how many completions
 *        it carried, and the bulk-in/bulk-out endpoints' armed-vs-completed
 *        counts and queue depth
 *   br   the net80211 bridge: frames in/out, transmit completions, the
 *        interface queue depth and whether the driver has its queue stopped
 *   lw   the lwIP side: tcpip-thread liveness (mbox fetches), the core lock's
 *        owner and how long it has been held, mbox pressure, pbuf/tcp-seg pools
 *   rx   the driver's receive seam (calls, stage, pbuf failures)
 *   lock the port serializer (depth, owner, held time)
 *
 * Every number below is a plain scalar read from the layer that owns it; the
 * watch thread takes no lock on the data path, allocates nothing and transmits
 * nothing.  It runs below every data-path thread (the drain thread is normal,
 * the tcpip thread is high), so it only gets the CPU when they are idle -
 * which is what makes a frozen set of counters mean "not progressing" rather
 * than "not scheduled".
 *
 * Sampling: a short line every second keeps the run's timeline unbroken; a
 * long line every five.  A stall is declared once no progress counter has moved
 * for STALL_SAMPLES samples with a cell in flight, and then the full ledger
 * plus the serializer verdict is printed once, followed by one short line every
 * STALL_QUIET samples so the log still shows the board is alive.
 *
 * What this file deliberately does not do: no task table.  The effective kernel
 * config has configUSE_TRACE_FACILITY 0 (the cmsis2 FreeRTOSConfig that sets it
 * to 1 is not the one the include order picks), so the CMSIS enumerator is
 * compiled out; per-thread state waits until the counters point at a
 * thread-level question, which the drain phase and the serializer owner
 * already narrow to two candidates.
 */

#include <stdint.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"

#include "log.h"

#include "arch/sys_arch.h"
#include "lwip/memp.h"
#include "lwip/stats.h"
#include "sys_mbox_seam.h"

#include "wlan_bridge.h"

#include "net_watch.h"

/* the presentation layer's receive seam (rtl8xxxu_net.c, built when NET_WATCH
 * is set): how many frames the driver handed over and where the current one
 * stopped */
extern volatile unsigned int eu_net_rx_calls;
extern volatile unsigned int eu_net_rx_stage;

#define WATCH_STACK	4096u
/* one sample a second; a stall is five unchanged samples (5 s) */
#define WATCH_PERIOD_MS	1000u
#define STALL_SAMPLES	5u
/* after a stall report, print the short line every N samples */
#define STALL_QUIET	10u
/* the long line every N samples */
#define LONG_EVERY	5u

struct watch_state {
	osThreadId_t	thread;
	osThreadId_t	probe;		/* one-shot, started at the first stall */
	volatile int	session;	/* a NETIO cell is in flight */
	unsigned int	samples;
	unsigned int	still;		/* consecutive samples without progress */
	unsigned int	reported;	/* the stall report was printed */
};

static struct watch_state watch;

/* started from the stall report, defined below the sampler */
static void watch_start_probe(void);

/* the progress counters: any one moving means the path is not frozen */
struct watch_progress {
	unsigned long	drain_done;
	unsigned long	rx_comp;
	unsigned long	tx_comp;
	unsigned long	br_tx;
	unsigned long	br_rx;
	unsigned long	tcpip_fetch;
	unsigned long	eu_rx;
};

static unsigned long
watch_seconds(void)
{
	uint32_t hz = (uint32_t)osKernelGetTickFreq();
	uint64_t ticks = (uint64_t)osKernelGetTickCount();

	if (hz == 0u)
		return (unsigned long)ticks;
	return (unsigned long)((ticks * 1000u) / hz) / 1000ul;
}

static void
watch_sample(struct wlan_bridge_counters *br, struct wlan_seam_port_obs *po,
	     struct watch_progress *pr)
{

	wlan_bridge_stats(br);
	memset(po, 0, sizeof(*po));
	wlan_bridge_port_obs(po);

	pr->drain_done = po->usb_drain_done;
	pr->rx_comp = po->rx_ep_completions;
	pr->tx_comp = po->tx_ep_completions;
	pr->br_tx = br->data_tx;
	pr->br_rx = br->rx_frames;
	pr->tcpip_fetch = sys_arch_mbox_fetch_count();
	pr->eu_rx = eu_net_rx_calls;
}

static int
watch_progressed(const struct watch_progress *a, const struct watch_progress *b)
{

	return a->drain_done != b->drain_done || a->rx_comp != b->rx_comp ||
	       a->tx_comp != b->tx_comp || a->br_tx != b->br_tx ||
	       a->br_rx != b->br_rx || a->tcpip_fetch != b->tcpip_fetch ||
	       a->eu_rx != b->eu_rx;
}

/* the short line: the transport and the bridge's live gauges */
static void
watch_short(unsigned long t, const struct wlan_bridge_counters *br,
	    const struct wlan_seam_port_obs *po)
{

	log_emit(ULOG_ALWAYS_LEVEL,
	    "net: wd t=%lu s=%d usb p=%u d=%lu k=%lu rxe=%lu rxc=%lu rxq=%u "
	    "rxi=%u txe=%lu txc=%lu txq=%u txi=%u br tx=%u c=%u rx=%u q=%u "
	    "st=%u\n",
	    t, (int)watch.session,
	    po->usb_drain_phase, (unsigned long)po->usb_drain_done,
	    (unsigned long)po->usb_kicks,
	    (unsigned long)po->rx_ep_enqueued,
	    (unsigned long)po->rx_ep_completions,
	    po->rx_ep_qlen, po->rx_ep_in_flight,
	    (unsigned long)po->tx_ep_enqueued,
	    (unsigned long)po->tx_ep_completions,
	    po->tx_ep_qlen, po->tx_ep_in_flight,
	    br->data_tx, br->tx_completes, br->rx_frames, br->txq_len,
	    br->tx_stopped);
}

/* the long line: everything else, five seconds apart and on a stall */
static void
watch_long(unsigned long t, const struct wlan_bridge_counters *br,
	   const struct wlan_seam_port_obs *po)
{
	struct sys_mbox_seam seam;
	struct osMemoryStats heap;
	const char *lock_owner;
	unsigned long pb_used = 0, pb_avail = 0, pb_err = 0;
	unsigned long sg_used = 0, sg_avail = 0, sg_err = 0;

	memset(&seam, 0, sizeof(seam));
	sys_mbox_seam_get(&seam);
#if LWIP_STATS && MEMP_STATS
	if (lwip_stats.memp[MEMP_PBUF] != NULL) {
		pb_used = (unsigned long)lwip_stats.memp[MEMP_PBUF]->used;
		pb_avail = (unsigned long)lwip_stats.memp[MEMP_PBUF]->avail;
		pb_err = (unsigned long)lwip_stats.memp[MEMP_PBUF]->err;
	}
	if (lwip_stats.memp[MEMP_TCP_SEG] != NULL) {
		sg_used = (unsigned long)lwip_stats.memp[MEMP_TCP_SEG]->used;
		sg_avail = (unsigned long)lwip_stats.memp[MEMP_TCP_SEG]->avail;
		sg_err = (unsigned long)lwip_stats.memp[MEMP_TCP_SEG]->err;
	}
#endif
	memset(&heap, 0, sizeof(heap));
	osMemoryGetStats(&heap);
	lock_owner = sys_arch_core_lock_owner();

	log_emit(ULOG_ALWAYS_LEVEL,
	    "net: wd2 t=%lu br g=%u w=%u wq=%u/%u/%u/%u/%u/%u skb=%u/%u/%u "
	    "usb p=%u ds=%u k=%u x=%u pl=%u/%u irq=%u/%u/%u/%u "
	    "e0=%u/%03x/%04x e1=%u/%03x/%04x "
	    "soc=%05x/%05x/%05x "
	    "lw mf=%lu po=%lu pf=%lu fe=%lu lk=%s/%lu pb=%lu/%lu/%lu "
	    "sg=%lu/%lu/%lu rx=%lu rxst=%u heap=%lu/%lu/%lu\n",
	    t, br->tx_gated, br->tx_wakes,
	    po->wq_depth, po->wq_max, po->wq_runs, po->wq_direct, po->wq_full,
	    po->wq_state,
	    po->skb_used, po->skb_high, po->skb_alloc_fail,
	    po->usb_drain_phase, po->usb_drain_state,
	    po->usb_kicks, po->usb_kills,
	    po->usb_polls, po->usb_poll_drained,
	    po->usb_irq0, po->usb_irq1, po->usb_irq2, po->usb_irq3,
	    po->ehci0_frame, po->ehci0_status & 0xfffu, po->ehci0_portsc & 0xffffu,
	    po->ehci1_frame, po->ehci1_status & 0xfffu, po->ehci1_portsc & 0xffffu,
	    po->soc_cru_gate & 0x1ffffu, po->soc_cru_rst & 0x1ffffu,
	    po->soc_pmucru_gate & 0x1ffffu,
	    sys_arch_mbox_fetch_count(), (unsigned long)seam.post_ok,
	    (unsigned long)seam.post_full, (unsigned long)seam.fetch_empty,
	    lock_owner != NULL ? lock_owner : "-", sys_arch_core_lock_held_ms(),
	    pb_used, pb_avail, pb_err, sg_used, sg_avail, sg_err,
	    (unsigned long)eu_net_rx_calls, (unsigned int)eu_net_rx_stage,
	    (unsigned long)heap.free_bytes, (unsigned long)heap.min_free_bytes,
	    (unsigned long)heap.total_bytes);
}

/* the port serializer, the one lock every stack entry takes */
static void
watch_verdict(unsigned long t)
{
	struct wlan_bridge_lock_obs lock;

	memset(&lock, 0, sizeof(lock));
	wlan_bridge_lock_obs(&lock);
	log_emit(ULOG_ALWAYS_LEVEL,
	    "net: wd3 t=%lu serializer depth=%u owner=%s held_ms=%u\n",
	    t, lock.depth, lock.owner != NULL ? lock.owner : "-", lock.held_ms);
}

static void
net_watch_thread(void *arg)
{
	struct wlan_bridge_counters br;
	struct wlan_seam_port_obs po;
	struct watch_progress pr, last;
	unsigned long t;

	(void)arg;
	memset(&last, 0, sizeof(last));
	for (;;) {
		osDelay(WATCH_PERIOD_MS);
		t = watch_seconds();
		memset(&br, 0, sizeof(br));
		memset(&po, 0, sizeof(po));
		memset(&pr, 0, sizeof(pr));
		watch_sample(&br, &po, &pr);
		watch.samples++;

		if (watch.session == 0) {
			/* a quiet link: the short line still runs, so the log
			 * keeps a clock, but nothing is called a stall */
			watch.still = 0;
			watch.reported = 0;
		} else if (watch_progressed(&pr, &last)) {
			watch.still = 0;
		} else {
			watch.still++;
		}

		if (watch.reported == 0) {
			watch_short(t, &br, &po);
			if ((watch.samples % LONG_EVERY) == 0u)
				watch_long(t, &br, &po);
		}

		if (watch.reported == 0 && watch.still >= STALL_SAMPLES) {
			/* no progress for STALL_SAMPLES seconds while a cell
			 * is in flight: one report, in verdict order */
			watch.reported = 1;
			log_emit(ULOG_ALWAYS_LEVEL,
			    "net: WEDGE t=%lu still=%u samples=%u\n",
			    t, watch.still, watch.samples);
			watch_short(t, &br, &po);
			watch_long(t, &br, &po);
			watch_verdict(t);
			watch_start_probe();
		} else if (watch.reported != 0 &&
			   (watch.still % STALL_QUIET) == 0u) {
			/* still frozen: the short line proves the board is
			 * alive, the long one keeps the controller's status
			 * word in the log (a halted controller and a quiet
			 * device look identical from the counters alone) */
			watch_short(t, &br, &po);
			watch_long(t, &br, &po);
			watch_verdict(t);
		}
		last = pr;
	}
}

/*
 * The post-stall probe: the controller's own registers, one read at a time,
 * each named before it runs.  This is the one surface the ledger must not
 * sample: a controller that stopped answering holds the read forever, which
 * is why the periodic sample reads the SoC-side gates instead.  The marker
 * before each read turns a hang into an answer - the last marker in the log
 * names the register that did not come back.
 */
#define PROBE_STACK	3072u

static void
watch_probe_thread(void *arg)
{
	static const char *const names[] = {
		"usbcmd", "usbsts", "crcr", "erdp", "portsc1", "portsc2",
	};
	unsigned int i;

	(void)arg;
	for (i = 0u; i < (unsigned int)(sizeof(names) / sizeof(names[0]));
	     i++) {
		unsigned int v;

		log_emit(ULOG_ALWAYS_LEVEL, "net: xprobe %s begin\n", names[i]);
		v = wlan_bridge_port_probe(i);
		log_emit(ULOG_ALWAYS_LEVEL, "net: xprobe %s v=%08x\n",
		    names[i], v);
	}
	/* the controller stack's own view: event ring position and cycle,
	 * the interrupt counters and every endpoint still waiting */
	log_emit(ULOG_ALWAYS_LEVEL, "net: xprobe stack begin\n");
	wlan_bridge_port_stall_dump();
	log_emit(ULOG_ALWAYS_LEVEL, "net: xprobe stack done\n");

	/* The experiment the whole image was built for: run the completion
	 * path once from this thread.  If the events the dump just showed are
	 * real, the counters move and the cell recovers - the interrupt that
	 * never came is then proven to be the whole fault.  Three tries, one
	 * second apart, each followed by the counter line. */
	for (i = 0u; i < 3u; i++) {
		struct wlan_bridge_counters br;
		struct wlan_seam_port_obs po;
		struct watch_progress pr;
		unsigned long t;

		log_emit(ULOG_ALWAYS_LEVEL, "net: xpoll %u begin\n", i);
		wlan_bridge_port_poll();
		log_emit(ULOG_ALWAYS_LEVEL, "net: xpoll %u done\n", i);
		osDelay(1000u);

		memset(&br, 0, sizeof(br));
		memset(&po, 0, sizeof(po));
		memset(&pr, 0, sizeof(pr));
		t = watch_seconds();
		watch_sample(&br, &po, &pr);
		watch_short(t, &br, &po);
		watch_verdict(t);
	}
	log_emit(ULOG_ALWAYS_LEVEL, "net: xprobe done\n");
}

static void
watch_start_probe(void)
{
	const osThreadAttr_t attr = {
		.name = "net-xprobe",
		.stack_size = PROBE_STACK,
		.priority = osPriorityLow,
	};

	if (watch.probe != NULL)
		return;
	watch.probe = osThreadNew(watch_probe_thread, NULL, &attr);
	if (watch.probe == NULL)
		log_emit(ULOG_ALWAYS_LEVEL, "net: xprobe refused\n");
}

void
net_watch_start(void)
{
	const osThreadAttr_t attr = {
		.name = "net-watch",
		.stack_size = WATCH_STACK,
		.priority = osPriorityBelowNormal,
	};

	if (watch.thread != NULL)
		return;
	watch.thread = osThreadNew(net_watch_thread, NULL, &attr);
	if (watch.thread == NULL)
		log_emit(ULOG_ALWAYS_LEVEL, "net: wd thread refused\n");
	else
		log_emit(ULOG_ALWAYS_LEVEL, "net: wd started\n");
}

void
net_watch_session(int active)
{

	watch.session = active;
	if (!active) {
		watch.still = 0;
		watch.reported = 0;
	}
}

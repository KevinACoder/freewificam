/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    lkpi_rxtrace.c
 * @brief   The bounded receive boundary trace's body (see lkpi_rxtrace.h).
 *
 * @design the same bounded-ring shape the control-transfer trace uses
 *         (linuxkpi_usb.c, lkpi_usb_ctrl_trace): a static array, a sequence
 *         number, newest-first copy-out, no allocation and no lock of its
 *         own.  The board's console ring drops bursts, so the dump is emitted
 *         once per anomaly class instead of streaming; every line carries a
 *         sequence number because the console's log folds consecutive
 *         identical lines and a trace whose repeats collapse is worthless.
 * @date    10.10.2026
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "linuxkpi_core.h"
#include <net/mac80211.h>
#include "lkpi_rxtrace.h"

#ifdef CONFIG_LKPI_RXTRACE

#include "os/cmsis2/cmsis2.h"

#define	LKPI_RXTRACE_N		32	/* events held per side */
#define	LKPI_RXTRACE_HEAD	16	/* leading bytes copied per event */

struct lkpi_rx_evt {
	u32	seq;
	u32	t_ms;
	u32	ptr;		/* completion: transfer buffer; delivery: skb */
	u8	ep;		/* completion: endpoint address */
	s8	st;		/* completion: urb status; delivery: fcs_bad */
	u16	len;
	u8	head[LKPI_RXTRACE_HEAD];
};

static struct lkpi_rx_evt	ck_ring[LKPI_RXTRACE_N];	/* completion side */
static struct lkpi_rx_evt	dv_ring[LKPI_RXTRACE_N];	/* delivery side */
static struct lkpi_rx_evt	first_dv_anom;
static int			first_dv_anom_seen;
static u32			ck_seq;
static u32			dv_seq;
static u32			ck_n;
static u32			dv_n;
static u32			anom_n;
static u32			short_n;
static u32			dump_n;

/*
 * A status class, not the errno: + = success, - = an error completion, and
 * the printed value carries the raw status for the reader (it is one byte in
 * the ring; the full int goes to the console line).
 */
static void
evt_fill(struct lkpi_rx_evt *e, u32 seq, u32 ptr, u8 ep, s8 st, u32 len,
    const void *data)
{
	size_t n = len < LKPI_RXTRACE_HEAD ? (size_t)len : LKPI_RXTRACE_HEAD;

	e->seq = seq;
	e->t_ms = (u32)osKernelGetTickCount();
	e->ptr = ptr;
	e->ep = ep;
	e->st = st;
	e->len = (u16)(len > 0xffffU ? 0xffffU : len);
	if (data != NULL && n != 0)
		memcpy(e->head, data, n);
	else
		memset(e->head, 0, sizeof(e->head));
}

static void
evt_push(struct lkpi_rx_evt *ring, u32 *seq, struct lkpi_rx_evt *e)
{
	os_critical_state_t cs;

	cs = osCriticalEnter();
	ring[*seq % LKPI_RXTRACE_N] = *e;
	(*seq)++;
	osCriticalExit(cs);
}

/* newest-first copy-out; returns how many were copied */
static unsigned int
evt_copy(const struct lkpi_rx_evt *ring, u32 seq, unsigned int want,
    struct lkpi_rx_evt *out)
{
	unsigned int i, n = want;
	os_critical_state_t cs;

	cs = osCriticalEnter();
	if (n > seq)
		n = seq;
	if (n > LKPI_RXTRACE_N)
		n = LKPI_RXTRACE_N;
	for (i = 0; i < n; i++)
		out[i] = ring[(seq - 1U - i) % LKPI_RXTRACE_N];
	osCriticalExit(cs);
	return n;
}

static void
evt_dump(const struct lkpi_rx_evt *e, const char *side)
{

	lkpi_printf(ULOG_DEBUG_LEVEL, "rxtrace %s seq=%u t=%u ptr=%08x ep=%02x st=%d len=%u "
	    "head=%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x "
	    "%02x%02x%02x%02x\n", side, e->seq, e->t_ms, e->ptr, e->ep,
	    (int)e->st, e->len,
	    e->head[0], e->head[1], e->head[2], e->head[3],
	    e->head[4], e->head[5], e->head[6], e->head[7],
	    e->head[8], e->head[9], e->head[10], e->head[11],
	    e->head[12], e->head[13], e->head[14], e->head[15]);
}

/*
 * One snapshot of both sides.  Called from whichever side saw the anomaly;
 * the other side's ring still holds the transfers that produced it.  Bounded
 * to three dumps per boot - a fourth adds nothing a first did not already
 * name, and the console ring would have dropped most of it anyway.
 */
static void
both_sides_dump(const char *why)
{
	struct lkpi_rx_evt ev[16];
	unsigned int n, i;

	if (dump_n >= 3U)
		return;
	dump_n++;

	lkpi_printf(ULOG_DEBUG_LEVEL, "rxtrace: dump #%u (%s) comp_n=%u deliv_n=%u anom=%u\n",
	    dump_n, why, ck_n, dv_n, anom_n);

	n = evt_copy(ck_ring, ck_seq, 16, ev);
	lkpi_printf(ULOG_DEBUG_LEVEL, "rxtrace: completion last %u:\n", n);
	for (i = 0; i < n; i++)
		evt_dump(&ev[i], "c");

	n = evt_copy(dv_ring, dv_seq, 16, ev);
	lkpi_printf(ULOG_DEBUG_LEVEL, "rxtrace: delivery last %u:\n", n);
	for (i = 0; i < n; i++)
		evt_dump(&ev[i], "d");
}

void
lkpi_rxtrace_completion(unsigned int ep_addr, int status, u32 len,
    const void *data)
{
	struct lkpi_rx_evt e;

	evt_fill(&e, ck_seq, (u32)(uintptr_t)data, (u8)ep_addr,
	    (s8)(status < -128 ? -128 : (status > 127 ? 127 : status)), len,
	    data);
	evt_push(ck_ring, &ck_seq, &e);
	ck_n++;

	if (status != 0) {
		anom_n++;
		both_sides_dump("completion error");
	}
}

void
lkpi_rxtrace_delivery(struct sk_buff *skb)
{
	struct lkpi_rx_evt e;
	const struct ieee80211_rx_status *rs;
	u32 len;
	int fcs_bad;

	if (skb == NULL)
		return;
	len = skb->len;
	rs = (const struct ieee80211_rx_status *)skb->cb;
	fcs_bad = (rs->flag & RX_FLAG_FAILED_FCS_CRC) != 0;

	evt_fill(&e, dv_seq, (u32)(uintptr_t)skb, 0, (s8)(fcs_bad != 0), len,
	    skb->data);
	evt_push(dv_ring, &dv_seq, &e);
	dv_n++;

	/*
	 * The anomaly classes that trigger a snapshot: a single 802.11 frame
	 * cannot exceed ~2346 bytes, so a delivery past 1600 is either an
	 * aggregate that was never split or a length read out of the wrong
	 * bytes; a frame the chip flagged FCS-failed that still reached the
	 * stack means the parser was handed a frame the chip marked bad.  A
	 * short delivery only counts (`short`): 14-byte ACKs and other control
	 * frames are legitimate traffic here (the substitute hands control
	 * frames up like upstream does before it filters them), so they must
	 * not spend the snapshot budget.
	 */
	if (len > 1600U || fcs_bad != 0) {
		anom_n++;
		if (!first_dv_anom_seen) {
			first_dv_anom_seen = 1;
			first_dv_anom = e;
		}
		both_sides_dump(len > 1600U ? "delivery len high" : "delivery fcs");
	} else if (len < 24U) {
		short_n++;
	}
}

int
lkpi_rxtrace_first_anom(u32 *len, u32 *ptr, int *st, u8 head[16])
{

	if (!first_dv_anom_seen)
		return 0;
	if (len != NULL)
		*len = first_dv_anom.len;
	if (ptr != NULL)
		*ptr = first_dv_anom.ptr;
	if (st != NULL)
		*st = (int)first_dv_anom.st;
	if (head != NULL)
		memcpy(head, first_dv_anom.head, 16);
	return 1;
}

void
lkpi_rxtrace_stats(u32 *completions, u32 *deliveries, u32 *anomalies,
    u32 *shorts, u32 *dumps)
{

	if (completions != NULL)
		*completions = ck_n;
	if (deliveries != NULL)
		*deliveries = dv_n;
	if (anomalies != NULL)
		*anomalies = anom_n;
	if (shorts != NULL)
		*shorts = short_n;
	if (dumps != NULL)
		*dumps = dump_n;
}

#endif /* CONFIG_LKPI_RXTRACE */

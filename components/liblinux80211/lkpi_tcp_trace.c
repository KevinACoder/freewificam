/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    lkpi_tcp_trace.c
 * @brief   The acknowledgement path's account: one record per received frame,
 *          reduced into per-boundary latency histograms, plus the congestion
 *          window's two decisive moments.
 *
 * @design  the census pattern this tree already uses (components/linuxkpi/
 *          lkpi_census.h): counters live in the module that owns the subject,
 *          a reset/print pair is called from the image's phase boundary, and
 *          every line starts with `perf census:`. The one deliberate
 *          difference is the resolution: lkpi_census_histo is logarithmic
 *          (2x per bucket), which cannot separate a 300 us boundary from a
 *          560 us one, so the deltas here use fixed 16 us buckets.
 *
 *          Two contexts write this state and they do not share a lock: the
 *          receive thread accounts the boundaries up to the netif input, and
 *          the TCP/IP thread accounts the rest. The frame record in between is
 *          single-threaded by construction (the receive worker runs the whole
 *          bridge call synchronously), so only the histograms and the pending
 *          handoff table need atomic increments.
 *
 * @date    17.10.2026
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "linuxkpi_core.h"
#include "lkpi_census.h"
#include "lkpi_tcp_trace.h"

#if LKPI_TCP_TRACE_ENABLED

/*
 * 16 us buckets over 0..4.095 ms. The boundaries this round must separate are
 * hundreds of microseconds apart (300 us is the Linux control's acknowledgement
 * round trip, 560 us the measured gap), so a bucket edge is the unit of the
 * answer; anything above 4 ms lands in `over` and is reported as such.
 */
#define TRACE_D_BUCKETS		256
#define TRACE_D_STEP_NS		16000ULL
#define TRACE_D_MAX_US		((uint32_t)(TRACE_D_BUCKETS * 16u))

/* Congestion state: one bucket per unit (an MSS for the windows, a segment for
 * the queues). 256 covers the 512 KB send buffer's 359-segment ceiling, which
 * the first boot's 64-bucket table could not (it read ">= 64" for everything
 * that mattered). */
#define TRACE_S_BUCKETS		256

/* How far back the netif-to-tcp_input handoff looks. The mailbox is 128 deep,
 * so 128 entries always cover everything in flight. */
#define TRACE_PEND		256
#define TRACE_PEND_SCAN		128

struct trace_dhist {
	uint32_t	n;
	uint32_t	over;
	uint32_t	bucket[TRACE_D_BUCKETS];
};

struct trace_shist {
	uint32_t	n;
	uint32_t	over;
	uint32_t	bucket[TRACE_S_BUCKETS];
};

struct trace_pend {
	uint32_t	ackno;
	uint32_t	pad;
	uint64_t	t_netif;
};

static struct trace_dhist	g_d[LKPI_TCP_D_N];
static struct trace_shist	g_cwnd, g_ssthresh, g_unacked, g_unsent;
static struct trace_shist	g_sndwnd, g_sndbuf;

static uint32_t	g_ack_netif;	/* pure ACKs that reached the netif input */
static uint32_t	g_ack_tcpin;	/* ... and were matched to a pcb */
static uint32_t	g_ack_adv;	/* ... and advanced lastack */
static uint32_t	g_sent;		/* sent callbacks run */
static uint32_t	g_seg_tx;	/* data segments handed to the netif */
static uint32_t	g_rto;		/* retransmission-timer firings */
static uint32_t	g_nrtx_max;
static uint32_t	g_rto_ms_max;

/* The frame in flight. Written only by the receive thread, which runs the
 * whole bridge call synchronously, so one record is enough. */
static struct {
	uint64_t	t[LKPI_TCP_B_N];
	unsigned int	active;
} g_cur;

/* The USB completion the driver's callback is running under. */
static uint64_t	g_usb_ns;

/* The netif-to-tcp_input handoff: the receive thread appends, the TCP/IP
 * thread matches by acknowledgement number. */
static struct trace_pend	g_pend[TRACE_PEND];
static uint32_t			g_pend_next;	/* writer's position */
static uint32_t			g_pend_w;	/* published position */
/* how many TCP inputs found no matching netif entry: the cross-thread join's
 * own denominator, so a p50 computed over the matches can be read against the
 * population it came from */
static uint32_t			g_pend_miss;

/* The TCP/IP thread's own marks: which acknowledgement just entered, and when
 * the window last moved (the origin of the next transmitted segment). */
static uint64_t	g_last_tcpin_ns;
static uint32_t	g_last_tcpin_ackno;
static uint64_t	g_tx_origin_ns;
/* The same moment kept for the identity-matched path: tx_submit clears
 * g_tx_origin_ns after the first transfer of an acknowledgement, but a second
 * segment released by that acknowledgement still needs its origin. */
static uint64_t	g_ack_adv_ns;

/*
 * The segment in transit. tcp_output_segment announces the segment it is about
 * to hand to ip_output; the netif's linkoutput reads the sequence number out of
 * the frame and claims the announcement, and the bridge, the seam and the USB
 * engine then belong to that same segment. The whole chain runs on the TCP/IP
 * thread - ip_output calls linkoutput, the bridge calls its if_start, and the
 * driver's wake_tx_queue drains in the same call - so one record is exact
 * rather than a race.
 */
static struct {
	uint32_t	seqno;
	uint32_t	len;		/* the announced TCP payload length */
	uint64_t	t_seg_tx;	/* tcp_output_segment announced it */
	uint64_t	t_ack_adv;	/* the acknowledgement that released it */
	uint64_t	t_linkout;
	uint64_t	t_last;		/* the previous milestone of this segment */
	unsigned int	armed;
	unsigned int	seam_seen;	/* the driver seam was reached */
} g_seg;
static uint32_t	g_seg_pend_seqno;	/* announced, not yet matched */
static uint32_t	g_seg_pend_len;
static uint64_t	g_seg_pend_ns;
static unsigned int	g_seg_pend_valid;
static uint32_t	g_ident_hit;		/* TCP frames whose announcement matched */
static uint32_t	g_ident_miss;		/* TCP frames with no announcement */
static uint32_t	g_ident_orphan;		/* a new announcement before the last submit */
static uint32_t	g_ident_stray;		/* submits with no segment armed */

/*
 * The first retransmission of a phase is kept whole: it is the moment the
 * hypothesis is about, and the ring that follows it would overwrite it long
 * before the phase closes.
 */
static struct {
	uint32_t	taken;
	uint32_t	lastack, snd_nxt, cwnd, ssthresh, mss;
	uint32_t	nrtx, unacked, unsent;
	int32_t		rto_ticks;
	uint32_t	back_n;
	uint32_t	back[8];
} g_epi;

/* ------------------------------------------------------------------ */
/* accounting helpers                                                  */
/* ------------------------------------------------------------------ */

static void
dhist_add(struct trace_dhist *h, uint64_t ns)
{
	uint64_t i = ns / TRACE_D_STEP_NS;

	(void)__atomic_fetch_add(&h->n, 1u, __ATOMIC_RELAXED);
	if (i >= TRACE_D_BUCKETS)
		(void)__atomic_fetch_add(&h->over, 1u, __ATOMIC_RELAXED);
	else
		(void)__atomic_fetch_add(&h->bucket[i], 1u, __ATOMIC_RELAXED);
}

static void
shist_add(struct trace_shist *h, uint32_t v)
{
	(void)__atomic_fetch_add(&h->n, 1u, __ATOMIC_RELAXED);
	if (v >= TRACE_S_BUCKETS)
		(void)__atomic_fetch_add(&h->over, 1u, __ATOMIC_RELAXED);
	else
		(void)__atomic_fetch_add(&h->bucket[v], 1u, __ATOMIC_RELAXED);
}

/* The bucket edge at or below the requested percentile, in microseconds. */
static uint32_t
dhist_pct(const struct trace_dhist *h, uint32_t pct)
{
	uint64_t	want, acc = 0;
	uint32_t	i;

	if (h->n == 0u)
		return 0u;
	want = ((uint64_t)h->n * pct) / 100u;
	if (want == 0u)
		want = 1u;
	for (i = 0; i < TRACE_D_BUCKETS; i++) {
		acc += h->bucket[i];
		if (acc >= want)
			return i * (uint32_t)(TRACE_D_STEP_NS / 1000ULL);
	}
	return TRACE_D_MAX_US;
}

static uint32_t
shist_pct(const struct trace_shist *h, uint32_t pct)
{
	uint64_t	want, acc = 0;
	uint32_t	i;

	if (h->n == 0u)
		return 0u;
	want = ((uint64_t)h->n * pct) / 100u;
	if (want == 0u)
		want = 1u;
	for (i = 0; i < TRACE_S_BUCKETS; i++) {
		acc += h->bucket[i];
		if (acc >= want)
			return i;
	}
	return TRACE_S_BUCKETS;
}

static uint32_t
shist_max(const struct trace_shist *h)
{
	uint32_t	i;

	if (h->over != 0u)
		return TRACE_S_BUCKETS;
	for (i = TRACE_S_BUCKETS; i > 0u; i--)
		if (h->bucket[i - 1u] != 0u)
			return i - 1u;
	return 0u;
}

/* Add the delta between two boundaries of the frame in flight, if both were
 * seen. A frame whose first boundary is missing contributes to `n` only
 * through the boundaries it did reach, which the counters report separately. */
static void
delta_add(unsigned int d, unsigned int from, unsigned int to)
{
	if (g_cur.t[from] == 0u || g_cur.t[to] == 0u ||
	    g_cur.t[to] < g_cur.t[from])
		return;
	dhist_add(&g_d[d], g_cur.t[to] - g_cur.t[from]);
}

/* ------------------------------------------------------------------ */
/* the boundaries                                                      */
/* ------------------------------------------------------------------ */

void
lkpi_tcp_trace_usb_done(uint64_t t_ns)
{
	g_usb_ns = t_ns;
}

/*
 * Settled on the thread that ran the driver's callback, so the stamp cannot be
 * overwritten by the next completion before it is read. The freshness bound
 * rejects a stale stamp (an endpoint whose completion carried no data frame):
 * a parse that appears to take milliseconds is not a parse.
 */
void
lkpi_tcp_trace_usb_to_rxq(uint64_t t_ns)
{
	if (g_usb_ns == 0u || t_ns < g_usb_ns || t_ns - g_usb_ns > 5000000ULL)
		return;
	dhist_add(&g_d[LKPI_TCP_D_USB_RXQ], t_ns - g_usb_ns);
}

void
lkpi_tcp_trace_frame_begin(uint64_t t_rxq_in, uint64_t t_rxq_out)
{
	memset(&g_cur, 0, sizeof(g_cur));
	g_cur.active = 1u;
	g_cur.t[LKPI_TCP_B_RXQ_IN] = t_rxq_in;
	g_cur.t[LKPI_TCP_B_RXQ_OUT] = t_rxq_out;
}

void
lkpi_tcp_trace_mark(unsigned int boundary)
{
	if (g_cur.active != 0u && boundary < LKPI_TCP_B_N)
		g_cur.t[boundary] = lkpi_census_now_ns();
}

void
lkpi_tcp_trace_frame_end(void)
{
	g_cur.active = 0u;
}

/*
 * The frame as lwIP sees it: an ethernet header, then IPv4, then TCP. Only a
 * pure acknowledgement - the peer's reply to our data - is accounted; the
 * downlink's data frames cross the same boundaries but answer a different
 * question.
 */
static int
parse_pure_ack(const uint8_t *f, size_t len, uint32_t *ackno, uint32_t *seqno)
{
	const uint8_t	*ip, *tcp;
	unsigned int	ihl, tot, doff;

	if (f == NULL || len < 14u + 20u + 20u)
		return 0;
	if (f[12] != 0x08u || f[13] != 0x00u)	/* IPv4 */
		return 0;
	ip = f + 14;
	if ((ip[0] >> 4) != 4u)
		return 0;
	ihl = (unsigned int)(ip[0] & 0x0fu) * 4u;
	if (ihl < 20u || len < 14u + ihl + 20u)
		return 0;
	if (ip[9] != 6u)			/* TCP */
		return 0;
	tot = ((unsigned int)ip[2] << 8) | (unsigned int)ip[3];
	if (tot < ihl + 20u || tot > len - 14u)
		return 0;
	tcp = ip + ihl;
	doff = (unsigned int)(tcp[12] >> 4) * 4u;
	if (doff < 20u || tot < ihl + doff)
		return 0;
	if (tot != ihl + doff)			/* a payload means data, not an ACK */
		return 0;
	if ((tcp[13] & 0x10u) == 0u)		/* ACK flag */
		return 0;
	*seqno = ((uint32_t)tcp[4] << 24) | ((uint32_t)tcp[5] << 16) |
	    ((uint32_t)tcp[6] << 8) | (uint32_t)tcp[7];
	*ackno = ((uint32_t)tcp[8] << 24) | ((uint32_t)tcp[9] << 16) |
	    ((uint32_t)tcp[10] << 8) | (uint32_t)tcp[11];
	return 1;
}

void
lkpi_tcp_trace_classify(const uint8_t *frame, size_t len)
{
	uint32_t	ackno, seqno, w;

	if (g_cur.active == 0u)
		return;
	g_cur.t[LKPI_TCP_B_NETIF] = lkpi_census_now_ns();
	if (parse_pure_ack(frame, len, &ackno, &seqno) == 0)
		return;

	delta_add(LKPI_TCP_D_RXQ, LKPI_TCP_B_RXQ_IN, LKPI_TCP_B_RXQ_OUT);
	delta_add(LKPI_TCP_D_RXQ_BRIDGE, LKPI_TCP_B_RXQ_OUT,
	    LKPI_TCP_B_BRIDGE);
	delta_add(LKPI_TCP_D_BRIDGE_NETIF, LKPI_TCP_B_BRIDGE,
	    LKPI_TCP_B_NETIF);
	(void)__atomic_fetch_add(&g_ack_netif, 1u, __ATOMIC_RELAXED);

	/* hand it to the TCP/IP thread's half of the account. The acknowledgement
	 * number is the only key both halves share - the mailbox hop loses the
	 * frame's identity - so the join is by ackno, and a miss is counted rather
	 * than silently dropped. The entry is written before the position that
	 * publishes it, and the reader loads that position with acquire. */
	w = g_pend_next % TRACE_PEND;
	g_pend[w].ackno = ackno;
	g_pend[w].t_netif = g_cur.t[LKPI_TCP_B_NETIF];
	g_pend_next++;
	__atomic_store_n(&g_pend_w, g_pend_next, __ATOMIC_RELEASE);
	(void)seqno;
}

void
lkpi_tcp_trace_tcp_in(uint32_t ackno, uint32_t seqno, uint32_t len)
{
	uint64_t	now = lkpi_census_now_ns();
	uint32_t	w, j;

	(void)seqno;
	(void)__atomic_fetch_add(&g_ack_tcpin, 1u, __ATOMIC_RELAXED);
	g_last_tcpin_ns = now;
	g_last_tcpin_ackno = ackno;

	w = __atomic_load_n(&g_pend_w, __ATOMIC_ACQUIRE);
	for (j = 1u; j <= TRACE_PEND_SCAN; j++) {
		uint32_t i = (w - j) % TRACE_PEND;

		if (g_pend[i].ackno != ackno || g_pend[i].t_netif == 0u)
			continue;
		if (now > g_pend[i].t_netif)
			dhist_add(&g_d[LKPI_TCP_D_NETIF_TCPIN],
			    now - g_pend[i].t_netif);
		g_pend[i].ackno = 0u;
		g_pend[i].t_netif = 0u;
		return;
	}
	/* Only a segment that could have been classified at the netif input is a
	 * miss: a payload-carrying segment was never entered in the table. */
	if (len == 0u)
		(void)__atomic_fetch_add(&g_pend_miss, 1u, __ATOMIC_RELAXED);
}

void
lkpi_tcp_trace_ack_adv(uint32_t ackno, uint32_t acked)
{
	uint64_t	now = lkpi_census_now_ns();

	(void)acked;
	(void)__atomic_fetch_add(&g_ack_adv, 1u, __ATOMIC_RELAXED);
	if (g_last_tcpin_ns != 0u && g_last_tcpin_ackno == ackno &&
	    now >= g_last_tcpin_ns)
		dhist_add(&g_d[LKPI_TCP_D_TCPIN_ADV], now - g_last_tcpin_ns);
	/* the segment this acknowledgement unblocks is submitted from here */
	g_tx_origin_ns = now;
	g_ack_adv_ns = now;
}

/*
 * The congestion state is sampled here, not at the acknowledgement advance:
 * tcp_in.c moves lastack before it frees the acknowledged segments and before
 * it grows cwnd, so a sample there would report one segment more in flight than
 * there is and the window before its update. This hook runs after tcp_process
 * has returned, with the queues settled.
 */
void
lkpi_tcp_trace_sent(uint32_t ackno, uint32_t cwnd, uint32_t ssthresh,
    uint32_t mss, uint32_t nrtx, uint32_t unacked, uint32_t unsent,
    uint32_t snd_wnd, uint32_t snd_buf, int32_t rto_ticks)
{
	(void)ackno;
	(void)__atomic_fetch_add(&g_sent, 1u, __ATOMIC_RELAXED);
	shist_add(&g_cwnd, mss != 0u ? cwnd / mss : 0u);
	shist_add(&g_ssthresh, mss != 0u ? ssthresh / mss : 0u);
	shist_add(&g_unacked, unacked);
	shist_add(&g_unsent, unsent);
	/*
	 * The peer's advertised window is the one limit this instrument had no
	 * reading for: a sender whose own cwnd is wide open but whose snd_wnd is
	 * one MSS produces exactly the observed "one segment per acknowledgement"
	 * pattern without being slow at all.
	 */
	shist_add(&g_sndwnd, mss != 0u ? snd_wnd / mss : 0u);
	shist_add(&g_sndbuf, mss != 0u ? snd_buf / mss : 0u);
	if (nrtx > g_nrtx_max)
		g_nrtx_max = nrtx;
	if (rto_ticks > 0 && (uint32_t)rto_ticks * 500u > g_rto_ms_max)
		g_rto_ms_max = (uint32_t)rto_ticks * 500u;
}

void
lkpi_tcp_trace_seg_tx(uint32_t seqno, uint32_t len)
{
	uint64_t	now = lkpi_census_now_ns();

	(void)__atomic_fetch_add(&g_seg_tx, 1u, __ATOMIC_RELAXED);
	if (len == 0u) {
		/* a pure acknowledgement: it carries no sequence space of its own
		 * and must not claim the next data frame */
		return;
	}
	g_seg_pend_seqno = seqno;
	g_seg_pend_len = len;
	g_seg_pend_ns = now;
	g_seg_pend_valid = 1u;
	if (g_tx_origin_ns == 0u || now < g_tx_origin_ns ||
	    now - g_tx_origin_ns >= 5000000ULL)
		return;
	dhist_add(&g_d[LKPI_TCP_D_ADV_TX], now - g_tx_origin_ns);
	/* the origin stays set: the same acknowledgement's segment is measured
	 * again where it reaches the engine, so the two halves of the transmit
	 * path can be told apart. tx_submit clears it. */
}

/*
 * The netif is about to hand a frame down. Its TCP sequence number is what
 * makes the following milestones belong to this segment rather than to the last
 * acknowledgement: the announcement tcp_output_segment made is claimed here, and
 * a frame whose number does not match it is counted, never guessed at.
 *
 * A frame that is not IPv4/TCP (ARP, ICMP, a management frame) is not part of
 * the population and is left alone.
 */
void
lkpi_tcp_trace_tx_frame(const uint8_t *frame, size_t len)
{
	uint32_t	seqno, payload, ip_len, thl;
	uint64_t	now = lkpi_census_now_ns();
	const uint8_t	*ip;
	size_t		ihl;

	if (frame == NULL || len < 14u + 20u + 20u)
		return;
	if ((((uint32_t)frame[12] << 8) | frame[13]) != 0x0800u)
		return;
	ip = frame + 14;
	if ((ip[0] >> 4) != 4u || ip[9] != 6u)
		return;
	ihl = (size_t)(ip[0] & 0x0fu) * 4u;
	if (ihl < 20u || len < 14u + ihl + 20u)
		return;
	ip_len = ((uint32_t)ip[2] << 8) | (uint32_t)ip[3];
	thl = (uint32_t)(ip[ihl + 12] >> 4) * 4u;
	if (ip_len < ihl + thl)
		return;
	/* A pure acknowledgement carries no sequence space and is not part of the
	 * population; only a data frame can match an announcement. */
	payload = ip_len - (uint32_t)ihl - thl;
	if (payload == 0u)
		return;
	seqno = ((uint32_t)ip[ihl + 4] << 24) | ((uint32_t)ip[ihl + 5] << 16) |
	    ((uint32_t)ip[ihl + 6] << 8) | (uint32_t)ip[ihl + 7];
	/* Sequence number and payload length together: a retransmission carries
	 * both unchanged, while a different segment or a different flow does not. */
	if (!g_seg_pend_valid || g_seg_pend_seqno != seqno ||
	    g_seg_pend_len != payload ||
	    now < g_seg_pend_ns || now - g_seg_pend_ns > 5000000ULL) {
		/* announced but never reached the netif, or a frame this
		 * instrument cannot attribute: both are misses of the join */
		g_ident_miss++;
		g_seg_pend_valid = 0u;
		return;
	}
	if (g_seg.armed)
		g_ident_orphan++;
	g_seg.seqno = seqno;
	g_seg.len = payload;
	g_seg.t_seg_tx = g_seg_pend_ns;
	g_seg.t_ack_adv = g_ack_adv_ns;
	g_seg.t_linkout = now;
	g_seg.t_last = now;
	g_seg.armed = 1u;
	g_seg.seam_seen = 0u;
	g_seg_pend_valid = 0u;
	dhist_add(&g_d[LKPI_TCP_D_SEG_LINKOUT], now - g_seg.t_seg_tx);
	if (g_seg.t_ack_adv != 0u && now >= g_seg.t_ack_adv &&
	    now - g_seg.t_ack_adv <= 5000000ULL)
		dhist_add(&g_d[LKPI_TCP_D_ADV_LINKOUT],
		    now - g_seg.t_ack_adv);
}

/*
 * The transmit side's far end. Everything between the netif's linkoutput and
 * this call - the bridge's encap and crypto, the driver's descriptor build and
 * the engine's queue - is what this interval covers.
 */
void
lkpi_tcp_trace_tx_submit(uint64_t t_ns)
{
	if (!g_seg.armed || !g_seg.seam_seen) {
		/* a bulk-out transfer that no data segment announced, or one that
		 * was submitted between this segment's linkoutput and its seam
		 * (a management frame drained first): neither may consume the
		 * record, or the interval would belong to the wrong frame. */
		g_ident_stray++;
		goto legacy;
	}
	if (t_ns >= g_seg.t_last)
		dhist_add(&g_d[LKPI_TCP_D_SEG_SUBMIT], t_ns - g_seg.t_last);
	if (t_ns >= g_seg.t_seg_tx)
		dhist_add(&g_d[LKPI_TCP_D_SEG_TOTAL], t_ns - g_seg.t_seg_tx);
	g_ident_hit++;
	g_seg.armed = 0u;
legacy:
	if (g_tx_origin_ns == 0u || t_ns < g_tx_origin_ns ||
	    t_ns - g_tx_origin_ns > 5000000ULL)
		return;
	dhist_add(&g_d[LKPI_TCP_D_ADV_SUBMIT], t_ns - g_tx_origin_ns);
	g_tx_origin_ns = 0u;
}

/*
 * The transmit path's intermediate points, each measured from the previous
 * milestone of the same segment.
 */
void
lkpi_tcp_trace_tx_mark(unsigned int boundary)
{
	uint64_t	t_ns = lkpi_census_now_ns();
	unsigned int	d;

	if (!g_seg.armed || t_ns < g_seg.t_last)
		return;
	switch (boundary) {
	case LKPI_TCP_TX_BRIDGE:
		d = LKPI_TCP_D_SEG_BRIDGE;
		break;
	case LKPI_TCP_TX_ENCAP_IN:
		d = LKPI_TCP_D_SEG_ENCAP_IN;
		break;
	case LKPI_TCP_TX_ENCAP_DONE:
		d = LKPI_TCP_D_SEG_ENCAP;
		break;
	case LKPI_TCP_TX_CRYPTO_DONE:
		d = LKPI_TCP_D_SEG_CRYPTO;
		break;
	case LKPI_TCP_TX_SEAM:
		d = LKPI_TCP_D_SEG_SEAM;
		g_seg.seam_seen = 1u;
		break;
	default:
		return;
	}
	dhist_add(&g_d[d], t_ns - g_seg.t_last);
	g_seg.t_last = t_ns;
}

void
lkpi_tcp_trace_rto(uint32_t lastack, uint32_t snd_nxt, uint32_t cwnd,
    uint32_t ssthresh, uint32_t mss, uint32_t nrtx, uint32_t unacked,
    uint32_t unsent, int32_t rto_ticks)
{
	uint32_t	i, w;

	(void)__atomic_fetch_add(&g_rto, 1u, __ATOMIC_RELAXED);
	if (rto_ticks > 0 && (uint32_t)rto_ticks * 500u > g_rto_ms_max)
		g_rto_ms_max = (uint32_t)rto_ticks * 500u;
	if (g_epi.taken != 0u)
		return;
	g_epi.taken = 1u;
	g_epi.lastack = lastack;
	g_epi.snd_nxt = snd_nxt;
	g_epi.cwnd = cwnd;
	g_epi.ssthresh = ssthresh;
	g_epi.mss = mss;
	g_epi.nrtx = nrtx;
	g_epi.unacked = unacked;
	g_epi.unsent = unsent;
	g_epi.rto_ticks = rto_ticks;
	/* the acknowledgements that did arrive just before the timer fired */
	w = __atomic_load_n(&g_pend_w, __ATOMIC_RELAXED);
	g_epi.back_n = 0u;
	for (i = 1u; i <= 8u; i++) {
		uint32_t k = (w - i) % TRACE_PEND;

		if (g_pend[k].ackno == 0u)
			continue;
		g_epi.back[g_epi.back_n++] = g_pend[k].ackno;
	}
}

/* ------------------------------------------------------------------ */
/* the phase face                                                      */
/* ------------------------------------------------------------------ */

void
lkpi_tcp_trace_reset(void)
{
	memset(g_d, 0, sizeof(g_d));
	memset(&g_cwnd, 0, sizeof(g_cwnd));
	memset(&g_ssthresh, 0, sizeof(g_ssthresh));
	memset(&g_unacked, 0, sizeof(g_unacked));
	memset(&g_unsent, 0, sizeof(g_unsent));
	memset(&g_sndwnd, 0, sizeof(g_sndwnd));
	memset(&g_sndbuf, 0, sizeof(g_sndbuf));
	memset(&g_cur, 0, sizeof(g_cur));
	memset(&g_epi, 0, sizeof(g_epi));
	memset(g_pend, 0, sizeof(g_pend));
	g_ack_netif = 0u;
	g_ack_tcpin = 0u;
	g_ack_adv = 0u;
	g_sent = 0u;
	g_seg_tx = 0u;
	g_rto = 0u;
	g_nrtx_max = 0u;
	g_rto_ms_max = 0u;
	g_pend_w = 0u;
	g_pend_next = 0u;
	g_pend_miss = 0u;
	g_usb_ns = 0u;
	g_last_tcpin_ns = 0u;
	g_last_tcpin_ackno = 0u;
	g_tx_origin_ns = 0u;
	g_ack_adv_ns = 0u;
	memset(&g_seg, 0, sizeof(g_seg));
	g_seg_pend_seqno = 0u;
	g_seg_pend_len = 0u;
	g_seg_pend_ns = 0u;
	g_seg_pend_valid = 0u;
	g_ident_hit = 0u;
	g_ident_miss = 0u;
	g_ident_orphan = 0u;
	g_ident_stray = 0u;
}

void
lkpi_tcp_trace_print(void)
{
	lkpi_printf(ULOG_INFO_LEVEL,
	    "perf census: C ack lat n=%u usb_rxq=%u/%u rxq=%u/%u rxq_br=%u/%u "
	    "br_netif=%u/%u netif_tcp=%u/%u tcp_adv=%u/%u adv_tx=%u/%u "
	    "adv_sub=%u/%u over=%u\n",
	    (unsigned int)g_ack_netif,
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_USB_RXQ], 50),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_USB_RXQ], 90),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_RXQ], 50),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_RXQ], 90),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_RXQ_BRIDGE], 50),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_RXQ_BRIDGE], 90),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_BRIDGE_NETIF], 50),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_BRIDGE_NETIF], 90),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_NETIF_TCPIN], 50),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_NETIF_TCPIN], 90),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_TCPIN_ADV], 50),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_TCPIN_ADV], 90),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_ADV_TX], 50),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_ADV_TX], 90),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_ADV_SUBMIT], 50),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_ADV_SUBMIT], 90),
	    (unsigned int)(g_d[LKPI_TCP_D_USB_RXQ].over +
	    g_d[LKPI_TCP_D_RXQ].over + g_d[LKPI_TCP_D_RXQ_BRIDGE].over +
	    g_d[LKPI_TCP_D_BRIDGE_NETIF].over +
	    g_d[LKPI_TCP_D_NETIF_TCPIN].over + g_d[LKPI_TCP_D_TCPIN_ADV].over +
	    g_d[LKPI_TCP_D_ADV_TX].over + g_d[LKPI_TCP_D_ADV_SUBMIT].over));
	lkpi_printf(ULOG_INFO_LEVEL,
	    "perf census: C tx path linkout=%u/%u bridge=%u/%u encap=%u/%u "
	    "crypto=%u/%u seam=%u/%u submit=%u/%u n=%u over=%u\n",
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_SEG_LINKOUT], 50),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_SEG_LINKOUT], 90),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_SEG_BRIDGE], 50),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_SEG_BRIDGE], 90),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_SEG_ENCAP], 50),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_SEG_ENCAP], 90),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_SEG_CRYPTO], 50),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_SEG_CRYPTO], 90),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_SEG_SEAM], 50),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_SEG_SEAM], 90),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_SEG_SUBMIT], 50),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_SEG_SUBMIT], 90),
	    (unsigned int)g_ident_hit,
	    (unsigned int)(g_d[LKPI_TCP_D_SEG_LINKOUT].over +
	    g_d[LKPI_TCP_D_SEG_BRIDGE].over +
	    g_d[LKPI_TCP_D_SEG_ENCAP_IN].over +
	    g_d[LKPI_TCP_D_SEG_ENCAP].over + g_d[LKPI_TCP_D_SEG_CRYPTO].over +
	    g_d[LKPI_TCP_D_SEG_SEAM].over + g_d[LKPI_TCP_D_SEG_SUBMIT].over));
	/*
	 * The join's own denominator. `hit` counts TCP data frames whose sequence
	 * number matched the announcement tcp_output_segment made; `miss` counts
	 * the ones it could not match. A p50 over the matched population is only
	 * readable next to the fraction it came from - and a low fraction means
	 * the transmit split cannot be claimed at all.
	 */
	lkpi_printf(ULOG_INFO_LEVEL,
	    "perf census: C tx ident hit=%u miss=%u orphan=%u stray=%u pct=%u "
	    "total=%u/%u adv=%u/%u\n",
	    (unsigned int)g_ident_hit, (unsigned int)g_ident_miss,
	    (unsigned int)g_ident_orphan, (unsigned int)g_ident_stray,
	    (unsigned int)((g_ident_hit + g_ident_miss) != 0u ?
	    (g_ident_hit * 100u) / (g_ident_hit + g_ident_miss) : 0u),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_SEG_TOTAL], 50),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_SEG_TOTAL], 90),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_ADV_LINKOUT], 50),
	    (unsigned int)dhist_pct(&g_d[LKPI_TCP_D_ADV_LINKOUT], 90));
	/*
	 * The per-boundary sample counts. Without them a p50 of 0 is ambiguous:
	 * it reads the same for "no sample matched" and for "every sample landed
	 * in the first 16 us bucket", and the first boot could not tell the two
	 * apart on exactly the boundary the round was about.
	 */
	lkpi_printf(ULOG_INFO_LEVEL,
	    "perf census: C ack latn usb_rxq=%u rxq=%u rxq_br=%u br_netif=%u "
	    "netif_tcp=%u tcp_adv=%u adv_tx=%u adv_sub=%u\n",
	    (unsigned int)g_d[LKPI_TCP_D_USB_RXQ].n,
	    (unsigned int)g_d[LKPI_TCP_D_RXQ].n,
	    (unsigned int)g_d[LKPI_TCP_D_RXQ_BRIDGE].n,
	    (unsigned int)g_d[LKPI_TCP_D_BRIDGE_NETIF].n,
	    (unsigned int)g_d[LKPI_TCP_D_NETIF_TCPIN].n,
	    (unsigned int)g_d[LKPI_TCP_D_TCPIN_ADV].n,
	    (unsigned int)g_d[LKPI_TCP_D_ADV_TX].n,
	    (unsigned int)g_d[LKPI_TCP_D_ADV_SUBMIT].n);
	lkpi_printf(ULOG_INFO_LEVEL,
	    "perf census: C ack see netif=%u tcpin=%u adv=%u sent=%u segtx=%u "
	    "rto=%u usb_rxq_n=%u pend_miss=%u\n", (unsigned int)g_ack_netif,
	    (unsigned int)g_ack_tcpin, (unsigned int)g_ack_adv,
	    (unsigned int)g_sent, (unsigned int)g_seg_tx, (unsigned int)g_rto,
	    (unsigned int)g_d[LKPI_TCP_D_USB_RXQ].n,
	    (unsigned int)g_pend_miss);
	lkpi_printf(ULOG_INFO_LEVEL,
	    "perf census: C tcp state n=%u cwnd=%u/%u ssthresh=%u/%u nrtx_max=%u "
	    "unacked=%u/%u unsent=%u/%u sndwnd=%u/%u sndbuf=%u/%u rto_ms=%u "
	    "over=%u\n",
	    (unsigned int)g_cwnd.n,
	    (unsigned int)shist_pct(&g_cwnd, 50), (unsigned int)shist_max(&g_cwnd),
	    (unsigned int)shist_pct(&g_ssthresh, 50),
	    (unsigned int)shist_max(&g_ssthresh), (unsigned int)g_nrtx_max,
	    (unsigned int)shist_pct(&g_unacked, 50),
	    (unsigned int)shist_max(&g_unacked),
	    (unsigned int)shist_pct(&g_unsent, 50),
	    (unsigned int)shist_max(&g_unsent),
	    (unsigned int)shist_pct(&g_sndwnd, 50),
	    (unsigned int)shist_max(&g_sndwnd),
	    (unsigned int)shist_pct(&g_sndbuf, 50),
	    (unsigned int)shist_max(&g_sndbuf), (unsigned int)g_rto_ms_max,
	    (unsigned int)(g_cwnd.over + g_ssthresh.over + g_unacked.over +
	    g_unsent.over + g_sndwnd.over + g_sndbuf.over));
	if (g_epi.taken != 0u) {
		lkpi_printf(ULOG_INFO_LEVEL,
		    "perf census: C tcp rto lastack=%u snd_nxt=%u cwnd=%u "
		    "ssthresh=%u mss=%u nrtx=%u unacked=%u unsent=%u rto_ms=%u\n",
		    (unsigned int)g_epi.lastack, (unsigned int)g_epi.snd_nxt,
		    (unsigned int)g_epi.cwnd, (unsigned int)g_epi.ssthresh,
		    (unsigned int)g_epi.mss, (unsigned int)g_epi.nrtx,
		    (unsigned int)g_epi.unacked, (unsigned int)g_epi.unsent,
		    (unsigned int)(g_epi.rto_ticks > 0 ?
		    (uint32_t)g_epi.rto_ticks * 500u : 0u));
		lkpi_printf(ULOG_INFO_LEVEL,
		    "perf census: C tcp rto back ack=%u,%u,%u,%u,%u,%u,%u,%u\n",
		    (unsigned int)g_epi.back[0], (unsigned int)g_epi.back[1],
		    (unsigned int)g_epi.back[2], (unsigned int)g_epi.back[3],
		    (unsigned int)g_epi.back[4], (unsigned int)g_epi.back[5],
		    (unsigned int)g_epi.back[6], (unsigned int)g_epi.back[7]);
	}
}

#endif /* LKPI_TCP_TRACE_ENABLED */

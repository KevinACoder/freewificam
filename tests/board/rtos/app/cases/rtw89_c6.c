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

#ifdef CONFIG_LKPI_CENSUS
#include "rtos_stats.h"
#include <linux/rhashtable.h>
extern unsigned int log_anchor_call_count(void);
extern unsigned long long log_emit_byte_count(void);
extern unsigned int log_ring_count(void);
extern unsigned int log_ring_line(unsigned int idx, char *out,
    unsigned int max);
#endif

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"
#include "linux80211/nl80211_probe.h"
#include "linux80211/net.h"
#include "linux80211/hostap_ap.h"
#include "lkpi_usb_cherryusb.h"

#include <linux/netdevice.h>
#include <net/net_namespace.h>

#ifdef CONFIG_LKPI_MEMCMP_TRACE
/*
 * The receive worker that burns its window inside the C library's compare
 * loop needs a name for the structure it is walking.  The caller's return
 * address is that name (symbolise with the ELF the boot ran), and the largest
 * length says whether the loop is many small compares or one long scan.
 */
#define LKPI_MCMP_MAX	8u

struct lkpi_mcmp_row {
	void		*ra;
	unsigned long	calls;
	unsigned long	max_n;
};

static struct lkpi_mcmp_row mcmp_rows[LKPI_MCMP_MAX];
static unsigned long mcmp_calls;
static unsigned long long mcmp_bytes;

int __real_memcmp(const void *a, const void *b, size_t n);

int
__wrap_memcmp(const void *a, const void *b, size_t n)
{
	void *ra = __builtin_return_address(0);
	os_critical_state_t st = osCriticalEnter();
	unsigned int i;

	for (i = 0u; i < LKPI_MCMP_MAX; i++) {
		if (mcmp_rows[i].ra == ra) {
			mcmp_rows[i].calls++;
			if (n > mcmp_rows[i].max_n)
				mcmp_rows[i].max_n = (unsigned long)n;
			goto out;
		}
		if (mcmp_rows[i].ra == NULL) {
			mcmp_rows[i].ra = ra;
			mcmp_rows[i].calls = 1u;
			mcmp_rows[i].max_n = (unsigned long)n;
			goto out;
		}
	}
out:
	mcmp_calls++;
	mcmp_bytes += n;
	osCriticalExit(st);
	return __real_memcmp(a, b, n);
}

static void
lkpi_mcmp_reset(void)
{
	unsigned int i;

	for (i = 0u; i < LKPI_MCMP_MAX; i++) {
		mcmp_rows[i].ra = NULL;
		mcmp_rows[i].calls = 0u;
		mcmp_rows[i].max_n = 0u;
	}
	mcmp_calls = 0u;
	mcmp_bytes = 0u;
}

static unsigned int
lkpi_mcmp_get(struct lkpi_mcmp_row *out, unsigned int max,
    unsigned long *calls, unsigned long long *bytes)
{
	unsigned int i;

	*calls = mcmp_calls;
	*bytes = mcmp_bytes;
	for (i = 0u; i < LKPI_MCMP_MAX && i < max; i++)
		out[i] = mcmp_rows[i];
	return i;
}
#endif

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

#ifdef CONFIG_LKPI_CENSUS
#define C6_TASK_MAX	24u

static const char *
c6_task_state(uint32_t st)
{
	switch (st) {
	case RTOS_TASK_RUNNING:		return "run";
	case RTOS_TASK_READY:		return "ready";
	case RTOS_TASK_BLOCKED:		return "block";
	case RTOS_TASK_SUSPENDED:	return "susp";
	case RTOS_TASK_DELETED:		return "del";
	default:			return "?";
	}
}

/*
 * Who held the core during this phase.  The freeze the C6T round is chasing
 * leaves two workers READY with a body marked running, which means something
 * above them never blocked; the run-time counters name it, and the tick PC
 * sampler says what that task was executing.  Addresses are image addresses
 * (symbolise with the ELF the boot ran).
 */
static void
c6_task_census(unsigned phase, const struct rtos_task_stat *a, unsigned int na,
    const struct rtos_task_stat *b, unsigned int nb)
{
	uint32_t delta[C6_TASK_MAX];
	unsigned int i, j, k, shown = 0u;
	int top = -1;

	for (i = 0u; i < nb && i < C6_TASK_MAX; i++) {
		delta[i] = 0u;
		for (j = 0u; j < na; j++) {
			if (a[j].number == b[i].number) {
				delta[i] = b[i].runtime - a[j].runtime;
				break;
			}
		}
	}
	for (k = 0u; k < 5u; k++) {
		unsigned int best = nb;

		for (i = 0u; i < nb && i < C6_TASK_MAX; i++) {
			if (delta[i] == 0u)
				continue;
			if (best == nb || delta[i] > delta[best])
				best = i;
		}
		if (best == nb)
			break;
		if (k == 0u)
			top = (int)best;
		CASE_ANCHOR("rtw89_c6: task phase %u name=%s st=%s prio=%u "
			    "ticks=%u stack=%u\n", phase,
			    b[best].name != NULL ? b[best].name : "?",
			    c6_task_state(b[best].state), b[best].prio,
			    delta[best], b[best].stack_free);
		delta[best] = 0u;
		shown++;
	}
	if (shown == 0u)
		CASE_ANCHOR("rtw89_c6: task phase %u none\n", phase);
	if (top >= 0 && b[top].handle != NULL) {
		struct rtos_pc_hit hits[4];
		unsigned int nh = rtos_pc_top(b[top].handle, hits, 4u);
		unsigned int q;

		for (q = 0u; q < nh; q++)
			CASE_ANCHOR("rtw89_c6: hogpc phase %u name=%s "
				    "pc=0x%llx n=%u\n", phase,
				    b[top].name != NULL ? b[top].name : "?",
				    (unsigned long long)hits[q].pc,
				    hits[q].count);
		{
			uint64_t vals[6];
			uint32_t offs[6];
			unsigned int nv;

			CASE_ANCHOR("rtw89_c6: hogsaved phase %u name=%s "
				    "pc=0x%llx\n", phase,
				    b[top].name != NULL ? b[top].name : "?",
				    (unsigned long long)rtos_task_saved_pc(
					b[top].handle));
			nv = rtos_task_frame_scan(b[top].handle, vals, offs, 6u);
			for (q = 0u; q < nv; q++)
				CASE_ANCHOR("rtw89_c6: hogframe phase %u "
					    "off=%u ra=0x%llx\n", phase,
					    offs[q],
					    (unsigned long long)vals[q]);
		}
	}
}
static struct rtos_task_stat c6_task_begin[C6_TASK_MAX];
static unsigned int c6_task_begin_n;
static unsigned int c6_log_calls_begin;
static unsigned long long c6_log_bytes_begin;
#endif

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
#ifdef CONFIG_LKPI_CENSUS
	{
		struct rtos_task_stat st[C6_TASK_MAX];
		unsigned int nst;

		nst = rtos_task_stats_get(st, C6_TASK_MAX);
		(void)nst;
		/* kept for the end-of-phase diff */
		memcpy(c6_task_begin, st, sizeof(st));
		c6_task_begin_n = nst;
		c6_log_calls_begin = log_anchor_call_count();
		c6_log_bytes_begin = log_emit_byte_count();
#ifdef CONFIG_LKPI_MEMCMP_TRACE
		lkpi_mcmp_reset();
		lkpi_printf_trace_reset();
#endif
	}
#endif
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
	CASE_ANCHOR("rtw89_c6: proto phase %u rx_ip=%lu arp_req=%lu "
		    "arp_rep=%lu other=%lu foreign=%lu eapol=%lu\n",
		    n,
		    after.diag.rx_ip - before.diag.rx_ip,
		    after.diag.rx_arp_req - before.diag.rx_arp_req,
		    after.diag.rx_arp_rep - before.diag.rx_arp_rep,
		    after.diag.rx_other - before.diag.rx_other,
		    after.diag.rx_foreign - before.diag.rx_foreign,
		    after.diag.rx_eapol - before.diag.rx_eapol);
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
#ifdef CONFIG_LKPI_SKBQ_DIAG
	{
		/* The queue census above names the owner by address only.  This
		 * block answers the two questions the C6T stall left open: whose
		 * queue is it (the first caller's return address, resolved with
		 * addr2line offline) and does `qlen` still match the chain.  The
		 * frame head tells an 802.11 MPDU from a transport buffer. */
		struct lkpi_skbq_owner owners[LKPI_SKBQ_OWNER_MAX];
		struct lkpi_skbq_census cen[LKPI_SKBQ_CENSUS_MAX];
		struct lkpi_wq_census wqs[LKPI_WQ_CENSUS_MAX];
		struct lkpi_wq_stats wst;
		struct lkpi_timer_stats tst;
		unsigned int no, nw, i;

		lkpi_timer_stats_get(&tst);
		CASE_ANCHOR("rtw89_c6: timer phase %u mods=%u fail=%u "
			    "fires=%u setups=%u new_fail=%u\n", n,
			    tst.mods, tst.mod_fail, tst.fires, tst.setups,
			    tst.new_fail);
		CASE_ANCHOR("rtw89_c6: rht phase %u walk_max=%u cycles=%u\n",
			    n, lkpi_rht_walk_max(), lkpi_rht_cycle_hits());
#ifdef CONFIG_LKPI_MEMCMP_TRACE
		{
			struct lkpi_mcmp_row mc[LKPI_MCMP_MAX];
			unsigned long calls = 0u;
			unsigned long long bytes = 0u;
			unsigned int nr = lkpi_mcmp_get(mc, LKPI_MCMP_MAX,
			    &calls, &bytes);
			unsigned int q;

			{
				struct lkpi_printf_site ps[8];
				unsigned long pcalls = 0u;
				unsigned int nps = lkpi_printf_trace_get(
				    ps, 8u, &pcalls);

				CASE_ANCHOR("rtw89_c6: pf phase %u calls=%lu\n",
					    n, pcalls);
				for (q = 0u; q < nps; q++) {
					if (ps[q].ra == NULL)
						break;
					CASE_ANCHOR("rtw89_c6: pfra phase %u "
						    "ra=%p n=%lu\n", n,
						    ps[q].ra, ps[q].calls);
				}
			}
			CASE_ANCHOR("rtw89_c6: mcmp phase %u calls=%lu "
				    "bytes=%lu\n", n, calls,
				    (unsigned long)bytes);
			for (q = 0u; q < nr; q++) {
				if (mc[q].ra == NULL)
					break;
				CASE_ANCHOR("rtw89_c6: mcmpra phase %u ra=%p "
					    "n=%lu maxn=%lu\n", n, mc[q].ra,
					    mc[q].calls, mc[q].max_n);
			}
		}
#endif
		for (i = 0u; i < 4u; i++) {
			if (tst.fail_fn[i] == NULL)
				break;
			CASE_ANCHOR("rtw89_c6: tfail phase %u fn=%p n=%u\n",
				    n, tst.fail_fn[i], tst.fail_fn_n[i]);
		}
		lkpi_wq_stats_get(&wst);
		CASE_ANCHOR("rtw89_c6: wqstat phase %u direct=%u full=%u "
			    "state=%s cur=%p\n", n, wst.direct, wst.full,
			    lkpi_wq_state(), lkpi_wq_current_work_func());
		nw = lkpi_wq_census(wqs, LKPI_WQ_CENSUS_MAX);
		for (i = 0u; i < nw; i++)
			CASE_ANCHOR("rtw89_c6: wq phase %u name=%s runs=%u "
				    "refused=%u depth=%u busy=%u cur=%p "
				    "stack=%u st=%u\n", n, wqs[i].name,
				    wqs[i].runs, wqs[i].refused, wqs[i].depth,
				    wqs[i].busy, wqs[i].cur, wqs[i].stack_free,
				    wqs[i].thread_state);
		if (lkpi_skb_queue_census(cen, LKPI_SKBQ_CENSUS_MAX) > 0u) {
			/* The census row is an owner proven by live skbs, so this
			 * probe is safe even when the head is a dead stack or
			 * freed-queue address: only the counter and the first
			 * frame's bytes are read.  The frame head is the identity
			 * test: 802.11 data starts with an FC word, an rtw89 URB
			 * buffer starts with the chip's RX descriptor. */
			struct lkpi_skbq_owner probe;

			(void)lkpi_skbq_probe(cen[0].q, &probe);
			CASE_ANCHOR("rtw89_c6: qprobe phase %u q=%p qlen=%u "
				    "owned=%u len=%u fc=%02x%02x "
				    "head=%02x%02x%02x%02x%02x%02x%02x%02x "
				    "%02x%02x%02x%02x%02x%02x%02x%02x\n",
				    n, probe.q, probe.qlen, probe.owned,
				    probe.first_len,
				    probe.first_head[0], probe.first_head[1],
				    probe.first_head[0], probe.first_head[1],
				    probe.first_head[2], probe.first_head[3],
				    probe.first_head[4], probe.first_head[5],
				    probe.first_head[6], probe.first_head[7],
				    probe.first_head[8], probe.first_head[9],
				    probe.first_head[10], probe.first_head[11],
				    probe.first_head[12], probe.first_head[13],
				    probe.first_head[14], probe.first_head[15]);
		}
		no = lkpi_skbq_owner_dump(owners, LKPI_SKBQ_OWNER_MAX);
		CASE_ANCHOR("rtw89_c6: skbq rows=%u\n", no);
		for (i = 0u; i < no && i < 6u; i++) {
			if (owners[i].owned == 0u && owners[i].qlen == 0u)
				continue;
			CASE_ANCHOR("rtw89_c6: skbq phase %u q=%p qlen=%u "
				    "owned=%u enq=%u deq=%u len=%u/%u/%u "
				    "first_len=%u head=%02x%02x%02x%02x "
				    "enq_ra=%p deq_ra=%p\n",
				    n, owners[i].q, owners[i].qlen,
				    owners[i].owned, owners[i].enqs,
				    owners[i].deqs, owners[i].len_min,
				    owners[i].len_avg, owners[i].len_max,
				    owners[i].first_len,
				    owners[i].first_head[0],
				    owners[i].first_head[1],
				    owners[i].first_head[2],
				    owners[i].first_head[3],
				    owners[i].enq_ra, owners[i].deq_ra);
		}
	}
#endif
#ifdef CONFIG_LKPI_CENSUS
	{
		struct rtos_task_stat st[C6_TASK_MAX];
		unsigned int nst = rtos_task_stats_get(st, C6_TASK_MAX);
		unsigned int calls = log_anchor_call_count();
		unsigned long long bytes = log_emit_byte_count();
		unsigned int i;
		char line[64];

		CASE_ANCHOR("rtw89_c6: log phase %u calls=%u bytes=%lu\n", n,
			    calls - c6_log_calls_begin,
			    (unsigned long)(bytes - c6_log_bytes_begin));
		for (i = 0u; i < 4u; i++) {
			unsigned int idx = log_ring_count() - 1u - i;

			(void)log_ring_line(idx, line, sizeof(line));
			if (line[0] != '\0')
				CASE_ANCHOR("rtw89_c6: logtail phase %u "
					    "i=%u msg=%s\n", n, i, line);
		}
		c6_task_census(n, c6_task_begin, c6_task_begin_n, st, nst);
	}
#endif
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
#ifdef CONFIG_LKPI_SKBQ_DIAG
	/* Same callout machinery the stack's timers use, exercised once before
	 * the traffic starts: a control arm inside the same boot. */
	CASE_ANCHOR("rtw89_c6: timer selftest fired=%d\n",
		    lkpi_timer_selftest());
#endif

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
#ifdef CONFIG_RTW89_C6_SKIP_LOOPBACK
		if (c6_plan[i].kind == C6_LOOPBACK) {
			CASE_ANCHOR("rtw89_c6: phase %u skipped "
				    "name=%s (loopback consumes the listener)\n",
				    i + 1u, c6_plan[i].name);
			continue;
		}
#endif
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

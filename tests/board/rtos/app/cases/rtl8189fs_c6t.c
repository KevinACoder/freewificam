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

#ifdef CONFIG_LKPI_CENSUS
/* The freeze watchdog's inputs: the board clock (the census' time source),
 * the kernel's per-task run time, the controller's own state, and the fsl
 * card-lock owner.  Kept inside the census switch so a delivery image
 * compiles none of it. */
#include "generic_timer.h"
#include "lkpi_census.h"
#include "rtos_stats.h"
#include "dwc_mmc.h"
#include "sdmmc_lock_diag.h"
#endif

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

#ifdef CONFIG_LKPI_CENSUS
/*
 * The freeze watchdog (observation images).
 *
 * The engine's own account says where a transfer stopped; it cannot say who
 * owned the core instead.  This thread is the second half of that reading: when
 * the engine's progress counter has not moved for C6T_WD_STALL_MS while a
 * wireless phase is running, it takes one snapshot - the engine's in-flight
 * operation and site, the fsl card-lock owner, the controller's registers, the
 * kernel's per-task run time over a 250 ms window, and the tail of the transfer
 * ring - and prints it once for the episode.  A freeze therefore becomes a
 * named finding instead of "the pump is stuck".
 *
 * It runs in the highest band on purpose: the target condition is the pump not
 * being scheduled, so the observer must be able to outrank whatever is on the
 * core.  It costs one 200 ms tick and nothing else until it fires.
 */
#define C6T_WD_PERIOD_MS	200u
#define C6T_WD_STALL_MS		600u
#define C6T_WD_TASKS		24u
#define C6T_WD_OPS		12u

uint64_t
lkpi_census_now_ns(void)
{
	uint32_t freq = gtimer_freq_get();
	uint64_t count;

	if (freq == 0u)
		return 0u;
	count = gtimer_count_get();
	return (count / freq) * 1000000000ULL +
	    ((count % freq) * 1000000000ULL) / freq;
}

static volatile unsigned int c6t_traffic_active;
static osThreadId_t c6t_wd_thread;

static const char *
c6t_task_state(uint32_t state)
{
	switch (state) {
	case RTOS_TASK_RUNNING:		return "run";
	case RTOS_TASK_READY:		return "ready";
	case RTOS_TASK_BLOCKED:		return "block";
	case RTOS_TASK_SUSPENDED:	return "susp";
	case RTOS_TASK_DELETED:		return "del";
	default:			return "?";
	}
}

static void
c6t_watchdog_tasks(const struct rtos_task_stat *a, unsigned int na,
    const struct rtos_task_stat *b, unsigned int nb)
{
	uint32_t delta[C6T_WD_TASKS];
	uint32_t freq = gtimer_freq_get();
	uint32_t per_ms = (freq != 0u) ? (freq / 1000u) : 24000u;
	unsigned int i, j, k, shown = 0u;
	int top = -1;

	for (i = 0u; i < nb && i < C6T_WD_TASKS; i++) {
		delta[i] = 0u;
		/* Match by task number, not name: the vendor creates more than one
		 * task named RTWHALXT, and a name match then pairs two different
		 * tasks' counters (measured: a 2^32-tick "delta" that was the wrap). */
		for (j = 0u; j < na; j++) {
			if (a[j].number == b[i].number) {
				delta[i] = b[i].runtime - a[j].runtime;
				break;
			}
		}
	}
	for (k = 0u; k < 6u; k++) {
		unsigned int best = nb;

		for (i = 0u; i < nb && i < C6T_WD_TASKS; i++) {
			if (delta[i] == 0u)
				continue;
			if (best == nb || delta[i] > delta[best])
				best = i;
		}
		if (best == nb)
			break;
		if (k == 0u)
			top = (int)best;
		CASE_ANCHOR("rtl8189fs_c6t: stall task %s st=%s prio=%u "
			    "cpu_ms=%u stack=%u\n",
			    b[best].name != NULL ? b[best].name : "?",
			    c6t_task_state(b[best].state), b[best].prio,
			    delta[best] / per_ms, b[best].stack_free);
		delta[best] = 0u;
		shown++;
	}
	if (shown == 0u)
		CASE_ANCHOR("rtl8189fs_c6t: stall task none\n");
	/* What the biggest consumer was executing: a spin names itself here, and
	 * an ISR storm shows up as PCs in the vector/board range attributed to the
	 * task it interrupted. */
	if (top >= 0 && b[top].handle != NULL) {
		struct rtos_pc_hit h[6];
		unsigned int nh = rtos_pc_top(b[top].handle, h, 6u);
		unsigned int q;

		for (q = 0u; q < nh; q++) {
			CASE_ANCHOR("rtl8189fs_c6t: stall hogpc task=%s pc=0x%llx n=%u\n",
				    b[top].name != NULL ? b[top].name : "?",
				    (unsigned long long)h[q].pc, h[q].count);
		}
	}
}

static void
c6t_watchdog_dump(unsigned int no_progress_ms)
{
	struct lkpi_sdio_diag d;
	struct lkpi_sdio_stats st;
	struct lkpi_sdio_op ops[C6T_WD_OPS];
	struct rtos_task_stat a[C6T_WD_TASKS], b[C6T_WD_TASKS];
	struct dwc_mmc_snapshot sn;
	struct dwc_mmc_census mc;
	struct sdmmc_lock_diag osa;
	uint64_t now_ns;
	unsigned int now_ms, age_ms, n, i, na, nb;

	lkpi_sdio_diag_get(&d);
	lkpi_sdio_stats_get(&st);
	(void)dwc_mmc_snapshot_get(&sn);
	dwc_mmc_census_get(&mc);
	sdmmc_osa_lock_diag_get(&osa);
	now_ns = lkpi_census_now_ns();
	now_ms = (unsigned int)(now_ns / 1000000u);
	age_ms = (d.inflight_valid != 0u &&
	    now_ms >= d.inflight_t0_us / 1000u) ?
	    now_ms - d.inflight_t0_us / 1000u : 0u;

	CASE_ANCHOR("rtl8189fs_c6t: stall begin no_progress_ms=%u progress=%u "
		    "site=%s dwcsite=%s\n", no_progress_ms, d.progress,
		    d.site, dwc_mmc_site());
	CASE_ANCHOR("rtl8189fs_c6t: stall claim=%s/%u/%s/%s pump=%s irq=%u "
		    "prio=%u io=%u/%u err=%u\n", lkpi_sdio_claim_owner(),
		    lkpi_sdio_claim_held_ms(), lkpi_sdio_pump_stage(),
		    lkpi_sdio_claim_op(), lkpi_sdio_pump_state(),
		    lkpi_sdio_pump_irq_get() ? 1u : 0u,
		    lkpi_sdio_pump_prio_level_get(), st.io_direct,
		    st.io_extended, st.errors);
	CASE_ANCHOR("rtl8189fs_c6t: stall inflight valid=%u kind=%u func=%u "
		    "addr=0x%x bytes=%u blocks=%u age_ms=%u\n",
		    d.inflight_valid, d.inflight_kind, d.inflight_func,
		    d.inflight_addr, d.inflight_bytes, d.inflight_blocks,
		    age_ms);
	CASE_ANCHOR("rtl8189fs_c6t: stall dwc pending=%u cmd=%u len=%u err=%u "
		    "laststs=0x%x\n", mc.pending, mc.last_cmd, mc.last_len,
		    mc.last_error, mc.last_status);
	CASE_ANCHOR("rtl8189fs_c6t: stall dwc t=%u d=%u e=%u cmd_p=%u/"
		    "%llu/%llu data_p=%u/%llu/%llu idmac_p=%u/%llu/%llu\n",
		    mc.transfers, mc.with_data, mc.errors, mc.cmd_polls,
		    (unsigned long long)(mc.cmd_wait_ns / 1000u),
		    (unsigned long long)(mc.cmd_wait_max_ns / 1000u),
		    mc.data_polls,
		    (unsigned long long)(mc.data_wait_ns / 1000u),
		    (unsigned long long)(mc.data_wait_max_ns / 1000u),
		    mc.idmac_polls,
		    (unsigned long long)(mc.idmac_wait_ns / 1000u),
		    (unsigned long long)(mc.idmac_wait_max_ns / 1000u));
	CASE_ANCHOR("rtl8189fs_c6t: stall reg rintsts=0x%x idsts=0x%x "
		    "status=0x%x bytcnt=%u tcb=%u tbb=%u dsc=0x%x buf=0x%x\n",
		    sn.rintsts, sn.idsts, sn.status, sn.bytcnt, sn.tcb,
		    sn.tbb, sn.dscaddr, sn.bufaddr);
	CASE_ANCHOR("rtl8189fs_c6t: stall reg bmod=0x%x idinten=0x%x "
		    "tmout=0x%x ctype=0x%x clkena=0x%x clkdiv=%u fifoth=0x%x\n",
		    sn.bmod, sn.idinten, sn.tmout, sn.ctype, sn.clkena,
		    sn.clkdiv, sn.fifoth);
	CASE_ANCHOR("rtl8189fs_c6t: stall reg pldmnd=%u desc0=0x%x/%u/0x%x "
		    "osa=%s/%u/%u\n", sn.pldmnd, sn.desc0_flags, sn.desc0_cnt,
		    sn.desc0_addr, osa.owner, osa.held_ms, osa.waiters);
	{
		struct sdmmc_lock_diag card_lock;

		lkpi_sdio_fsl_card_lock_get(&card_lock);
		CASE_ANCHOR("rtl8189fs_c6t: stall cardlock=%s/%u/w%u v=%u\n",
			    card_lock.owner, card_lock.held_ms,
			    card_lock.waiters, card_lock.valid);
	}
	CASE_ANCHOR("rtl8189fs_c6t: stall udelay_caller=0x%lx\n",
		    (unsigned long)dwc_mmc_udelay_caller_get());
	CASE_ANCHOR("rtl8189fs_c6t: stall savedpc pump=0x%llx\n",
		    (unsigned long long)rtos_task_saved_pc(
		    lkpi_sdio_pump_thread()));
	{
		uint64_t c0 = gtimer_count_get();
		uint64_t c1 = gtimer_count_get();

		CASE_ANCHOR("rtl8189fs_c6t: stall loganchor=%u\n",
		    log_anchor_call_count());
	CASE_ANCHOR("rtl8189fs_c6t: stall claim_violations=%u thread=%s owner=%s "
		    "prog=%u tick=%u\n",
		    lkpi_sdio_claim_violations(),
		    lkpi_sdio_claim_violation_thread(),
		    lkpi_sdio_claim_violation_owner(),
		    lkpi_sdio_claim_violation_progress(),
		    lkpi_sdio_claim_violation_tick());
	CASE_ANCHOR("rtl8189fs_c6t: stall clk freq=%u poll_us=%u "
			    "dpoll_us=%u c0=%llu dc=%llu\n",
			    gtimer_freq_get(), dwc_mmc_get_poll_us(),
			    dwc_mmc_get_data_poll_us(),
			    (unsigned long long)c0,
			    (unsigned long long)(c1 - c0));
	}

	na = rtos_task_stats_get(a, C6T_WD_TASKS);
	osDelay(250u);
	nb = rtos_task_stats_get(b, C6T_WD_TASKS);
	c6t_watchdog_tasks(a, na, b, nb);

	{
		struct rtos_pc_hit hits[4];
		unsigned int nh = rtos_pc_top(lkpi_sdio_pump_thread(), hits, 4u);

		if (nh == 0u) {
			CASE_ANCHOR("rtl8189fs_c6t: stall pc none\n");
		}
		for (i = 0u; i < nh; i++) {
			CASE_ANCHOR("rtl8189fs_c6t: stall pc=0x%llx n=%u\n",
				    (unsigned long long)hits[i].pc, hits[i].count);
		}
		{
			void *holder = lkpi_sdio_claim_owner_thread();
			uint32_t ago = 0u;
			uint64_t last = rtos_pc_last(lkpi_sdio_pump_thread(),
						     &ago);
			uint64_t hlast = rtos_pc_last(holder, &ago);
			unsigned int hi;

			for (hi = 0u; hi < nb; hi++) {
				if (b[hi].handle == holder)
					break;
			}
			CASE_ANCHOR("rtl8189fs_c6t: stall holder=%s st=%s "
				    "lastpc=0x%llx ago=%u sp=0x%llx\n",
				    (hi < nb) ? b[hi].name : "-",
				    (hi < nb) ? c6t_task_state(b[hi].state) : "-",
				    (unsigned long long)hlast, ago,
				    (unsigned long long)(holder != NULL ?
					*(uint64_t *)holder : 0u));
			{
				uint64_t hv[8];
				uint32_t ho[8];
				unsigned int hn, hk;
				uint64_t raw[16];

				hn = rtos_task_frame_scan(holder, hv, ho, 8u);
				for (hk = 0u; hk < hn; hk++)
					CASE_ANCHOR("rtl8189fs_c6t: stall hframe "
						    "off=0x%x val=0x%llx\n",
						    ho[hk], (unsigned long long)hv[hk]);
				(void)rtos_task_frame_raw(holder, 0x200u, raw, 16u);
				for (hk = 0u; hk < 16u; hk++)
					CASE_ANCHOR("rtl8189fs_c6t: stall hraw "
						    "off=0x%x val=0x%llx\n",
						    0x200u + hk * 8u,
						    (unsigned long long)raw[hk]);
			}
			uint64_t fv[8];
			uint32_t fo[8];
			unsigned int fn, k;

			CASE_ANCHOR("rtl8189fs_c6t: stall lastpc pump=0x%llx "
				    "ago_tick=%u\n",
				    (unsigned long long)last, ago);
			fn = rtos_task_frame_scan(lkpi_sdio_pump_thread(), fv,
						  fo, 8u);
			for (k = 0u; k < fn; k++)
				CASE_ANCHOR("rtl8189fs_c6t: stall frame "
					    "off=0x%x val=0x%llx\n",
					    fo[k], (unsigned long long)fv[k]);
		}
	}

	n = lkpi_sdio_op_dump(ops, C6T_WD_OPS);
	for (i = 0u; i < n; i++) {
		if (ops[i].seq == 0u)
			continue;
		CASE_ANCHOR("rtl8189fs_c6t: stall op kind=%u addr=0x%x "
			    "bytes=%u blk=%u dur_us=%u ret=%d\n",
			    ops[i].kind, ops[i].addr, ops[i].bytes,
			    ops[i].blocks, ops[i].dur_us, ops[i].ret);
	}
	CASE_ANCHOR("rtl8189fs_c6t: stall end\n");
}

static void
c6t_watchdog_entry(void *arg)
{
	uint32_t base = 0u, last = 0u;
	unsigned int still = 0u, primed = 0u, dumped = 0u;
	unsigned int active_prev = 0u;

	(void)arg;
	for (;;) {
		struct lkpi_sdio_diag d;

		osDelay(C6T_WD_PERIOD_MS);
		if (c6t_traffic_active == 0u) {
			active_prev = 0u;
			primed = 0u;
			still = 0u;
			dumped = 0u;
			continue;
		}
		lkpi_sdio_diag_get(&d);
		if (active_prev == 0u) {
			active_prev = 1u;
			base = d.progress;
			primed = 0u;
			still = 0u;
			dumped = 0u;
			continue;
		}
		if (primed == 0u) {
			if (d.progress != base)
				primed = 1u;
			else
				continue;
			last = d.progress;
			continue;
		}
		if (d.progress != last) {
			last = d.progress;
			still = 0u;
			dumped = 0u;
			continue;
		}
		still += C6T_WD_PERIOD_MS;
		if (still >= C6T_WD_STALL_MS && dumped == 0u) {
			dumped = 1u;
			c6t_watchdog_dump(still);
		}
	}
}
#endif	/* CONFIG_LKPI_CENSUS */

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

#ifdef CONFIG_LKPI_CENSUS
/*
 * The engine's per-cycle probe (lkpi_census.h): the one reading only the board
 * side can take before the bus is claimed - how many receive buffers the
 * vendor driver still has free.  The engine calls it once per pump cycle; the
 * minimum is kept for the observation line, so a cycle storm that drains the
 * driver's receive pool shows up as a low-water mark rather than a guess.
 */
void
lkpi_census_pump_probe(void)
{
	struct rtl8189f_diag d;

	rtl8189f_diag_get(&d);
	if (d.valid != 0u && d.recv_free < c6t_recv_min)
		c6t_recv_min = d.recv_free;
}
#endif

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
		    "rxfifo=%u txb=%u/%u cmd=%u/%u la=%u lb=%llu\n",
		    sec, lkpi_sdio_claim_owner(), (unsigned)lkpi_sdio_claim_held_ms(),
		    lkpi_sdio_pump_stage(), lkpi_sdio_claim_op(),
		    lkpi_sdio_pump_state(),
		    lkpi_sdio_pump_irq_get() ? 1u : 0u,
		    (unsigned)st.io_direct, (unsigned)st.io_extended,
		    (unsigned)st.errors, (unsigned)d.recv_free,
		    (unsigned)d.recvbuf_free, c6t_recv_min, c6t_rxbuf_min,
		    (unsigned)d.rx_fifo, (unsigned)d.txbuf_free,
		    (unsigned)d.txframe_free, (unsigned)d.cmd_issued,
		    (unsigned)d.cmd_done, log_anchor_call_count(),
		    log_emit_byte_count());
#ifdef CONFIG_LKPI_CENSUS
	{
		struct lkpi_sdio_rx_stats rxs;
		struct lkpi_sdio_diag dg;

		lkpi_sdio_rx_stats_get(&rxs);
		lkpi_sdio_diag_get(&dg);
		CASE_ANCHOR("rtl8189fs_c6t: obs2 t=%u site=%s dwc=%s prog=%u "
			    "offer=%u/%u bytes=%lu read=%u/%u/%u "
			    "hisr=%u/%u/%u/%u\n",
			    sec, dg.site, dwc_mmc_site(), dg.progress,
			    rxs.offer_reads, rxs.offer_zero,
			    (unsigned long)rxs.offer_bytes, rxs.read_ok,
			    rxs.read_short, rxs.read_fail, rxs.hisr_rxreq,
			    rxs.hisr_aval, rxs.hisr_rxerr, rxs.hisr_rxfovw);
	}
#endif
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
#ifdef CONFIG_LKPI_CENSUS
	if (ph->kind != C6T_LOOPBACK)
		c6t_traffic_active = 1u;
#endif

	switch (ph->kind) {
	case C6T_LOOPBACK:
		rc = lkpi_net_loopback(ph->port, ph->window_ms, &r);
		break;
	case C6T_TCP_DOWN:
		rc = c6t_tcp_down_sampled(ph->window_ms, &r);
		/*
		 * The iperf server reports once per session, and a second down
		 * window on the same server can miss that report while the data
		 * itself arrived.  The adapter's receive counter is the same
		 * quantity one layer down; use it only as the fallback, and say
		 * so in the line, so a real zero is still a zero.
		 */
		if (rc != 0) {
			unsigned long rxb = after.diag.rx_bytes - before.diag.rx_bytes;

			if (rxb != 0u) {
				r.bytes = (unsigned)rxb;
				r.ms = ph->window_ms;
				r.kbit = (unsigned)(((uint64_t)rxb * 8u) /
						    ph->window_ms);
				r.done = 1u;
				rc = 0;
				CASE_ANCHOR("rtl8189fs_c6t: phase %u report "
					    "source=adapter\n", n);
			}
		}
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
#ifdef CONFIG_LKPI_CENSUS
	if (ph->kind != C6T_LOOPBACK)
		c6t_traffic_active = 0u;
#endif
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
	/* The pump's band is the round's single causal variable when it is not
	 * the default; setting it before the plan line makes the boot record the
	 * band it actually ran, not the band the profile asked for. */
	lkpi_sdio_pump_prio_level_set(CONFIG_C6T_PUMP_PRIO);
	lkpi_sdio_pump_mode_set(CONFIG_C6T_PUMP_MODE);
	lkpi_sdio_pump_yield_set(CONFIG_C6T_PUMP_YIELD);
	lkpi_sdio_pump_drain_set(CONFIG_C6T_PUMP_DRAIN);
#if CONFIG_C6T_PUMP_IRQ
	(void)lkpi_sdio_pump_irq_set(true);
#endif
#ifdef CONFIG_LKPI_CENSUS
	lkpi_sdio_claim_guard_set(true);
#endif
	CASE_ANCHOR("rtl8189fs_c6t: plan=throughput bssid=%02x:%02x:%02x:%02x:"
		    "%02x:%02x dev=%s prio=%u mode=%u yield=%u drain=%u irq=%u\n",
		    bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
		    dev->name, lkpi_sdio_pump_prio_level_get(),
		    (unsigned)CONFIG_C6T_PUMP_MODE,
		    (unsigned)CONFIG_C6T_PUMP_YIELD,
		    (unsigned)CONFIG_C6T_PUMP_DRAIN,
		    lkpi_sdio_pump_irq_get() ? 1u : 0u);

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

#ifdef CONFIG_LKPI_CENSUS
	{
		osThreadAttr_t wd = { 0 };

		wd.name = "c6t_wd";
		wd.priority = osPriorityRealtime;
		wd.stack_size = 12288u;
		c6t_wd_thread = osThreadNew(c6t_watchdog_entry, NULL, &wd);
		if (c6t_wd_thread == NULL)
			CASE_ANCHOR("rtl8189fs_c6t: note watchdog not started\n");
	}
#endif

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

#ifdef CONFIG_LKPI_CENSUS
	c6t_traffic_active = 0u;
	if (c6t_wd_thread != NULL) {
		(void)osThreadTerminate(c6t_wd_thread);
		c6t_wd_thread = NULL;
	}
#endif

	if (ok == C6T_TRAFFIC_PHASES) {
		CASE_ANCHOR("rtl8189fs_c6t: PASS C6T\n");
		return 0;
	}
	CASE_ANCHOR("rtl8189fs_c6t: FAIL traffic phases %u/%u\n", ok,
		    C6T_TRAFFIC_PHASES);
	return 5;
}

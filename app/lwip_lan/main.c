/*
 * @file   main.c
 * @brief  lwip_lan bring-up main (S7 phase 2, NO_SYS=0, threadx base).
 *
 * P7-2a shape (specs/lwip-adapt §3.1/§4/P7-2a): the lwIP stack runs its own
 * tcpip thread (sys_arch over the cmsis2_threadx shim, D54 band ladder:
 * tcpip(5) > this app's workers(3) - N7 raised the tcpip band) on the
 * ThreadX UP kernel.
 * The GMAC driver and the ethnetif bridge are unchanged from phase 1 -
 * only the input pointer (tcpip_input) and the delivery context moved:
 *
 *   GMAC ISR (frames staged) -> dwc_eqos rx_notify hook -> osSemaphoreRelease
 *   thr_rx: osSemaphoreAcquire -> ethnetif_poll -> netif->input = tcpip_input
 *
 *   thr_boot: GMAC bring-up -> tcpip_init -> [tcpip thread] netif_add +
 *   lwiperf server + the `net: READY` anchor (the prior workspace's
 *   lwip_adapter anchor shape) -> heartbeat park.
 *
 * The GMAC port stays a build-time single choice (CONFIG_GMAC_PORT,
 * KI-027 by construction); the acceptance chain is phase 1's rerun on the
 * socket-form stack.
 */

#include <stdint.h>

#include "board_conf.h"
#include "cmsis_os2.h"
#include "gicv3.h"
#include "log.h"
#include "minilibc.h"
#include "lwip/init.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "lwip/apps/lwiperf.h"
#include "arch/sys_arch.h"
#include "uart.h"

#ifdef CONFIG_GDB_STUB
#include "gdb_glue.h"
#endif

#include "dwc_eqos.h"
#include "ethnetif.h"
#include "eqos_rk3568.h"
#include "rtl8211f.h"
#include "cpu_boost.h"

#if defined(CONFIG_GMAC_TICK_MON) || defined(CONFIG_GMAC_FAST_PROBE)
#include "cmsis2_diag.h"
#endif

#ifdef CONFIG_SCMI_PROBE
#include "scmi_clk.h"
#endif
#ifdef CONFIG_IPERF2_AUTO_HOST
#include "iperf2_cmd.h"
#endif

#ifdef CONFIG_SHELL
#include "cherrysh_adapter.h"
#endif

/* RX-path forensics (2026-10-02 console-outage round; SHELL_RX_DIAG build
 * knob): the driver counters live in uart.c, the adapter's in
 * cherrysh_adapter.c - both non-static so the heartbeat can split the
 * path ISR -> ring -> readline in one line. */
#if defined(SHELL_RX_DIAG) && defined(CONFIG_UART_RX_IRQ) && defined(CONFIG_SHELL)
extern volatile unsigned uart_console_rx_isr_calls;
extern volatile unsigned uart_console_rx_isr_bytes;
extern volatile unsigned cherrysh_rx_ringed;
extern volatile unsigned cherrysh_rx_consumed;
extern volatile unsigned cherrysh_rx_wakes;
#endif

#ifndef THREADX_BUILD
#error "lwip_lan phase 2 runs on the threadx base (spec S7 §4/P7-2a)"
#endif

#ifndef CONFIG_GMAC_PORT
#error "CONFIG_GMAC_PORT (0=gmac0, 1=gmac1) must come from the config"
#endif

/* The watchdog cadence the prior workspace's osTimer ran at. */
#define GMAC_WATCHDOG_MS	100u

static struct netif g_netif;
static unsigned int g_port;
static osSemaphoreId_t g_rx_sem;

#if defined(CONFIG_GMAC_TICK_MON) || defined(CONFIG_GMAC_FAST_PROBE)
/* Declared locally, not via os/threadx/heap.h: this file compiles without
 * the threadx include path on purpose (FR-3 - tx headers stay inside the
 * adapter units). The one symbol consumed is plain size_t. */
extern size_t xPortGetFreeHeapSize(void);

/* --- N7-stab forensics (2026-10-02 sustained-TX freeze round) ---------------
 *
 * The freeze signature: everything (heartbeat, tick command, ping, serial)
 * dies 15-30 s into a ~650 Mbit/s TCP TX flow, with zero precursor in the 1 Hz
 * counters. Two independent observers, so the death is never silent again:
 *
 *  - TICK MONITOR: a CNTP (EL1 physical timer) interrupt at GIC priority 0x60
 *    - ABOVE the GMAC line (0xa0), which is above the kernel tick (0xe0).
 *    If the kernel tick stops advancing for one monitor period (250 ms), an
 *    interrupt at an urgency between the monitor and the tick is starving
 *    everything below it (the interrupt-storm class this board's IP has
 *    produced before), OR interrupts are masked in some context. Either way
 *    the monitor - which can only have fired between their handlers - prints
 *    the GMAC register crime scene via the lock-free emergency outlet.
 *  - FAST PROBE: a Realtime-band thread (tx prio 7, above tcpip's 11) that
 *    prints a compact state line at 10 Hz for the first 60 s, then 1 Hz.
 *    Every thread's run counter rides the line, so a thread that stops being
 *    scheduled - or one that hogs the CPU - is visible by diffing adjacent
 *    samples. The last line before a freeze is the crime scene at thread
 *    level.
 */
static volatile unsigned int g_mon_fires;
static volatile unsigned int g_mon_stalls;

#ifdef CONFIG_GMAC_TICK_MON
static uint32_t mon_last_tick;
static int mon_primed;

static void tick_mon_isr(void *arg)
{
	static unsigned int stall_reports;
	uint32_t now;
	uint32_t period;

	(void)arg;

	/* Re-arm first: CNTP is a down-counter; the level line clears when TVAL
	 * is rewritten (same discipline as the tick itself). */
	{
		uint64_t freq;

		__asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(freq));
		period = (uint32_t)(freq / 4u);	/* 250 ms */
		__asm__ __volatile__("msr cntp_tval_el0, %0" ::"r"(period));
		__asm__ __volatile__("isb" ::: "memory");
	}

	g_mon_fires++;
	now = osKernelGetTickCount();
	if (!mon_primed) {
		mon_primed = 1;
		mon_last_tick = now;
		return;
	}
	if (now == mon_last_tick) {
		g_mon_stalls++;
		if (stall_reports < 4u) {
			struct dwc_eqos_diag d;

			stall_reports++;
			dwc_eqos_get_diag(g_port, &d);
			board_log_emergency(
				"MON tick stalled #%u: fires=%u tick=%u | ch0=%08x ien=%08x pend=%d irqn=%u odd=%u st=%08x brk=%u cur=%s",
				stall_reports, g_mon_fires, (unsigned)now,
				(unsigned)d.ch0_status, (unsigned)d.intr_enable,
				(int)d.gic_pending, (unsigned)d.irq_n,
				(unsigned)d.irq_odd, (unsigned)d.last_status,
				(unsigned)d.irq_breaker,
				cmsis2_current_thread_name());
			/* Second line: every other block that shares the line. The
			 * MAC status read is read-clear - it doubles as an unstick
			 * attempt, so a resumed tick after this dump pins the latch
			 * as the line source. */
			dwc_eqos_irq_dump(g_port);
		}
	}
	mon_last_tick = now;
}

static void tick_mon_start(unsigned int port)
{
	uint64_t freq;
	uint32_t period;

	(void)port;
	__asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(freq));
	period = (uint32_t)(freq / 4u);

	(void)irq_register(BOARD_TICK_MON_INTID, tick_mon_isr, NULL);
	(void)irq_set_priority(BOARD_TICK_MON_INTID, BOARD_TICK_MON_PRIORITY_RAW);
	/* Program before enabling: ENABLE + IMASK=0, level line clears on TVAL
	 * rewrite (the R2-proven CNTP programming model in tick.c). */
	__asm__ __volatile__("msr cntp_tval_el0, %0" ::"r"(period));
	__asm__ __volatile__("msr cntp_ctl_el0, %0" ::"r"(1u));
	__asm__ __volatile__("isb" ::: "memory");
	(void)irq_enable(BOARD_TICK_MON_INTID);
	board_log("MON: armed intid=%u prio=0x%02x period=%ums\n",
		  (unsigned)BOARD_TICK_MON_INTID,
		  (unsigned)BOARD_TICK_MON_PRIORITY_RAW,
		  (unsigned)(period / (uint32_t)(freq / 1000u)));
}
#endif /* CONFIG_GMAC_TICK_MON */

#ifdef CONFIG_GMAC_FAST_PROBE
/* Sampling cadence 100 ms; print every 5th sample (2 Hz) for the first 120 s,
 * then every 50th. The print bypasses board_log on purpose: that sink masks
 * ALL interrupts for the whole line (~15 ms at 115200), and a 10 Hz probe
 * doing that would steal the interrupts whose absence it is meant to
 * observe. uart_early_puts writes the same UART without touching the lock -
 * thread-vs-thread interleaving is still prevented because every board_log
 * caller holds IRQs masked for its own line (so it can never start mid
 * probe-line and vice versa). */
static void thr_probe(void *arg)
{
	unsigned long samples = 0;

	(void)arg;
	for (;;) {
		struct dwc_eqos_diag d;
		cmsis2_thread_snap_t snap[12];
		unsigned int n, i;
		unsigned int overrun = 0;
		unsigned int every = (samples < 1200UL) ? 5U : 50U;
		char tline[220];
		unsigned int off = 0;

		(void)osDelay(100U);
		samples++;

		dwc_eqos_get_diag(g_port, &d);
		n = cmsis2_thread_snapshot(snap, 12);

		if ((samples % every) != 0UL) {
			continue;
		}

		off += (unsigned int)snprintf(
			&tline[off], sizeof(tline) - off,
			"probe t=%lum tx=%u/%u/%u irq=%u/%u st=%08x pend=%d rx=%u/%u hp=%luk pl=%d mon=%u/%u cur=%s |",
			samples * 100UL,
			(unsigned)d.tx_head, (unsigned)d.tx_tail,
			(unsigned)d.tx_sem_count,
			(unsigned)d.irq_n, (unsigned)d.irq_odd,
			(unsigned)d.last_status, (int)d.gic_pending,
			(unsigned)d.rx_ready_count, (unsigned)d.rx_free_count,
			(unsigned long)(xPortGetFreeHeapSize() / 1024UL),
			board_console_lock_held(),
			g_mon_fires, g_mon_stalls,
			cmsis2_current_thread_name());

		for (i = 0U; i < n; i++) {
			if (off > sizeof(tline) - 24U) {
				overrun = 1;
				break;
			}
			off += (unsigned int)snprintf(&tline[off],
						      sizeof(tline) - off,
						      " %.7s%u=%lu",
						      (snap[i].name != NULL) ?
							      snap[i].name : "?",
						      snap[i].tx_prio,
						      snap[i].runs);
		}
		if (overrun) {
			(void)snprintf(&tline[off], sizeof(tline) - off, " ...");
		}

		if (board_console_lock_held()) {
			/* A dead context owns the print lock: entering board_log
			 * would spin on it forever. The probe's own outlet below
			 * is lock-free anyway; keep the emergency prefix so the
			 * condition is visible in the log. */
			board_log_emergency("%s", tline);
		} else {
			uart_early_puts(tline);
			uart_early_puts("\n");
		}
	}
}
#endif /* CONFIG_GMAC_FAST_PROBE */
#endif /* CONFIG_GMAC_TICK_MON || CONFIG_GMAC_FAST_PROBE */

/* GMAC ISR context: frames landed in the staging ring - wake the RX
 * thread (osSemaphoreRelease is ISR-safe: tx_semaphore_put). */
static void rx_notify(unsigned int index)
{
	(void)index;
	(void)osSemaphoreRelease(g_rx_sem);
}

/* RX thread: drain the staging ring into the tcpip thread's mailbox. */
static void thr_rx(void *arg)
{
	(void)arg;
	for (;;) {
		(void)osSemaphoreAcquire(g_rx_sem, osWaitForever);
		(void)ethnetif_poll(&g_netif);
	}
}

/* lwiperf report callback: one line per session event (the P7-1c anchor
 * shape, kept for the regression comparison). */
static void lwiperf_report(void *arg, enum lwiperf_report_type type,
			   const ip_addr_t *local_addr, u16_t local_port,
			   const ip_addr_t *remote_addr, u16_t remote_port,
			   u32_t bytes, u32_t ms, u32_t kbitpsec)
{
	(void)arg;
	(void)local_addr;
	(void)local_port;
	(void)remote_addr;
	(void)remote_port;
	board_log("lwiperf: report type=%d bytes=%u ms=%u %ukbit/s\n",
		  (int)type, (unsigned)bytes, (unsigned)ms,
		  (unsigned)kbitpsec);
}

#if defined(CONFIG_SCMI_PROBE) || defined(CONFIG_IPERF2_AUTO_HOST)
/* Unattended diagnostic chain (the R5b console-RX outage workaround): the
 * SCMI clock probe first, then - when built with CONFIG_IPERF2_AUTO_HOST -
 * one 10 s lwiperf client burst so the forward direction gets measured
 * without anyone typing at a shell. Delays keep the report order stable in
 * the boot log and give the PC-side server a moment to accept. */
static void thr_autorun(void *arg)
{
	(void)arg;
	board_log("autorun: enter\n");
	/* Settle delay: give the bring-up log a moment before the optional
	 * probe/measurement (osDelay is the kernel's own proven path - the
	 * R5b "plain sleep never returned" observation was the slot-entry
	 * parking bug, not the delay call). A busy-wait here would starve
	 * the tx19 shell/heartbeat threads for its whole duration. */
	(void)osDelay(3000U);
#ifdef CONFIG_SCMI_PROBE
	/* The boot-time probe is part of the unattended rate-set chain
	 * (boot set != 0) only. The probe has never executed on this
	 * silicon - with the shell demonstrably alive (2026-10-02) the
	 * first run belongs behind a decisive wall, i.e. the typed `scmi`
	 * command, not the boot path. */
#if defined(CONFIG_SCMI_BOOT_SET_MHZ) && (CONFIG_SCMI_BOOT_SET_MHZ != 0)
	scmi_boot_probe();
#endif
#endif
#ifdef CONFIG_IPERF2_AUTO_HOST
	(void)osDelay(2000U);
	(void)iperf2_client_start(CONFIG_IPERF2_AUTO_HOST);
#endif
}
#endif

/* Runs in the tcpip thread once the stack is up: netif management belongs
 * to this context (LWIP_TCPIP_CORE_LOCKING=1 - lwipopts N7 - so netifapi
 * calls execute directly against the core under LOCK_TCPIP_CORE()). */
static void tcpip_ready(void *arg)
{
	ip4_addr_t ip, mask, gw;
	uint8_t mac[6];

	(void)arg;

	IP4_ADDR(&mask, 255, 255, 255, 0);
	IP4_ADDR(&gw, 192, 168, 0, 1);
	if (g_port == 1U) {
		IP4_ADDR(&ip, 192, 168, 0, 200);
	} else {
		IP4_ADDR(&ip, 192, 168, 0, 201);
	}

	ethnetif_add(&g_netif, g_port, &ip, &mask, &gw);
	(void)dwc_eqos_get_mac_address(g_port, mac);
	board_log("net: up gmac%u ip=%u.%u.%u.%u/24 mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		  g_port, ip4_addr1_16(&ip), ip4_addr2_16(&ip),
		  ip4_addr3_16(&ip), ip4_addr4_16(&ip),
		  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	if (lwiperf_start_tcp_server_default(lwiperf_report, NULL) != NULL) {
		board_log("lwiperf: tcp server start\n");
	} else {
		board_log("lwiperf: server START FAILED\n");
	}

	board_log("net: READY (NO_SYS=0 tcpip thread prio band %d)\n",
		  TCPIP_THREAD_PRIO);

#if defined(CONFIG_SCMI_PROBE) || defined(CONFIG_IPERF2_AUTO_HOST)
	/* AboveNormal on purpose (tx band 15, beats thr_boot's busy loop at
	 * 19 - board-proven R5b: a Normal-priority autorun never got CPU).
	 * The thread exits when done. */
	{
		osThreadId_t t = osThreadNew(thr_autorun, NULL,
					     &(osThreadAttr_t){
						     .name = "autorun",
						     .priority = osPriorityAboveNormal });

		board_log("autorun: create -> %p\n", (void *)t);
	}
#endif
}

/* Bring one GMAC port up to "link up, channel running" (phase-1 sequence,
 * unchanged - see the P7-1b run ledger for the anchor chain). */
static int gmac_bring_up(unsigned int port)
{
	const struct dwc_eqos_plat *plat = eqos_board_plat(port);
	eqos_link_info_t link;
	uint32_t configure;

	if (plat == NULL) {
		board_log("gmac: no plat for port %u\n", (unsigned)port);
		return -1;
	}

	if (dwc_eqos_initialize(port, plat) != EQOS_OK) {
		board_log("gmac%u: initialize FAIL\n", (unsigned)port);
		return -1;
	}
	if (dwc_eqos_power_control(port, EQOS_POWER_FULL) != EQOS_OK) {
		board_log("gmac%u: power FAIL\n", (unsigned)port);
		return -1;
	}

	if (rtl8211f_initialize(port, (void *)(uintptr_t)port,
				eqos_board_mdio_read,
				eqos_board_mdio_write) != EQOS_OK) {
		board_log("phy%u: initialize FAIL\n", (unsigned)port);
		return -1;
	}
	if (rtl8211f_power_control(port, EQOS_POWER_FULL) != EQOS_OK) {
		board_log("phy%u: power FAIL\n", (unsigned)port);
		return -1;
	}
	(void)rtl8211f_set_interface(port, EQOS_INTERFACE_RGMII);
	if (rtl8211f_set_mode(port,
			      EQOS_PHY_AUTO_NEGOTIATE | EQOS_PHY_SPEED_1G) != EQOS_OK) {
		board_log("phy%u: no link (autoneg timeout)\n", (unsigned)port);
		return -1;
	}

	link = rtl8211f_get_link_info(port);
	configure = link.speed | link.duplex;
	(void)dwc_eqos_control(port, DWC_EQOS_CONFIGURE, configure);
	(void)dwc_eqos_control(port, DWC_EQOS_CONTROL_TX, 1);
	(void)dwc_eqos_control(port, DWC_EQOS_CONTROL_RX, 1);
	board_log("gmac%u: channel start\n", (unsigned)port);

	eqos_board_golden_dump(port);

	return 0;
}

/* Boot task: GMAC up -> RX delivery armed -> tcpip_init (which creates the
 * tcpip thread; tcpip_ready finishes the netif there) -> watchdog + the
 * 1 s heartbeat. */
static void thr_boot(void *arg)
{
	unsigned int port = (unsigned int)CONFIG_GMAC_PORT;
	struct dwc_eqos_stats stats;
	struct dwc_eqos_mmc_stats mmc;
	unsigned long d_poll, d_pool, d_input, d_fed;
	uint32_t wd_due, now;

	(void)arg;
	g_port = port;
	board_log("main: lwip_lan bring-up (threadx NO_SYS=0)\n");
	board_log("lwip_lan: gmac port=%u single-port build\n", port);
	board_log("lwip: v%s init NO_SYS=0\n", LWIP_VERSION_STRING);

	if (gmac_bring_up(port) != 0) {
		board_log("lwip_lan: gmac bring-up FAILED\n");
		return;
	}

#ifdef CONFIG_GMAC_TICK_MON
	tick_mon_start(port);
#endif

	g_rx_sem = osSemaphoreNew(0xFFFFU, 0U, NULL);
	if (g_rx_sem == NULL) {
		board_log("lwip_lan: rx semaphore FAIL\n");
		return;
	}
	dwc_eqos_set_rx_notify(rx_notify);
	(void)osThreadNew(thr_rx, NULL,
			  &(osThreadAttr_t){ .name = "gmacrx",
					     .priority = osPriorityNormal });

	tcpip_init(tcpip_ready, NULL);

	dwc_eqos_get_stats(port, &stats);
	board_log("gmac%u: rx=%u dropped=%u errors=%u tx=%u txdrop=%u txerr=%u\n",
		  port,
		  (unsigned)stats.rx_frames, (unsigned)stats.rx_dropped,
		  (unsigned)stats.rx_errors, (unsigned)stats.tx_frames,
		  (unsigned)stats.tx_dropped, (unsigned)stats.tx_errors);

	now = osKernelGetTickCount();
	wd_due = now + GMAC_WATCHDOG_MS;
	for (;;) {
		if ((int32_t)(osKernelGetTickCount() - wd_due) >= 0) {
			wd_due = osKernelGetTickCount() + GMAC_WATCHDOG_MS;
			dwc_eqos_watchdog(port);
		}
		if ((uint32_t)(osKernelGetTickCount() - now) >= 1000U) {
			now += 1000U;
			dwc_eqos_get_stats(port, &stats);
			dwc_eqos_get_mmc_stats(port, &mmc);
			ethnetif_get_diag(&d_poll, &d_pool, &d_input, &d_fed);
			board_log("gmac%u: hb rx=%u dropped=%u errors=%u tx=%u txdrop=%u txerr=%u | irq=%u csdrop=%u fed=%lu poolfail=%lu inputfail=%lu mbox=%u mmc_ovf=%u\n",
				  port,
				  (unsigned)stats.rx_frames,
				  (unsigned)stats.rx_dropped,
				  (unsigned)stats.rx_errors,
				  (unsigned)stats.tx_frames,
				  (unsigned)stats.tx_dropped,
				  (unsigned)stats.tx_errors,
				  (unsigned)stats.rx_irqs,
				  (unsigned)stats.rx_csum_drops,
				  d_fed, d_pool, d_input,
				  sys_arch_tcpip_mbox_count(),
				  (unsigned)mmc.rx_fifo_overflow);
#if defined(SHELL_RX_DIAG) && defined(CONFIG_UART_RX_IRQ) && defined(CONFIG_SHELL)
			/* One line per second: driver registers + ISR counters,
			 * then the adapter's ring/wake/consume counters. Where a
			 * keystroke stops moving localizes the fault. */
			uart_console_rx_diag();
			board_log("shell: rx armed=%d isr=%u bytes=%u ringed=%u consumed=%u wakes=%u\n",
				  uart_console_rx_armed(),
				  uart_console_rx_isr_calls,
				  uart_console_rx_isr_bytes,
				  cherrysh_rx_ringed,
				  cherrysh_rx_consumed,
				  cherrysh_rx_wakes);
#endif
		}
		(void)osDelay(1);
	}
}

#ifdef CONFIG_SHELL
/* cherrysh_init() MUST run from task context after the scheduler is up
 * (S4 spec §3.3.2 - the RX interrupt it arms ends in a FromISR wake). */
static void thr_shellboot(void *arg)
{
	(void)arg;

	if (cherrysh_init() == 0) {
		board_log("shell: READY\n");
	} else {
		board_log("shell: FAIL\n");
	}
	for (;;) {
		(void)osDelay(0x7fffffffU);
	}
}
#endif

int main(void)
{
	/* The polled console speaks only after a full re-program; the kernel
	 * takes the tick over in tx_application_define (1000 Hz). */
	uart_early_init();
	gicv3_init();

	/* N7 R5a: the bootloader leaves the cluster at 816 MHz; pair the
	 * Linux opp (voltage over i2c0 first, then APLL relock) before any
	 * thread exists. Probe-only when CONFIG_CPU_BOOST_MHZ == 0. */
	(void)rk3568_cpu_boost(CONFIG_CPU_BOOST_MHZ);

#ifdef CONFIG_GDB_STUB
	/* Same slot as every other app: the stub owns the dbgport before
	 * anything can trap (RSP rides the shared console UART via the oslab
	 * raw-TCP bridge - the gdb-registers comparison rounds attach here). */
	gdb_glue_init();
#endif

	(void)osKernelInitialize();
#ifdef CONFIG_SHELL
	(void)osThreadNew(thr_shellboot, NULL,
			  &(osThreadAttr_t){ .name = "shellboot",
					     .priority = osPriorityNormal });
#endif
	(void)osThreadNew(thr_boot, NULL,
			  &(osThreadAttr_t){ .name = "boot",
					     .priority = osPriorityNormal });
#ifdef CONFIG_GMAC_FAST_PROBE
	/* AboveNormal (tx 15): outranks the tx19 app threads (heartbeat, shell,
	 * gmacrx) so sampling stays on cadence, but stays BELOW the tcpip feed
	 * (tx 11) and the timer thread: the probe must observe the stack, not
	 * displace it - a probe at the top of the ladder would starve the very
	 * data path whose stall it is meant to record. */
	(void)osThreadNew(thr_probe, NULL,
			  &(osThreadAttr_t){ .name = "probe",
					     .priority = osPriorityAboveNormal });
#endif
	board_log("cmsis2: threads created, starting kernel\n");
	(void)osKernelStart();
	return 0;	/* unreachable */
}

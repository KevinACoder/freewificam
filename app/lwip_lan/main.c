/*
 * @file   main.c
 * @brief  lwip_lan bring-up main (S7 phase 2, NO_SYS=0, threadx base).
 *
 * P7-2a shape (specs/lwip-adapt §3.1/§4/P7-2a): the lwIP stack runs its own
 * tcpip thread (sys_arch over the cmsis2_threadx shim, D54 band ladder:
 * shell(5) > tcpip(4) > this app's workers(3)) on the ThreadX UP kernel.
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

#include "cmsis_os2.h"
#include "gicv3.h"
#include "log.h"
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

#ifdef CONFIG_SHELL
#include "cherrysh_adapter.h"
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
			board_log("gmac%u: hb rx=%u dropped=%u errors=%u tx=%u txdrop=%u txerr=%u | fed=%lu poolfail=%lu inputfail=%lu mbox=%u mmc_ovf=%u\n",
				  port,
				  (unsigned)stats.rx_frames,
				  (unsigned)stats.rx_dropped,
				  (unsigned)stats.rx_errors,
				  (unsigned)stats.tx_frames,
				  (unsigned)stats.tx_dropped,
				  (unsigned)stats.tx_errors,
				  d_fed, d_pool, d_input,
				  sys_arch_tcpip_mbox_count(),
				  (unsigned)mmc.rx_fifo_overflow);
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
	board_log("cmsis2: threads created, starting kernel\n");
	(void)osKernelStart();
	return 0;	/* unreachable */
}

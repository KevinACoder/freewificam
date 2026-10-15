/**
 * @file    gmac_dataplane.c
 * @brief   Case gmac-up + the wired-line console commands (GMAC + lwIP).
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  case row of the regression table (tests/board/common/board_suite.h)
 *          plus the shell commands the bring-up session types at. The
 *          bring-up order and the console command shapes are the settled
 *          sequence of the data-plane milestone - clocks/iomux/RGMII then the
 *          PHY (board/rk3568-itx/drivers/eqos_rk3568.c), then the netif
 *          bridge, then the stack's own entry point - ref the shell-command
 *          shape of this repository (ports/cherrysh/console_board.c) and the
 *          stack's own API (lwip/tcpip.h tcpip_init; lwiperf.h server/client
 *          entries; contrib ping.h init/stop). Threading follows the
 *          bridge's contract: the GMAC ISR signals, an RX thread polls the
 *          driver's staging ring into tcpip_input, the TCP/IP thread runs the
 *          stack, and a service thread sweeps the driver watchdog and prints
 *          the heartbeat.
 *
 * The wired line is the data plane under measurement: one port only
 * (gmac1, the board's reliable port; the driver table carries both, the
 * single-port rule arms one), static address 192.168.0.200/24, and the
 * lwiperf TCP server up so a host can push the reverse direction at will.
 */

#include <stddef.h>
#include <string.h>

#include "board_suite.h"

#include "os/cmsis2/cmsis2.h"

#include "lwip/ip4_addr.h"
#include "lwip/tcpip.h"
#include "lwip/etharp.h"
#include "lwip/stats.h"
#include "lwip/apps/lwiperf.h"
#include "ping.h"

#include "log.h"

#include "dwc_eqos.h"
#include "eqos_rk3568.h"
#include "rtl8211f.h"
#include "ethnetif.h"

#include "csh.h"

#include "lwip_stack.h"

/* The measured line. CONFIG_GMAC_PORT-style single-port rule: only this
 * index ever reaches the driver. */
#define GMAC_DP_PORT		1u
#define GMAC_WATCHDOG_MS	100u

static struct netif g_netif;
static unsigned int g_port;
static osSemaphoreId_t g_rx_sem;
static void *g_client_session;
static volatile int g_up;
/* Heartbeat gate for measurement rounds: the periodic line costs the UART
 * ~8 ms at 115200 with interrupts masked while it drains, which is real time
 * taken from the flow under measurement. The driver watchdog sweep keeps
 * running either way; only the print stops. */
static volatile int g_hb_on = 1;

/* --- bring-up ----------------------------------------------------------------- */

/* GMAC ISR context: frames landed in the driver's staging ring - wake the RX
 * thread (osSemaphoreRelease is ISR-safe on both CMSIS backends). */
static void rx_notify(unsigned int index)
{
	(void)index;
	(void)osSemaphoreRelease(g_rx_sem);
}

/* RX thread: drain the staging ring into the TCP/IP thread's mailbox. */
static void thr_rx(void *arg)
{
	(void)arg;
	for (;;) {
		(void)osSemaphoreAcquire(g_rx_sem, osWaitForever);
		(void)ethnetif_poll(&g_netif);
	}
}

/* lwiperf report callback (TCP/IP thread): one line per session event, the
 * bytes/ms/kbitps triple the measurements are read from. */
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
	if ((type == LWIPERF_TCP_DONE_CLIENT) ||
	    (type == LWIPERF_TCP_ABORTED_LOCAL) ||
	    (type == LWIPERF_TCP_ABORTED_LOCAL_TXERROR) ||
	    (type == LWIPERF_TCP_ABORTED_REMOTE)) {
		g_client_session = NULL;
	}
	LOG_INFO("lwiperf: report type=%d bytes=%u ms=%u %ukbit/s\n",
		  (int)type, (unsigned)bytes, (unsigned)ms, (unsigned)kbitpsec);
}

/* Service thread: driver watchdog sweep (the sweep re-arms and re-pokes; the
 * ISR does the hot path) plus the 1 s heartbeat the measurements are read
 * from. */
static void thr_service(void *arg)
{
	struct dwc_eqos_stats stats;
	struct dwc_eqos_mmc_stats mmc;
	unsigned long d_poll, d_pool, d_input, d_fed;
	uint32_t wd_due;
	uint32_t hb_due;

	(void)arg;
	wd_due = osKernelGetTickCount() + GMAC_WATCHDOG_MS;
	hb_due = osKernelGetTickCount() + 1000u;
	for (;;) {
		if ((int32_t)(osKernelGetTickCount() - wd_due) >= 0) {
			wd_due = osKernelGetTickCount() + GMAC_WATCHDOG_MS;
			dwc_eqos_watchdog(g_port);
		}
		if ((int32_t)(osKernelGetTickCount() - hb_due) >= 0) {
			hb_due = osKernelGetTickCount() + 1000u;
			if (!g_hb_on) {
				continue;
			}
			dwc_eqos_get_stats(g_port, &stats);
			dwc_eqos_get_mmc_stats(g_port, &mmc);
			ethnetif_get_diag(&d_poll, &d_pool, &d_input, &d_fed);
			LOG_INFO("gmac%u: hb rx=%u err=%u tx=%u txdrop=%u txerr=%u dmaerr=%u "
				  "| fed=%lu poolfail=%lu inputfail=%lu mmc_ovf=%u mmc_crc=%u\n",
				  g_port,
				  (unsigned)stats.rx_frames, (unsigned)stats.rx_errors,
				  (unsigned)stats.tx_frames, (unsigned)stats.tx_dropped,
				  (unsigned)stats.tx_errors, (unsigned)stats.dma_errors,
				  d_fed, d_pool, d_input,
				  (unsigned)mmc.rx_fifo_overflow,
				  (unsigned)mmc.rx_crc_error);
		}
		(void)osDelay(10);
	}
}

/* One GMAC port, up to "link up, channel running". */
static int gmac_bring_up(unsigned int port)
{
	const struct dwc_eqos_plat *plat = eqos_board_plat(port);
	eqos_link_info_t link;
	uint32_t configure;

	if (plat == NULL) {
		LOG_INFO("gmac: no plat for port %u\n", (unsigned)port);
		return -1;
	}
	if (dwc_eqos_initialize(port, plat) != EQOS_OK) {
		LOG_ERROR("gmac%u: initialize FAIL\n", (unsigned)port);
		return -1;
	}
	if (dwc_eqos_power_control(port, EQOS_POWER_FULL) != EQOS_OK) {
		LOG_ERROR("gmac%u: power FAIL\n", (unsigned)port);
		return -1;
	}
	if (rtl8211f_initialize(port, (void *)(uintptr_t)port,
				eqos_board_mdio_read, eqos_board_mdio_write) != EQOS_OK) {
		LOG_ERROR("phy%u: initialize FAIL\n", (unsigned)port);
		return -1;
	}
	if (rtl8211f_power_control(port, EQOS_POWER_FULL) != EQOS_OK) {
		LOG_ERROR("phy%u: power FAIL\n", (unsigned)port);
		return -1;
	}
	(void)rtl8211f_set_interface(port, EQOS_INTERFACE_RGMII);
	if (rtl8211f_set_mode(port,
			      EQOS_PHY_AUTO_NEGOTIATE | EQOS_PHY_SPEED_1G) != EQOS_OK) {
		LOG_WARNING("phy%u: no link (autoneg timeout)\n", (unsigned)port);
		return -1;
	}

	link = rtl8211f_get_link_info(port);
	configure = link.speed | link.duplex;
	(void)dwc_eqos_control(port, DWC_EQOS_CONFIGURE, configure);
	(void)dwc_eqos_control(port, DWC_EQOS_CONTROL_TX, 1);
	(void)dwc_eqos_control(port, DWC_EQOS_CONTROL_RX, 1);
	LOG_INFO("gmac%u: channel start\n", (unsigned)port);

	return 0;
}

/* Bring the wired line up: driver, PHY, the netif bridge on the running
 * stack, and the two service threads. Idempotent per boot. */
static int gmac_line_up(void)
{
	ip4_addr_t ip, mask, gw;
	uint8_t mac[6];

	if (g_up) {
		return 0;
	}
	g_port = GMAC_DP_PORT;

	if (gmac_bring_up(g_port) != 0) {
		return -1;
	}

	g_rx_sem = osSemaphoreNew(0xFFFFu, 0u, NULL);
	if (g_rx_sem == NULL) {
		LOG_ERROR("lan: rx semaphore FAIL\n");
		return -1;
	}
	dwc_eqos_set_rx_notify(rx_notify);
	if (osThreadNew(thr_rx, NULL,
			&(osThreadAttr_t){ .name = "gmacrx",
					   .priority = osPriorityNormal }) == NULL) {
		LOG_ERROR("lan: rx thread FAIL\n");
		return -1;
	}

	if (lwip_stack_up() != 0) {
		LOG_ERROR("lan: stack FAIL\n");
		return -1;
	}

	IP4_ADDR(&ip, 192, 168, 0, 200);
	IP4_ADDR(&mask, 255, 255, 255, 0);
	IP4_ADDR(&gw, 192, 168, 0, 1);

	LOCK_TCPIP_CORE();
	ethnetif_add(&g_netif, g_port, &ip, &mask, &gw);
	if (lwiperf_start_tcp_server_default(lwiperf_report, NULL) != NULL) {
		LOG_INFO("lwiperf: tcp server start\n");
	} else {
		LOG_ERROR("lwiperf: server START FAILED\n");
	}
	UNLOCK_TCPIP_CORE();

	(void)dwc_eqos_get_mac_address(g_port, mac);
	LOG_INFO("net: up gmac%u ip=%u.%u.%u.%u/24 mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		  g_port, ip4_addr1_16(&ip), ip4_addr2_16(&ip),
		  ip4_addr3_16(&ip), ip4_addr4_16(&ip),
		  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	if (osThreadNew(thr_service, NULL,
			&(osThreadAttr_t){ .name = "lansvc",
					   .priority = osPriorityNormal }) == NULL) {
		LOG_ERROR("lan: service thread FAIL\n");
		return -1;
	}

	g_up = 1;
	LOG_INFO("gmac%u: dataplane up\n", g_port);

	return 0;
}

/* --- the case ------------------------------------------------------------------ */

static void gmac_up(void)
{
	if (gmac_line_up() != 0) {
		board_case_fail("gmac1 bring-up failed");
	}
}

BOARD_TEST("gmac-up", gmac_up);

/* --- console commands ---------------------------------------------------------- */

/* `gmac net`: the stack's own view - the ARP table and the protocol counters.
 * "Did the request leave / did a reply come back" is answered here without a
 * debugger: etharp.xmit/recv separate the ARP legs, ip.recv/chkerr/drop the IP
 * leg, icmp.* the ICMP layer, link.* the frames below it. */
static void net_print_stack(void)
{
	size_t i;

	for (i = 0; i < ARP_TABLE_SIZE; i++) {
		ip4_addr_t *ip = NULL;
		struct netif *nif = NULL;
		struct eth_addr *eth = NULL;

		if (!etharp_get_entry(i, &ip, &nif, &eth) || (ip == NULL) || (eth == NULL)) {
			continue;
		}
		LOG_INFO("net: arp[%u] %u.%u.%u.%u -> %02x:%02x:%02x:%02x:%02x:%02x\n",
			  (unsigned)i,
			  ip4_addr1_16(ip), ip4_addr2_16(ip), ip4_addr3_16(ip), ip4_addr4_16(ip),
			  eth->addr[0], eth->addr[1], eth->addr[2], eth->addr[3],
			  eth->addr[4], eth->addr[5]);
	}

#if LWIP_STATS
	LOG_INFO("net: stats etharp tx=%u rx=%u drop=%u | ip rx=%u chkerr=%u drop=%u tx=%u | "
		  "icmp rx=%u chkerr=%u drop=%u tx=%u | link rx=%u tx=%u drop=%u memerr=%u\n",
		  (unsigned)lwip_stats.etharp.xmit, (unsigned)lwip_stats.etharp.recv,
		  (unsigned)lwip_stats.etharp.drop,
		  (unsigned)lwip_stats.ip.recv, (unsigned)lwip_stats.ip.chkerr,
		  (unsigned)lwip_stats.ip.drop, (unsigned)lwip_stats.ip.xmit,
		  (unsigned)lwip_stats.icmp.recv, (unsigned)lwip_stats.icmp.chkerr,
		  (unsigned)lwip_stats.icmp.drop, (unsigned)lwip_stats.icmp.xmit,
		  (unsigned)lwip_stats.link.recv, (unsigned)lwip_stats.link.xmit,
		  (unsigned)lwip_stats.link.drop, (unsigned)lwip_stats.link.memerr);
#endif
}

/* `gmac stat`: one line, the counters the measurements read. */
static int cmd_gmac(int argc, char **argv)
{
	chry_shell_t *csh = (void *)argv[argc + 1];
	struct dwc_eqos_stats stats;
	struct dwc_eqos_mmc_stats mmc;
	unsigned long d_poll, d_pool, d_input, d_fed;

	if (argc >= 2 && strcmp(argv[1], "net") == 0) {
		net_print_stack();
		return 0;
	}
	/* `gmac diag`: the driver's black box in one line - ring indices, the
	 * live channel status and interrupt-enable words, the odd-pass /
	 * breaker counters, and the TX-ladder discriminators (how often a
	 * sender found the ring full, how long it parked, how deep the ring
	 * ran). A measurement round samples it between phases to see WHEN a
	 * direction degrades and which bit moved. */
	if (argc >= 2 && strcmp(argv[1], "diag") == 0) {
		struct dwc_eqos_diag d;

		dwc_eqos_get_diag(g_port, &d);
		csh_printf(csh, "gmac%u: diag txhead=%u txtail=%u ioc=%u txsc=%u "
			   "twait=%u tticks=%u toccmax=%u "
			   "rrdy=%u rfree=%u "
			   "irq=%u odd=%u brk=%u st=%08x ien=%08x pend=%d last=%08x\n",
			   g_port, (unsigned)d.tx_head, (unsigned)d.tx_tail,
			   (unsigned)d.tx_ioc_seq, (unsigned)d.tx_sem_count,
			   (unsigned)d.tx_waits, (unsigned)d.tx_wait_ticks,
			   (unsigned)d.tx_occ_max,
			   (unsigned)d.rx_ready_count, (unsigned)d.rx_free_count,
			   (unsigned)d.irq_n, (unsigned)d.irq_odd,
			   (unsigned)d.irq_breaker, (unsigned)d.ch0_status,
			   (unsigned)d.intr_enable, (int)d.gic_pending,
			   (unsigned)d.last_status);
		return 0;
	}
	if ((argc >= 3) && (strcmp(argv[1], "riwt") == 0)) {
		unsigned long v = strtoul(argv[2], NULL, 0);

		dwc_eqos_set_rx_riwt(g_port, (uint32_t)v);
		csh_printf(csh, "gmac: riwt %lu readback %lu\n", v,
			   (unsigned long)dwc_eqos_get_rx_riwt(g_port));
		return 0;
	}
	if ((argc >= 3) && (strcmp(argv[1], "hb") == 0)) {
		g_hb_on = (strcmp(argv[2], "on") == 0);
		csh_printf(csh, "gmac: heartbeat %s\n", g_hb_on ? "on" : "off");
		return 0;
	}

	if (!g_up) {
		csh_printf(csh, "gmac: line is not up (run at boot by the gmac-up case)\n");
		return 1;
	}
	dwc_eqos_get_stats(g_port, &stats);
	dwc_eqos_get_mmc_stats(g_port, &mmc);
	ethnetif_get_diag(&d_poll, &d_pool, &d_input, &d_fed);
	csh_printf(csh, "gmac%u: rx=%u err=%u tx=%u txdrop=%u txerr=%u dmaerr=%u irq=%u "
			"csdrop=%u | fed=%lu poll=%lu poolfail=%lu inputfail=%lu | "
			"mmc pkts=%u ovf=%u crc=%u\n",
		   g_port,
		   (unsigned)stats.rx_frames, (unsigned)stats.rx_errors,
		   (unsigned)stats.tx_frames, (unsigned)stats.tx_dropped,
		   (unsigned)stats.tx_errors, (unsigned)stats.dma_errors,
		   (unsigned)stats.rx_irqs, (unsigned)stats.rx_csum_drops,
		   d_fed, d_poll, d_pool, d_input,
		   (unsigned)mmc.rx_pkts_gb, (unsigned)mmc.rx_fifo_overflow,
		   (unsigned)mmc.rx_crc_error);
	return 0;
}

/* `ping <ip>` / `ping stop`: the upstream contrib ping (socket mode, its own
 * thread). One receive line per echo reply lands on the console.
 *
 * The target lives in static storage because the app keeps the POINTER:
 * ping_init stores its argument (contrib/apps/ping/ping.c:403-408,
 * "ping_target = ping_addr") and the ping thread reads it asynchronously, so
 * a stack local here would dangle and the thread would ping whatever memory
 * the address now points at. */
static ip_addr_t ping_target_store;

static int cmd_ping(int argc, char **argv)
{
	chry_shell_t *csh = (void *)argv[argc + 1];

	if (argc >= 2 && strcmp(argv[1], "stop") == 0) {
		ping_stop();
		csh_printf(csh, "ping: stopped\n");
		return 0;
	}
	if ((argc < 2) || !ipaddr_aton(argv[1], &ping_target_store)) {
		csh_printf(csh, "usage: ping <ip> | ping stop\n");
		return 1;
	}
	if (!g_up) {
		csh_printf(csh, "ping: line is not up\n");
		return 1;
	}
	ping_init(&ping_target_store);
	return 0;
}

/* `iperf <ip>` / `iperf server` / `iperf stop`: lwiperf's TCP client and
 * server (raw-API state machines on the TCP/IP thread; start/stop under the
 * core lock). The client session lasts its built-in 10 s and reports on the
 * console. */
static int cmd_iperf(int argc, char **argv)
{
	chry_shell_t *csh = (void *)argv[argc + 1];

	if (!g_up) {
		csh_printf(csh, "iperf: line is not up\n");
		return 1;
	}
	if (argc < 2) {
		csh_printf(csh, "usage: iperf <ip> | iperf server | iperf stop"
				"   (lwiperf; client lasts 10 s, report on console)\n");
		return 1;
	}
	if (strcmp(argv[1], "stop") == 0) {
		LOCK_TCPIP_CORE();
		if (g_client_session != NULL) {
			lwiperf_abort(g_client_session);
			g_client_session = NULL;
		}
		UNLOCK_TCPIP_CORE();
		csh_printf(csh, "iperf: stop requested\n");
		return 0;
	}
	if (strcmp(argv[1], "server") == 0) {
		LOCK_TCPIP_CORE();
		g_client_session = NULL;
		if (lwiperf_start_tcp_server_default(lwiperf_report, NULL) == NULL) {
			UNLOCK_TCPIP_CORE();
			csh_printf(csh, "iperf: server start FAILED\n");
			return 1;
		}
		UNLOCK_TCPIP_CORE();
		csh_printf(csh, "iperf: server on port 5001\n");
		return 0;
	}

	{
		ip_addr_t remote;

		if (!ipaddr_aton(argv[1], &remote)) {
			csh_printf(csh, "iperf: bad address %s\n", argv[1]);
			return 1;
		}
		LOCK_TCPIP_CORE();
		g_client_session = lwiperf_start_tcp_client(&remote,
							    LWIPERF_TCP_PORT_DEFAULT,
							    LWIPERF_CLIENT,
							    lwiperf_report, NULL);
		UNLOCK_TCPIP_CORE();
		if (g_client_session == NULL) {
			csh_printf(csh, "iperf: client start FAILED\n");
			return 1;
		}
		csh_printf(csh, "iperf: client vs %s started (10 s)\n", argv[1]);
	}
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_gmac, gmac, "gmac stat | gmac diag | gmac net | gmac hb on|off | gmac riwt <n>",
			  "gmac stat - driver counters; gmac diag - ring/status black box;\n"
			  "gmac net - ARP table + stack counters;\n"
			  "gmac hb on|off - quiet the 1 Hz heartbeat for a measurement round;\n"
			  "gmac riwt <n> - set the RX watchdog (coalescing batch size, 256-tick units)\n")
CSH_CMD_EXPORT_ALIAS_FULL(cmd_ping, ping, "ping <ip> | ping stop",
			  "ping <ip> - ICMP echo via the lwIP socket API\n")
CSH_CMD_EXPORT_ALIAS_FULL(cmd_iperf, iperf, "iperf <ip> | iperf server | iperf stop",
			  "iperf - lwiperf TCP client (10 s) / server / stop\n")

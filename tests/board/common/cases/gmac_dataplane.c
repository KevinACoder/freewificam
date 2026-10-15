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
#include "lwip/udp.h"
#include "ping.h"

#include "log.h"

#include "dwc_eqos.h"
#include "eqos_rk3568.h"
#include "rtl8211f.h"
#include "ethnetif.h"
#include "generic_timer.h"

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

/* --- UDP probe path (2026-10-18) -----------------------------------------------
 *
 * The decisive driver-vs-stack cut: a raw-API UDP sink on 5002 that only counts,
 * so a host flood arrives through the same driver, bridge and netif chain as
 * TCP but without congestion control, windowing, ACKs or retransmits. If UDP
 * lands at the same ceiling as TCP the wall is below the stack; if it runs much
 * higher the wall is TCP-state processing on the single core.
 *
 * The sink timestamps its own first and last datagram from the kernel tick so
 * the rate is computed board-side over the burst's own span - the harness's
 * phase boundaries are up to a second wide and would smear the number.
 * The source side (`gmac udpsend`) is the same shape in reverse: a tight
 * pbuf_realloc + udp_sendto loop, printing bytes and its own elapsed ticks. */
#define GMAC_UDP_SINK_PORT	5002u

static struct udp_pcb *g_udp_pcb;
static volatile uint32_t g_udp_pkts;
static volatile uint32_t g_udp_bytes;
static volatile uint32_t g_udp_first_tick;
static volatile uint32_t g_udp_last_tick;

/* --- idle-time gauge (2026-10-19, the PMCCNTR replacement) ---------------------
 *
 * The cycle-counter route is closed on this board: WFI stops PMCCNTR, and both
 * CMSIS backends idle by busy polling (the board's tx_user.h leaves the WFI
 * option deliberately undefined), so a "how much of the single core is really
 * idle" number has to come from the scheduler instead of the PMU. The gauge is
 * a lowest priority thread whose loop only advances a counter: it runs exactly
 * when nothing else can, so its spin rate over a window, divided by the rate
 * measured while the line is quiet (`gmac idle cal`), IS the idle fraction.
 * The loop body is one increment - the cheapest thing a thread can do - so the
 * gauge cannot hide work behind itself. */
static volatile uint64_t g_idle_spins;
static uint64_t g_idle_cal_spm;		/* spins per millisecond, quiet line */
static uint32_t g_idle_cal_ms;

static void thr_idle(void *arg)
{
	(void)arg;
	for (;;) {
		g_idle_spins++;
	}
}

static void udp_sink_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
			  const ip_addr_t *addr, u16_t port)
{
	(void)arg;
	(void)pcb;
	(void)addr;
	(void)port;
	if (p == NULL) {
		return;
	}
	if (g_udp_pkts == 0u) {
		g_udp_first_tick = osKernelGetTickCount();
	}
	g_udp_last_tick = osKernelGetTickCount();
	g_udp_pkts++;
	g_udp_bytes += p->tot_len;
	pbuf_free(p);
}

/* --- bring-up ----------------------------------------------------------------- */

/* GMAC ISR context: frames landed in the driver's staging ring - wake the RX
 * thread (osSemaphoreRelease is ISR-safe on both CMSIS backends). */
static void rx_notify(unsigned int index)
{
	(void)index;
	(void)osSemaphoreRelease(g_rx_sem);
}

/* RX sweep context, mirroring the driver's DWC_EQOS_RX_BH: default = the bottom
 * half (the ISR signals, this thread sweeps); -DCONFIG_GMAC_RX_BH=0 restores
 * the interrupt-context sweep. CONFIG_GMAC_RX_NAPI implies the bottom half and
 * adds the budget + interrupt mask. */
#if !defined(CONFIG_GMAC_RX_BH)
#define GMAC_RX_BH	1
#else
#define GMAC_RX_BH	CONFIG_GMAC_RX_BH
#endif
#if defined(CONFIG_GMAC_RX_NAPI) && CONFIG_GMAC_RX_NAPI
#undef GMAC_RX_BH
#define GMAC_RX_BH	1
#endif

/* RX thread priority. The default is Normal(24), BELOW the TCP/IP thread's
 * osPriorityHigh(40) (lwipopts.h:100): every mailbox post preempts this thread,
 * and the ACK path starves behind the send loop that runs on the stack thread.
 * CONFIG_GMAC_RX_PRIO_HI puts this thread above the stack instead - the Linux
 * shape, where RX runs in softirq above process context. The core lock is a
 * prio-inherit mutex (ports/lwip/sys_arch.c), so a preemption while the stack
 * holds it cannot invert. */
#if defined(CONFIG_GMAC_RX_PRIO_HI) && CONFIG_GMAC_RX_PRIO_HI
#define GMAC_RX_THREAD_PRIO	osPriorityRealtime
#else
#define GMAC_RX_THREAD_PRIO	osPriorityNormal
#endif

/* RX thread: drain the staging ring into the TCP/IP thread's mailbox. */
static void thr_rx(void *arg)
{
	(void)arg;
	for (;;) {
#if defined(CONFIG_GMAC_RX_POLL) && CONFIG_GMAC_RX_POLL
		/* No DMA interrupt: this thread drives the whole RX path. */
		dwc_eqos_watchdog(g_port);
		(void)ethnetif_poll(&g_netif);
		(void)osThreadYield();
#elif defined(CONFIG_GMAC_RX_NAPI) && CONFIG_GMAC_RX_NAPI
		/* NAPI shape: the ISR masked the RX interrupt and signalled. Drain
		 * under a frame budget; the driver hands the interrupt back when a
		 * sweep finds the ring empty. */
		(void)osSemaphoreAcquire(g_rx_sem, osWaitForever);
		(void)dwc_eqos_rx_poll(g_port, CONFIG_GMAC_RX_BUDGET);
		(void)ethnetif_poll(&g_netif);
#elif GMAC_RX_BH
		/* Bottom half: the ISR signalled; run the sweep here. */
		(void)osSemaphoreAcquire(g_rx_sem, osWaitForever);
		dwc_eqos_watchdog(g_port);
		(void)ethnetif_poll(&g_netif);
#else
		(void)osSemaphoreAcquire(g_rx_sem, osWaitForever);
		(void)ethnetif_poll(&g_netif);
#endif
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
					   .priority = GMAC_RX_THREAD_PRIO }) == NULL) {
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
	g_udp_pcb = udp_new();
	if ((g_udp_pcb != NULL) &&
	    (udp_bind(g_udp_pcb, IP_ANY_TYPE, GMAC_UDP_SINK_PORT) == ERR_OK)) {
		udp_recv(g_udp_pcb, udp_sink_recv, NULL);
		LOG_INFO("udp: sink bound :%u\n", GMAC_UDP_SINK_PORT);
	} else {
		LOG_ERROR("udp: sink BIND FAILED\n");
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
	if (osThreadNew(thr_idle, NULL,
			&(osThreadAttr_t){ .name = "gmacidle",
					   .priority = osPriorityLow }) == NULL) {
		LOG_ERROR("lan: idle gauge thread FAIL\n");
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
	/* `gmac prof`: the service-time profile (struct dwc_eqos_prof). The
	 * counters are cumulative, so a measurement round samples this before
	 * and after a phase and diffs: the wired ladder needs to know which
	 * bucket the single core's time actually goes to, not which one looks
	 * expensive. Units are microseconds of system-counter time. */
	if (argc >= 2 && strcmp(argv[1], "prof") == 0) {
		struct dwc_eqos_prof p;
		uint64_t prep_ticks, input_ticks;
		unsigned long input_calls;

		if (argc >= 3 && strcmp(argv[2], "reset") == 0) {
			dwc_eqos_reset_prof(g_port);
			csh_printf(csh, "gmac%u: prof reset\n", g_port);
			return 0;
		}
		dwc_eqos_get_prof(g_port, &p);
		ethnetif_get_stack_prof(&prep_ticks, &input_ticks, &input_calls);
		csh_printf(csh, "gmac%u: prof irq=%u odd=%u rxsweep=%u rxcache=%u "
			   "rxcopy=%u txdone=%u txtot=%u txcopy=%u txflush=%u "
			   "rxwalk=%u walkslots=%llu sweeps=%llu "
			   "bridgeprep=%u bridgeinput=%u bridgecalls=%lu "
			   "irqn=%u rx=%u tx=%u\n",
			   g_port,
			   (unsigned)(p.irq_ticks / 24u), (unsigned)(p.irq_odd_ticks / 24u),
			   (unsigned)(p.irq_rx_sweep_ticks / 24u),
			   (unsigned)(p.irq_rx_cache_ticks / 24u),
			   (unsigned)(p.irq_rx_copy_ticks / 24u),
			   (unsigned)(p.irq_tx_done_ticks / 24u),
			   (unsigned)(p.tx_total_ticks / 24u),
			   (unsigned)(p.tx_memcpy_ticks / 24u),
			   (unsigned)(p.tx_flush_ticks / 24u),
			   (unsigned)(p.rx_walk_ticks / 24u),
			   (unsigned long long)p.rx_walk_slots,
			   (unsigned long long)p.rx_sweeps,
			   (unsigned)(prep_ticks / 24u),
			   (unsigned)(input_ticks / 24u), input_calls,
			   (unsigned)p.irq_entries, (unsigned)p.rx_frames,
			   (unsigned)p.tx_frames);
		return 0;
	}

	/* `gmac ring`: the RX ring's producer/consumer boundary as the hardware
	 * reports it, next to what software believes (struct dwc_eqos_ring). The
	 * tail pointer's landing point is the thing two cursor walks died on, so
	 * this prints where the DMA says it is (CUR_RX_DESC / CUR_RX_BUF_ADDR),
	 * where the window ends (RX_END), and who owns which slot. Sample it
	 * before and after a phase: `stale` non-zero means the bounded walk is
	 * leaking slots, `run` is what it covers. */
	if (argc >= 2 && strcmp(argv[1], "ring") == 0) {
		struct dwc_eqos_ring r;

		if (dwc_eqos_get_ring(g_port, &r) != EQOS_OK) {
			csh_printf(csh, "gmac%u: ring unavailable\n", g_port);
			return 1;
		}
		csh_printf(csh, "gmac%u: ring base=%08x end=%08x ends=%d rlen=%u "
			   "curdesc=%08x curs=%d curbuf=%08x rxctl=%08x sr=%u "
			   "st=%08x\n",
			   g_port, (unsigned)r.rx_base, (unsigned)r.rx_end,
			   (int)r.end_slot, (unsigned)r.rx_ring_len,
			   (unsigned)r.cur_rx_desc, (int)r.cur_desc_slot,
			   (unsigned)r.cur_rx_buf, (unsigned)r.rx_control,
			   (unsigned)(r.rx_control & 1u), (unsigned)r.ch0_status);
		csh_printf(csh, "gmac%u: ring scan=%d lastarmed=%d armed=%u "
			   "ownclear=%u run=%u stale=%u\n",
			   g_port, (int)r.rx_scan, (int)r.last_armed,
			   (unsigned)r.ring_armed, (unsigned)r.own_clear,
			   (unsigned)r.walk_run, (unsigned)r.stale_slots);
		csh_printf(csh, "gmac%u: ring inflight=%08x %08x %08x %08x "
			   "dmaown=%08x %08x %08x %08x\n",
			   g_port,
			   (unsigned)r.in_flight_mask[0], (unsigned)r.in_flight_mask[1],
			   (unsigned)r.in_flight_mask[2], (unsigned)r.in_flight_mask[3],
			   (unsigned)r.dma_owned_mask[0], (unsigned)r.dma_owned_mask[1],
			   (unsigned)r.dma_owned_mask[2], (unsigned)r.dma_owned_mask[3]);
		/* What the LAST sweep saw, recorded mid-traffic: the phase-boundary
		 * snapshot can only ever see a drained ring, so a collapse has to be
		 * explained from these. `clear` far above `walkrun` means clear slots
		 * sat outside the walked range; `maxclear` is the worst such case. */
		csh_printf(csh, "gmac%u: ring sweeps=%u clear=%u maxclear=%u span=%u "
			   "swcur=%d swarmed=%d endgap=%d\n",
			   g_port, (unsigned)r.sw_sweeps, (unsigned)r.sw_clear_seen,
			   (unsigned)r.sw_max_clear, (unsigned)r.sw_span,
			   (int)r.sw_curdesc, (int)r.sw_last_armed, (int)r.end_gap);
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
	/* `gmac regs`: the live MAC/DMA register picture, same offsets the Linux
	 * lane dumps, so the parity round diffs hardware against hardware. One
	 * line, name:value cells; the harness keys on the `gmacN: regs ` prefix. */
	if (argc >= 2 && strcmp(argv[1], "regs") == 0) {
		struct dwc_eqos_regs r;

		if (dwc_eqos_get_regs(g_port, &r) != EQOS_OK) {
			csh_printf(csh, "gmac%u: regs unavailable\n", g_port);
			return 1;
		}
		csh_printf(csh, "gmac%u: regs macconf=%08x pktflt=%08x "
			   "q0txfc=%08x rxfc=%08x feat0=%08x feat1=%08x "
			   "mtltxq0=%08x mtlrxq0=%08x dmamode=%08x sysbus=%08x "
			   "ch0ctl=%08x txtrl=%08x rxctl=%08x "
			   "txend=%08x rxend=%08x txrlen=%08x rxrlen=%08x "
			   "ien=%08x riwt=%08x currx=%08x status=%08x\n",
			   g_port,
			   (unsigned)r.mac_configuration, (unsigned)r.mac_packet_filter,
			   (unsigned)r.mac_q0_tx_flow_ctrl, (unsigned)r.mac_rx_flow_ctrl,
			   (unsigned)r.hw_feature0,
			   (unsigned)r.hw_feature1,
			   (unsigned)r.mtl_txq0_op, (unsigned)r.mtl_rxq0_op,
			   (unsigned)r.dma_mode, (unsigned)r.sysbus_mode,
			   (unsigned)r.ch0_control, (unsigned)r.ch0_tx_control,
			   (unsigned)r.ch0_rx_control,
			   (unsigned)r.ch0_tx_end, (unsigned)r.ch0_rx_end,
			   (unsigned)r.ch0_tx_ring_len, (unsigned)r.ch0_rx_ring_len,
			   (unsigned)r.ch0_intr_enable, (unsigned)r.ch0_rx_watchdog,
			   (unsigned)r.ch0_cur_rx_desc, (unsigned)r.ch0_status);
		return 0;
	}
	/* `gmac udpstat [reset]`: the sink counters. Diff scheme and rate are
	 * derived board-side; reset starts a fresh burst window. */
	if (argc >= 2 && strcmp(argv[1], "udpstat") == 0) {
		uint32_t pkts, bytes, first, last;

		if (argc >= 3 && strcmp(argv[2], "reset") == 0) {
			g_udp_pkts = 0u;
			g_udp_bytes = 0u;
			g_udp_first_tick = 0u;
			g_udp_last_tick = 0u;
			csh_printf(csh, "gmac%u: udpstat reset\n", g_port);
			return 0;
		}
		pkts = g_udp_pkts;
		bytes = g_udp_bytes;
		first = g_udp_first_tick;
		last = g_udp_last_tick;
		csh_printf(csh, "gmac%u: udpstat pkts=%u bytes=%u span_ms=%u "
			   "rate_mbit=%u first=%u last=%u\n",
			   g_port, (unsigned)pkts, (unsigned)bytes,
			   (unsigned)((pkts > 1u) ? (last - first) : 0u),
			   (unsigned)((pkts > 1u) && (last != first)
				      ? ((uint64_t)bytes * 8u)
					/ ((uint64_t)(last - first) * 1000u)
				      : 0u),
			   (unsigned)first, (unsigned)last);
		return 0;
	}
	/* `gmac udpsend <ip> [ms]`: the reverse UDP leg - a tight raw-API send
	 * loop at 1472-byte payloads, no ACKs, no window. The rate it reaches is
	 * the transmit-half answer to the sink's receive-half one. Runs on the
	 * console thread with the core lock per packet (the locking shape the
	 * stack's own callers use), so its number is the path's, not a
	 * specially-optimized one's. */
	if ((argc >= 3) && (strcmp(argv[1], "udpsend") == 0)) {
		ip_addr_t remote;
		struct udp_pcb *pcb;
		uint32_t duration_ms = 5000u;
		uint32_t t0, t1;
		uint32_t sent = 0u;
		uint32_t send_bytes = 0u;

		if (!g_up) {
			csh_printf(csh, "gmac: line is not up\n");
			return 1;
		}
		if (!ipaddr_aton(argv[2], &remote)) {
			csh_printf(csh, "gmac: udpsend bad address %s\n", argv[2]);
			return 1;
		}
		if (argc >= 4) {
			duration_ms = (uint32_t)strtoul(argv[3], NULL, 0);
		}
		LOCK_TCPIP_CORE();
		pcb = udp_new();
		UNLOCK_TCPIP_CORE();
		if (pcb == NULL) {
			csh_printf(csh, "gmac: udpsend pcb alloc FAILED\n");
			return 1;
		}
		t0 = osKernelGetTickCount();
		do {
			struct pbuf *p;

			LOCK_TCPIP_CORE();
			p = pbuf_alloc(PBUF_TRANSPORT, 1472, PBUF_POOL);
			if (p != NULL) {
				memset(p->payload, 0x5a, 1472);
				if (udp_sendto(pcb, p, &remote,
					       GMAC_UDP_SINK_PORT) == ERR_OK) {
					sent++;
					send_bytes += 1472u;
				}
				pbuf_free(p);
			}
			UNLOCK_TCPIP_CORE();
			t1 = osKernelGetTickCount();
		} while ((uint32_t)(t1 - t0) < duration_ms);
		LOCK_TCPIP_CORE();
		udp_remove(pcb);
		UNLOCK_TCPIP_CORE();
		csh_printf(csh, "gmac%u: udpsend pkts=%u bytes=%u span_ms=%u "
			   "rate_mbit=%u\n",
			   g_port, (unsigned)sent, (unsigned)send_bytes,
			   (unsigned)(t1 - t0),
			   (unsigned)((t1 != t0)
				      ? ((uint64_t)send_bytes * 8u)
					/ ((uint64_t)(t1 - t0) * 1000u)
				      : 0u));
		return 0;
	}

	/* `gmac tcp [reset]`: the TCP layer's own counters. The lwIP stats block
	 * is always compiled; MIB2's segment counters (out/in/fast-retransmit)
	 * come with the TCPPROBE=1 lever. Diff between phase boundaries: the
	 * retransmit delta is the direct read on whether the ceiling carries a
	 * recovery load (world b) or the flows are clean (worlds a/c). */
	if (argc >= 2 && strcmp(argv[1], "tcp") == 0) {
#if LWIP_STATS
		if (argc >= 3 && strcmp(argv[2], "reset") == 0) {
			memset(&lwip_stats.tcp, 0, sizeof(lwip_stats.tcp));
#if MIB2_STATS
			lwip_stats.mib2.tcpoutsegs = 0u;
			lwip_stats.mib2.tcpinsegs = 0u;
			lwip_stats.mib2.tcpretranssegs = 0u;
			lwip_stats.mib2.tcpoutrsts = 0u;
#endif
			csh_printf(csh, "gmac%u: tcp reset\n", g_port);
			return 0;
		}
		csh_printf(csh, "gmac%u: tcp recv=%u xmit=%u drop=%u chkerr=%u "
			   "memerr=%u lenerr=%u err=%u proterr=%u",
			   g_port,
			   (unsigned)lwip_stats.tcp.recv, (unsigned)lwip_stats.tcp.xmit,
			   (unsigned)lwip_stats.tcp.drop, (unsigned)lwip_stats.tcp.chkerr,
			   (unsigned)lwip_stats.tcp.memerr, (unsigned)lwip_stats.tcp.lenerr,
			   (unsigned)lwip_stats.tcp.err, (unsigned)lwip_stats.tcp.proterr);
#if MIB2_STATS
		csh_printf(csh, " outsegs=%u insegs=%u rexmit=%u outrst=%u",
			   (unsigned)lwip_stats.mib2.tcpoutsegs,
			   (unsigned)lwip_stats.mib2.tcpinsegs,
			   (unsigned)lwip_stats.mib2.tcpretranssegs,
			   (unsigned)lwip_stats.mib2.tcpoutrsts);
#endif
		csh_printf(csh, "\n");
#else
		csh_printf(csh, "gmac: LWIP_STATS off\n");
#endif
		return 0;
	}
	/* `gmac gap [reset]`: the arrival-rhythm instrument (see struct
	 * dwc_eqos_prof). Bucket k spans [2^(k-1), 2^k) system-counter ticks
	 * (41.7 ns): a smooth line-rate flow piles every sample into the low
	 * buckets, a paced/bursty one shows a fat tail at the ms buckets.
	 * hw = peak staged frames waiting for the reader. */
	if (argc >= 2 && strcmp(argv[1], "gap") == 0) {
		struct dwc_eqos_prof p;
		unsigned int i;

		if (argc >= 3 && strcmp(argv[2], "reset") == 0) {
			dwc_eqos_reset_rx_hist(g_port);
			csh_printf(csh, "gmac%u: gap reset\n", g_port);
			return 0;
		}
		dwc_eqos_get_prof(g_port, &p);
		csh_printf(csh, "gmac%u: gap n=%llu hw=%u",
			   g_port, (unsigned long long)p.rx_gap_samples,
			   (unsigned)p.rx_stage_high_water);
		for (i = 0; i < DWC_EQOS_PROF_HIST_BUCKETS; i++) {
			csh_printf(csh, " b%u=%llu", i,
				   (unsigned long long)p.rx_gap_hist[i]);
		}
		csh_printf(csh, "\n");
		return 0;
	}
	/* `gmac idle [cal]`: the real-idle gauge. The idle thread is the lowest
	 * priority thread in the image, so its spin count advances only when
	 * every other thread is blocked. `cal` measures the quiet-line rate
	 * (spins per millisecond) once; afterwards the harness diffs two plain
	 * samples around a phase: idle_pct = (dspins / dt_ms) * 100 / cal_spm.
	 * Plain samples print absolute counts, so no state is kept per phase. */
	if (argc >= 2 && strcmp(argv[1], "idle") == 0) {
		if (argc >= 3 && strcmp(argv[2], "cal") == 0) {
			uint64_t s0, s1, t0, t1;
			uint64_t elapsed_us;

			s0 = g_idle_spins;
			t0 = gtimer_count_get();
			(void)osDelay(200u);
			t1 = gtimer_count_get();
			s1 = g_idle_spins;
			elapsed_us = (t1 - t0) / 24u;
			g_idle_cal_spm = (elapsed_us != 0u)
				? ((s1 - s0) * 1000u) / elapsed_us : 0u;
			g_idle_cal_ms = (uint32_t)(elapsed_us / 1000u);
			csh_printf(csh, "gmac%u: idle cal spins=%llu ms=%u spm=%llu\n",
				   g_port, (unsigned long long)(s1 - s0),
				   (unsigned)g_idle_cal_ms,
				   (unsigned long long)g_idle_cal_spm);
			return 0;
		}
		csh_printf(csh, "gmac%u: idle spins=%llu ticks=%llu cal_spm=%llu\n",
			   g_port, (unsigned long long)g_idle_spins,
			   (unsigned long long)gtimer_count_get(),
			   (unsigned long long)g_idle_cal_spm);
		return 0;
	}
	/* `gmac shape [reset]`: what the TCP flow is made of, both directions:
	 * data segments, pure ACKs, and a duplicate-ACK proxy (a pure ACK whose
	 * (seq, ack) equals the previous one - three in a row trigger fast
	 * retransmit). This is the read that turns "ACK:data = 0.54" into
	 * "which shapes those 0.54 are". */
	if (argc >= 2 && strcmp(argv[1], "shape") == 0) {
		struct ethnetif_shape rx, tx;

		if (argc >= 3 && strcmp(argv[2], "reset") == 0) {
			ethnetif_reset_shape();
			csh_printf(csh, "gmac%u: shape reset\n", g_port);
			return 0;
		}
		ethnetif_get_shape(&rx, &tx);
		csh_printf(csh, "gmac%u: shape rx data=%lu ack=%lu ackdup=%lu ackwu=%lu "
			   "syn=%lu fin=%lu rst=%lu other=%lu | "
			   "tx data=%lu ack=%lu ackdup=%lu ackwu=%lu syn=%lu fin=%lu rst=%lu other=%lu\n",
			   g_port, rx.data, rx.pure_ack, rx.ack_dup, rx.ack_wupd,
			   rx.syn, rx.fin, rx.rst, rx.other,
			   tx.data, tx.pure_ack, tx.ack_dup, tx.ack_wupd,
			   tx.syn, tx.fin, tx.rst, tx.other);
		return 0;
	}

	/* `gmac etime`: per-thread execution-time attribution (EPK=1 builds; the
	 * upstream ThreadX Execution Profile Kit behind osExecProfileGet). One
	 * line per thread, ticks in the 24 MHz system counter - /24000 = ms. The
	 * totals answer "who eats the single core" directly, no inference from
	 * counters. `isr` and `idle` are the kit's accumulators; when the build
	 * has no profile kit the line says so instead of printing zeros. */
	if (argc >= 2 && strcmp(argv[1], "etime") == 0) {
		os_exec_profile_entry_t prof[OS_EXEC_PROFILE_MAX_ENTRIES];
		uint64_t isr_ticks = 0u, idle_ticks = 0u;
		size_t n, i;

		n = osExecProfileGet(prof, OS_EXEC_PROFILE_MAX_ENTRIES,
				     &isr_ticks, &idle_ticks);
		if (n == 0u && isr_ticks == 0u && idle_ticks == 0u) {
			csh_printf(csh, "gmac%u: etime unavailable (build without EPK)\n",
				   g_port);
			return 0;
		}
		csh_printf(csh, "gmac%u: etime isr_ms=%llu idle_ms=%llu",
			   g_port, (unsigned long long)(isr_ticks / 24000u),
			   (unsigned long long)(idle_ticks / 24000u));
		for (i = 0; i < n; i++) {
			csh_printf(csh, " %s=%llu",
				   prof[i].name ? prof[i].name : "?",
				   (unsigned long long)(prof[i].ticks / 24000u));
		}
		csh_printf(csh, "\n");
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
			"csdrop=%u rxdrop=%u txcsum=%u txcsumun=%u | fed=%lu poll=%lu poolfail=%lu inputfail=%lu | "
			"mmc pkts=%u ovf=%u crc=%u\n",
		   g_port,
		   (unsigned)stats.rx_frames, (unsigned)stats.rx_errors,
		   (unsigned)stats.tx_frames, (unsigned)stats.tx_dropped,
		   (unsigned)stats.tx_errors, (unsigned)stats.dma_errors,
		   (unsigned)stats.rx_irqs, (unsigned)stats.rx_csum_drops,
		   (unsigned)stats.rx_dropped,
		   (unsigned)stats.tx_csum_frames, (unsigned)stats.tx_csum_uncovered,
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

CSH_CMD_EXPORT_ALIAS_FULL(cmd_gmac, gmac, "gmac stat | gmac diag | gmac prof | gmac ring | gmac regs | gmac net | gmac tcp | gmac gap | gmac idle | gmac shape | gmac etime | gmac udpstat | gmac udpsend | gmac hb on|off | gmac riwt <n>",
			  "gmac stat - driver counters; gmac diag - ring/status black box;\n"
			  "gmac prof [reset] - service-time profile in us (cumulative; diff it);\n"
			  "gmac ring - RX ring snapshot (tail/DMA position/ownership);\n"
			  "gmac regs - live MAC/DMA register dump (Linux-parity table);\n"
			  "gmac net - ARP table + stack counters;\n"
			  "gmac tcp [reset] - TCP stats (+ MIB2 segment counters under TCPPROBE=1);\n"
			  "gmac gap [reset] - RX inter-arrival histogram (log2 buckets, 24 MHz ticks);\n"
			  "gmac idle [cal] - idle-thread spin gauge (cal = calibrate on a quiet line);\n"
			  "gmac shape [reset] - TX/RX TCP frame shapes (data/ACK/dup-ACK);\n"
			  "gmac etime - per-thread execution time in ms (EPK=1 builds);\n"
			  "gmac udpstat [reset] - UDP sink counters (burst rate board-side);\n"
			  "gmac udpsend <ip> [ms] - raw-API UDP send loop (default 5000 ms);\n"
			  "gmac hb on|off - quiet the 1 Hz heartbeat for a measurement round;\n"
			  "gmac riwt <n> - set the RX watchdog (coalescing batch size, 256-tick units)\n")
CSH_CMD_EXPORT_ALIAS_FULL(cmd_ping, ping, "ping <ip> | ping stop",
			  "ping <ip> - ICMP echo via the lwIP socket API\n")
CSH_CMD_EXPORT_ALIAS_FULL(cmd_iperf, iperf, "iperf <ip> | iperf server | iperf stop",
			  "iperf - lwiperf TCP client (10 s) / server / stop\n")

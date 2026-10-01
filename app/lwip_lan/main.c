/*
 * @file   main.c
 * @brief  lwip_lan bring-up main (S7 phase 1, NO_SYS=1, bare-metal base).
 *
 * Phase-1 shape (specs/lwip-adapt §3.1 条 5 / §3.7): the polled lwIP model
 * on the S1 bare-metal base - the tick runs at 1000 Hz, the app main loop
 * owns the stack and the driver (RX drain + sys_check_timeouts + the GMAC
 * watchdog sweep), and no lwIP thread exists. P7-2a switches the image to
 * NO_SYS=0 + sys_arch on the threadx base; this file is revisited then.
 *
 * Bring-up order (spec §3.5, the verified sequence): CRU/GRF/iomux + PHY
 * hard reset pulse (all inside the MAC's power-up via the SoC glue) ->
 * MAC soft reset + MAC interrupt clear/mask + DMA rings (eqos_hw_init) ->
 * PHY scan + soft reset + RTL8211F strap clear (the clear must follow the
 * last PHY reset - the double-delay red line) -> autonegotiation -> MAC
 * speed/duplex from the link info -> channel start. The driver prints the
 * anchor lines; this file only sequences the calls.
 *
 * The GMAC port is a build-time single choice (CONFIG_GMAC_PORT, KI-027 by
 * construction): the unselected port's plat is never handed to any driver,
 * so it gets no clock, no register write and no anchor line.
 *
 * Main loop (NO_SYS model): the ISR stages frames into the driver's staging
 * ring; this loop drains it (into lwIP from P7-1c on), runs the lwIP
 * timeouts and the 100 ms GMAC watchdog, and prints the 1 s heartbeat with
 * the driver counters - the P7-1b evidence line.
 */

#include <stdint.h>

#include "gicv3.h"
#include "log.h"
#include "lwip/init.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"
#include "lwip/apps/lwiperf.h"
#include "tick.h"
#include "uart.h"

#include "dwc_eqos.h"
#include "ethnetif.h"
#include "eqos_rk3568.h"
#include "rtl8211f.h"

#ifdef THREADX_BUILD
#error "lwip_lan phase 1 is the bare-metal NO_SYS image (spec S7 §3.1); \
P7-2a moves it to the threadx base"
#endif

#ifndef CONFIG_GMAC_PORT
#error "CONFIG_GMAC_PORT (0=gmac0, 1=gmac1) must come from the config"
#endif

/* The watchdog cadence the prior workspace's osTimer ran at. */
#define GMAC_WATCHDOG_MS	100u

/* Static addresses, the M1-A convention carried over (spec §3.7): gmac1 =
 * 192.168.0.200 (the judged port), gmac0 = 192.168.0.201, /24 test LAN. */
static struct netif g_netif;

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

static void net_bring_up(unsigned int port)
{
	ip4_addr_t ip, mask, gw;
	uint8_t mac[6];

	IP4_ADDR(&mask, 255, 255, 255, 0);
	IP4_ADDR(&gw, 192, 168, 0, 1);
	if (port == 1U) {
		IP4_ADDR(&ip, 192, 168, 0, 200);
	} else {
		IP4_ADDR(&ip, 192, 168, 0, 201);
	}

	ethnetif_add(&g_netif, port, &ip, &mask, &gw);
	(void)dwc_eqos_get_mac_address(port, mac);
	board_log("net: up gmac%u ip=%u.%u.%u.%u/24 mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		  port, ip4_addr1_16(&ip), ip4_addr2_16(&ip),
		  ip4_addr3_16(&ip), ip4_addr4_16(&ip),
		  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	if (lwiperf_start_tcp_server_default(lwiperf_report, NULL) != NULL) {
		board_log("lwiperf: tcp server start\n");
	} else {
		board_log("lwiperf: server START FAILED\n");
	}
}

/* Bring one GMAC port up to "link up, channel running". Returns 0 on
 * success; every failure prints its own anchor line first. */
static int gmac_bring_up(unsigned int port)
{
	const struct dwc_eqos_plat *plat = eqos_board_plat(port);
	eqos_link_info_t link;
	uint32_t configure;

	if (plat == NULL) {
		board_log("gmac: no plat for port %u\n", (unsigned)port);
		return -1;
	}

	/* MAC: init + power (soc_init does clocks/iomux/delays/reset pulse,
	 * then the SWR + MAC interrupt clear + DMA rings). */
	if (dwc_eqos_initialize(port, plat) != EQOS_OK) {
		board_log("gmac%u: initialize FAIL\n", (unsigned)port);
		return -1;
	}
	if (dwc_eqos_power_control(port, EQOS_POWER_FULL) != EQOS_OK) {
		board_log("gmac%u: power FAIL\n", (unsigned)port);
		return -1;
	}

	/* PHY: scan + soft reset + strap clear (borrows this port's MDIO),
	 * then autonegotiate and wait for the link (spec: the strap clear
	 * must be the last reset-adjacent write - rtl8211f.c owns that
	 * ordering). */
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

	/* MAC side of the negotiated link, then the channel. */
	link = rtl8211f_get_link_info(port);
	configure = link.speed | link.duplex;
	(void)dwc_eqos_control(port, DWC_EQOS_CONFIGURE, configure);
	(void)dwc_eqos_control(port, DWC_EQOS_CONTROL_TX, 1);
	(void)dwc_eqos_control(port, DWC_EQOS_CONTROL_RX, 1);
	board_log("gmac%u: channel start\n", (unsigned)port);

	/* Golden dump (P7-1b 判据 1): post-init CRU/GRF readback against the
	 * lab's U-Boot-leftover golden. */
	eqos_board_golden_dump(port);

	return 0;
}

int main(void)
{
	unsigned int port = (unsigned int)CONFIG_GMAC_PORT;
	unsigned int fed = 0;
	struct dwc_eqos_stats stats;
	uint32_t now, target;
	uint32_t wd_due;

	/* The polled console speaks only after a full re-program. */
	uart_early_init();
	gicv3_init();

	board_log("main: lwip_lan bring-up (baremetal NO_SYS=1)\n");
	board_log("lwip_lan: gmac port=%u single-port build\n", port);

	/* 1000 Hz (spec §3.1 条 5): sys_now's millisecond resolution, the
	 * driver's osDelay pacing and the watchdog cadence stand on it. */
	if (OS_Tick_Setup(1000U, 0) == 0) {
		board_log("tick: irq=%u freq=%uHz\n",
			  (unsigned)OS_Tick_GetIRQn(),
			  (unsigned)os_tick_freq_hz());
		board_log_stamp_use_tick();
		__asm__ __volatile__("msr daifclr, #2" ::: "memory");
		__asm__ __volatile__("isb" ::: "memory");
	} else {
		board_log("tick: SETUP FAILED\n");
		return 0;
	}

	/* GMAC first: the autoneg wait needs the tick, and the anchors must
	 * precede anything lwIP prints. P7-1b accepts "link up" even when
	 * the LAN has no partner yet is not the case - a timeout here is a
	 * P7-1b fail trigger, not a soft skip. */
	if (gmac_bring_up(port) != 0) {
		board_log("lwip_lan: gmac bring-up FAILED\n");
		return 0;
	}

	/* Stack init + version anchor (P7-1a anchors, kept for the cold-boot
	 * anchor-line comparison). */
	lwip_init();
	board_log("lwip: v%s init NO_SYS=1\n", LWIP_VERSION_STRING);

	/* sys_now liveness: the NO_SYS timebase must advance in real ms or
	 * every lwIP timeout is dead. Wait out 100 ms through it. */
	now = sys_now();
	target = now + 100U;
	while (sys_now() < target) {
		__asm__ __volatile__("wfi" ::: "memory");
	}
	board_log("lwip: sys_now alive (100ms in %ums)\n",
		  (unsigned)(sys_now() - now));

	/* lwiperf smoke server moved into net_bring_up (needs the netif for
	 * meaningful reports); keep the anchor order stable. */
	net_bring_up(port);

	/* Steady state: RX feed into lwIP, lwIP timeouts, the 100 ms driver
	 * watchdog, and the 1 s heartbeat with driver counters + the feed
	 * counter (P7-1c evidence line). */
	dwc_eqos_get_stats(port, &stats);
	board_log("gmac%u: rx=%u dropped=%u errors=%u tx=%u\n", port,
		  (unsigned)stats.rx_frames, (unsigned)stats.rx_dropped,
		  (unsigned)stats.rx_errors, (unsigned)stats.tx_frames);

	now = sys_now();
	wd_due = now + GMAC_WATCHDOG_MS;
	for (;;) {
		sys_check_timeouts();

		fed += ethnetif_poll(&g_netif);

		if ((int32_t)(sys_now() - wd_due) >= 0) {
			wd_due = sys_now() + GMAC_WATCHDOG_MS;
			dwc_eqos_watchdog(port);
		}

		if ((uint32_t)(sys_now() - now) >= 1000U) {
			now += 1000U;
			dwc_eqos_get_stats(port, &stats);
			board_log("gmac%u: hb rx=%u dropped=%u errors=%u tx=%u fed=%u\n",
				  port,
				  (unsigned)stats.rx_frames,
				  (unsigned)stats.rx_dropped,
				  (unsigned)stats.rx_errors,
				  (unsigned)stats.tx_frames,
				  (unsigned)fed);
		}
		__asm__ __volatile__("wfi" ::: "memory");
	}
	return 0;	/* unreachable: start.S parks if main returns */
}

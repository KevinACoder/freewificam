/*
 * @file   main.c
 * @brief  lwip_lan bring-up main (S7 phase 1, NO_SYS=1, bare-metal base).
 *
 * Phase-1 shape (specs/lwip-adapt §3.1 条 5 / §3.7): the polled lwIP model
 * on the S1 bare-metal base - the tick runs at 1000 Hz, the app main loop
 * owns the stack (sys_check_timeouts +, from P7-1b on, the RX drain into
 * the netif), and no lwIP thread exists. P7-2a switches the image to
 * NO_SYS=0 + sys_arch on the threadx base; this file is revisited then.
 *
 * Anchors this round (P7-1a, build-level round): lwip init with the version
 * string, a sys_now liveness proof (100 ms of advance), lwiperf server
 * start, 1 Hz heartbeat off sys_now. The GMAC port is a build-time single
 * choice (CONFIG_GMAC_PORT, KI-027 by construction) - the anchor names it
 * so the serial transcript proves which port this image owns.
 */

#include <stdint.h>

#include "gicv3.h"
#include "log.h"
#include "lwip/init.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"
#include "lwip/apps/lwiperf.h"
#include "tick.h"
#include "uart.h"

#ifdef THREADX_BUILD
#error "lwip_lan phase 1 is the bare-metal NO_SYS image (spec S7 §3.1); \
P7-2a moves it to the threadx base"
#endif

#ifndef CONFIG_GMAC_PORT
#error "CONFIG_GMAC_PORT (0=gmac0, 1=gmac1) must come from the config"
#endif

int main(void)
{
	uint32_t now, target;

	/* The polled console speaks only after a full re-program. */
	uart_early_init();
	gicv3_init();

	board_log("main: lwip_lan bring-up (baremetal NO_SYS=1)\n");
	board_log("lwip_lan: gmac port=%u single-port build\n",
		  (unsigned)CONFIG_GMAC_PORT);

	/* 1000 Hz (spec §3.1 条 5): sys_now's millisecond resolution and the
	 * lwiperf timeouts stand on it. */
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

	/* Stack init + version anchor (P7-1a 判据 1 expects lwip_init in the
	 * image; the printed string must read 2.2.1). */
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

	/* Smoke server (iperf2 protocol; host iperf2.exe is the paired
	 * instrument, spec FR-10). No netif exists yet (P7-1c) - the server
	 * just listens; the anchor proves the symbol path. */
	if (lwiperf_start_tcp_server_default(NULL, NULL) != NULL) {
		board_log("lwiperf: tcp server start\n");
	} else {
		board_log("lwiperf: server START FAILED\n");
	}

	/* Steady state: sys_check_timeouts + 1 Hz heartbeat. RX drain joins
	 * in P7-1b; the heartbeat counter turns into the rx/tx evidence line
	 * with it. */
	now = sys_now();
	for (;;) {
		sys_check_timeouts();
		if ((uint32_t)(sys_now() - now) >= 1000U) {
			now += 1000U;
			board_log("lwip: hb t=%ums\n", (unsigned)sys_now());
		}
		__asm__ __volatile__("wfi" ::: "memory");
	}
	return 0;	/* unreachable: start.S parks if main returns */
}

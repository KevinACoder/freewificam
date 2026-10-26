/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   runner.c
 * @brief  Walk the compiled-in case list in one thread and print the verdict.
 *
 * The runner is the image's only application thread at start; the network case
 * creates the threads it needs (the stack's own TCP/IP thread, the RX sweep).
 * A case returning non-zero is a recorded failure, not a stop: the next case
 * still runs, so one boot carries the whole matrix.
 */

#include "runner.h"

#include "cases.h"

#include "os/cmsis2/cmsis2.h"

#if !defined(RTOS_HOST_TIER)
#include "uart.h"
#endif

#ifndef FW_BUILD_TAG
#define FW_BUILD_TAG "local"
#endif

#ifndef RTOS_BACKEND_NAME
#define RTOS_BACKEND_NAME "unknown"
#endif

struct rtos_case {
	const char *name;
	int (*run)(void);
};

static const struct rtos_case cases[] = {
#if defined(RTOS_CASE_TICK)
	{ "tick", case_tick_run },
#endif
#if defined(RTOS_CASE_CMSIS2)
	{ "cmsis2", case_cmsis2_run },
#endif
#if defined(RTOS_CASE_NET)
	{ "net", case_net_run },
#endif
#if defined(RTOS_CASE_EMMC)
	{ "emmc", case_emmc_run },
#endif
#if defined(RTOS_CASE_WLAN)
	{ "wlan", case_wlan_run },
#endif
#if defined(RTOS_CASE_PCIE)
	{ "pcie", case_pcie_run },
#endif
#if defined(RTOS_CASE_NVME)
	{ "nvme", case_nvme_run },
#endif
#if defined(RTOS_CASE_NET_GMAC)
	{ "net-gmac", case_net_gmac_run },
#endif
#if defined(RTOS_CASE_NET_E1000E)
	{ "net-e1000e", case_net_e1000e_run },
#endif
};

static const osThreadAttr_t runner_attr = {
	.name = "runner",
	.priority = osPriorityNormal,
	/* The network case's bring-up and the eMMC round trip both hold frame
	 * space on this thread; the stack's own threads run at their own
	 * priorities above it.  The 8189FTV case runs the vendor probe on this
	 * thread too: the SDIO service, the host glue and the controller each hold
	 * a frame, and the driver's power/firmware/HAL chain sits on top, so that
	 * image gets the reference image's 16 KiB runner stack. */
#if defined(RTOS_CASE_WLAN)
	.stack_size = 16384U,
#elif defined(RTOS_CASE_NVME) || defined(RTOS_CASE_NET_E1000E) || \
	defined(RTOS_CASE_NET_GMAC)
	/* The storage round trip and the wired-NIC bring-up (PHY negotiation
	 * loop, lwIP bring-up) run on this thread. */
	.stack_size = 16384U,
#else
	.stack_size = 8192U,
#endif
};

static void runner(void *arg)
{
	const unsigned total = (unsigned)(sizeof(cases) / sizeof(cases[0]));
	unsigned i;
	unsigned ok = 0u;

	(void)arg;

	CASE_ANCHOR("rtos: runner start cases=%u build=%s\n", total, FW_BUILD_TAG);

	for (i = 0u; i < total; i++) {
		int rc;

		CASE_ANCHOR("rtos: case %s start\n", cases[i].name);
		rc = cases[i].run();
		if (rc == 0) {
			ok++;
			CASE_ANCHOR("rtos: case %s PASS\n", cases[i].name);
		} else {
			CASE_ANCHOR("rtos: case %s FAIL rc=%d\n",
				    cases[i].name, rc);
		}
	}

	CASE_ANCHOR("rtos: result %s cases=%u/%u backend=%s\n",
		    (ok == total) ? "pass" : "fail", ok, total,
		    RTOS_BACKEND_NAME);

#if defined(RTOS_BOARD_AUTO_RESET)
	/* The lab loop's close-out: reset the board once the verdict line is
	 * out, so the next round's boot-loader prompt is already waiting
	 * (RTOS_BOARD_AUTO_RESET builds only). A passing round resets after a
	 * console-drain beat; a failing one dwells first - the failure window
	 * is exactly when the bench host wants to probe the still-running
	 * image (ping, iperf against the listening server, ARP inspection).
	 */
#include "reset.h"
	CASE_ANCHOR("rtos: resetting board\n");
	(void)osDelay((ok == total) ? 200u : 45000u);
	board_system_reset();
#endif

#if defined(RTOS_HOST_TIER)
	/* Leave the kernel so the host process can return; the board images stay
	 * resident and the round ends through the lab's power procedure. */
	cmsis2_host_end_scheduler();
#else
	for (;;) {
		(void)osDelay(1000u);
	}
#endif
}

int rtos_runner_start(void)
{
	return (osThreadNew(runner, NULL, &runner_attr) != NULL) ? 0 : -1;
}
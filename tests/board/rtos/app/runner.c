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

#include "board_tests.h"
#include "cases.h"

#include <string.h>

#include "os/cmsis2/cmsis2.h"

#if !defined(RTOS_HOST_TIER)
#include "uart.h"
#endif

#if defined(RTOS_BOARD_AUTO_RESET)
#include "reset.h"
#endif

#ifndef FW_BUILD_TAG
#define FW_BUILD_TAG "local"
#endif

#ifndef RTOS_BACKEND_NAME
#define RTOS_BACKEND_NAME "unknown"
#endif

#if defined(RTOS_HOST_TIER)
#define CASE_ENTRY(name, fn) { name, fn },
static const struct rtos_case cases[] = {
#else
#define CASE_ENTRY(name, fn) BOARD_TEST(name, fn);
#endif
#if defined(RTOS_CASE_SDIO_MCI)
	CASE_ENTRY("sdio_mci", case_sdio_mci_run)
#endif
#if defined(RTOS_CASE_TICK)
	CASE_ENTRY("tick", case_tick_run)
#endif
#if defined(RTOS_CASE_CMSIS2)
	CASE_ENTRY("cmsis2", case_cmsis2_run)
#endif
#if defined(RTOS_CASE_NET)
	CASE_ENTRY("net", case_net_run)
#endif
#if defined(RTOS_CASE_NET_MDK_NETIO)
	CASE_ENTRY("net_mdk_netio", case_net_mdk_netio_run)
#endif
#if defined(RTOS_CASE_NET_PPS)
	CASE_ENTRY("net_pps", case_net_pps_run)
#endif
#if defined(RTOS_CASE_NET_LOOP)
	CASE_ENTRY("net_loop", case_net_loop_run)
#endif
#if defined(RTOS_CASE_EMMC)
	CASE_ENTRY("emmc", case_emmc_run)
#endif
#if defined(RTOS_CASE_PCIE)
	CASE_ENTRY("pcie", case_pcie_run)
#endif
#if defined(RTOS_CASE_PCIE_BENCH)
	CASE_ENTRY("pcie_bench", case_pcie_bench_run)
#endif
#if defined(RTOS_CASE_PCIE_7260)
	CASE_ENTRY("pcie_7260", case_pcie_7260_run)
#endif
#if defined(RTOS_CASE_IWL7260)
	CASE_ENTRY("iwl7260", case_iwl7260_run)
#endif
#if defined(RTOS_CASE_IWL7260_SCAN)
	CASE_ENTRY("iwl7260_scan", case_iwl7260_scan_run)
#endif
#if defined(RTOS_CASE_IWL7260_JOIN)
	CASE_ENTRY("iwl7260_join", case_iwl7260_join_run)
#endif
#if defined(RTOS_CASE_IWL7260_DATAPLANE)
	CASE_ENTRY("iwl7260_dataplane", case_iwl7260_dataplane_run)
#endif
#if defined(RTOS_CASE_IWL7260_NET)
	CASE_ENTRY("iwl7260_net", case_iwl7260_net_run)
#endif
#if defined(RTOS_CASE_WLAN)
	CASE_ENTRY("wlan", case_wlan_run)
#endif
#if defined(RTOS_CASE_USB_XHCI)
	CASE_ENTRY("usb_xhci", case_usb_xhci_run)
#endif
#if defined(RTOS_CASE_USB_EHCI)
	CASE_ENTRY("usb_ehci", case_usb_ehci_run)
#endif
#if defined(RTOS_CASE_NVME)
	CASE_ENTRY("nvme", case_nvme_run)
#endif
#if defined(RTOS_CASE_PCIE_ECAM)
	CASE_ENTRY("pcie_ecam", case_pcie_ecam_run)
#endif
#if defined(RTOS_CASE_NET_XMAC)
	CASE_ENTRY("net_xmac", case_net_xmac_run)
#endif
#if defined(RTOS_CASE_RTW88)
	CASE_ENTRY("rtw88", case_rtw88_run)
#endif
#if defined(RTOS_CASE_RTW88_SCAN)
	CASE_ENTRY("rtw88_scan", case_rtw88_scan_run)
#endif
#if defined(RTOS_CASE_RTW89)
	CASE_ENTRY("rtw89", case_rtw89_run)
#endif
#if defined(RTOS_CASE_RTW89_SCAN)
	CASE_ENTRY("rtw89_scan", case_rtw89_scan_run)
#endif
#if defined(RTOS_CASE_RTW89_JOIN)
	CASE_ENTRY("rtw89_join", case_rtw89_join_run)
#endif
#if defined(RTOS_CASE_RTW89_DATAPLANE)
	CASE_ENTRY("rtw89_dataplane", case_rtw89_dataplane_run)
#endif
#if defined(RTOS_CASE_RTL8XXXU)
	CASE_ENTRY("rtl8xxxu", case_rtl8xxxu_run)
#endif
#if defined(RTOS_CASE_RTL8XXXU_SCAN)
	CASE_ENTRY("rtl8xxxu_scan", case_rtl8xxxu_scan_run)
#endif
#if defined(RTOS_CASE_RTL8XXXU_JOIN)
	CASE_ENTRY("rtl8xxxu_join", case_rtl8xxxu_join_run)
#endif
#if defined(RTOS_CASE_RTL8XXXU_DATAPLANE)
	CASE_ENTRY("rtl8xxxu_dataplane", case_rtl8xxxu_dataplane_run)
#endif
#if defined(RTOS_CASE_RTL8XXXU_NET)
	CASE_ENTRY("rtl8xxxu_net", case_rtl8xxxu_net_run)
#endif
#if defined(RTOS_CASE_RTL8XXXU_NETIO)
	CASE_ENTRY("rtl8xxxu_netio", case_rtl8xxxu_netio_run)
#endif
#if defined(RTOS_CASE_UVC_DETECT)
	CASE_ENTRY("uvc_detect", case_uvc_detect_run)
#endif
#if defined(RTOS_CASE_UVC_OPEN)
	CASE_ENTRY("uvc_open", case_uvc_open_run)
#endif
#if defined(RTOS_CASE_UVC_ISO_SMOKE)
	CASE_ENTRY("uvc_iso_smoke", case_uvc_iso_smoke_run)
#endif
#if defined(RTOS_CASE_UVC_STREAM)
	CASE_ENTRY("uvc_stream", case_uvc_stream_run)
#endif
#if defined(RTOS_HOST_TIER)
};
#endif

#if defined(RTOS_HOST_TIER)
#define CASE_TABLE cases
#define CASE_COUNT ((unsigned)(sizeof(cases) / sizeof(cases[0])))
#else
extern const struct rtos_case __board_tests_start[];
extern const struct rtos_case __board_tests_end[];
#define CASE_TABLE __board_tests_start
#define CASE_COUNT ((unsigned)(__board_tests_end - __board_tests_start))
#endif

unsigned rtos_case_count(void)
{
	return CASE_COUNT;
}

const char *rtos_case_name(unsigned index)
{
	return (index < rtos_case_count()) ? CASE_TABLE[index].name : NULL;
}

int rtos_case_find(const char *name)
{
	unsigned i;

	if (name == NULL) {
		return -1;
	}
	for (i = 0u; i < rtos_case_count(); i++) {
		if (strcmp(CASE_TABLE[i].name, name) == 0) {
			return (int)i;
		}
	}
	return -1;
}

int rtos_case_run(unsigned index)
{
	return (index < rtos_case_count()) ? CASE_TABLE[index].run() : -1;
}

#ifndef CONFIG_RUNNER_STACK_KB
#define CONFIG_RUNNER_STACK_KB 8
#endif
#ifndef CONFIG_RUNNER_BIG_STACK_KB
#define CONFIG_RUNNER_BIG_STACK_KB 16
#endif

static const osThreadAttr_t runner_attr = {
	.name = "runner",
	.priority = osPriorityNormal,
	/* The network case's bring-up and the eMMC round trip both hold frame
	 * space on this thread; the stack's own threads run at their own
	 * priorities above it.  The 8189FTV case runs the vendor probe on this
	 * thread too: the SDIO service, the host glue and the controller each hold
	 * a frame, and the driver's power/firmware/HAL chain sits on top, so that
	 * image gets the reference image's 16 KiB runner stack. */
#if defined(RTOS_CASE_WLAN) || defined(RTOS_CASE_IWL7260_SCAN) || \
    defined(RTOS_CASE_IWL7260_JOIN) || defined(RTOS_CASE_IWL7260_DATAPLANE) || \
    defined(RTOS_CASE_RTW88) || defined(RTOS_CASE_RTW88_SCAN) || \
    defined(RTOS_CASE_RTW89) || defined(RTOS_CASE_RTW89_SCAN) || \
    defined(RTOS_CASE_RTW89_JOIN) || defined(RTOS_CASE_RTW89_DATAPLANE) || \
    defined(RTOS_CASE_RTL8XXXU) || defined(RTOS_CASE_RTL8XXXU_SCAN) || \
    defined(RTOS_CASE_RTL8XXXU_JOIN) || defined(RTOS_CASE_RTL8XXXU_DATAPLANE) || \
    defined(RTOS_CASE_RTL8XXXU_NETIO) || defined(RTOS_CASE_NET_MDK_NETIO)
	/* The netio server is brought up from this thread (its socket workers get
	 * their own heap-backed stacks); give the bring-up frame room. */
	.stack_size = CONFIG_RUNNER_BIG_STACK_KB * 1024U,
#else
	.stack_size = CONFIG_RUNNER_STACK_KB * 1024U,
#endif
};

static void runner(void *arg)
{
	const unsigned total = rtos_case_count();
	unsigned i;
	unsigned ok = 0u;

	(void)arg;

	CASE_ANCHOR("rtos: runner start cases=%u build=%s\n", total, FW_BUILD_TAG);

	for (i = 0u; i < total; i++) {
		int rc;

		CASE_ANCHOR("rtos: case %s start\n", CASE_TABLE[i].name);
		rc = CASE_TABLE[i].run();
		if (rc == 0) {
			ok++;
			CASE_ANCHOR("rtos: case %s PASS\n", CASE_TABLE[i].name);
		} else {
			CASE_ANCHOR("rtos: case %s FAIL rc=%d\n",
				    CASE_TABLE[i].name, rc);
		}
	}

	CASE_ANCHOR("rtos: result %s cases=%u/%u backend=%s\n",
		    (ok == total) ? "pass" : "fail", ok, total,
		    RTOS_BACKEND_NAME);

#if defined(RTOS_BOARD_AUTO_RESET)
	/* The lab loop's close-out: reset once the verdict line is out, so the
	 * next round's boot-loader prompt is already waiting instead of a board
	 * that parked on its last osDelay. The delay lets the console drain
	 * before the reset. Gated like every other self-recovery here
	 * (RTOS_BOARD_AUTO_RESET builds only - the firefly block sets it). */
	CASE_ANCHOR("rtos: resetting board\n");
	(void)osDelay(200u);
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
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
#if defined(RTOS_CASE_NET_MDK_NETIO)
	{ "net_mdk_netio", case_net_mdk_netio_run },
#endif
#if defined(RTOS_CASE_NET_LOOP)
	{ "net_loop", case_net_loop_run },
#endif
#if defined(RTOS_CASE_EMMC)
	{ "emmc", case_emmc_run },
#endif
#if defined(RTOS_CASE_PCIE)
	{ "pcie", case_pcie_run },
#endif
#if defined(RTOS_CASE_PCIE_BENCH)
	{ "pcie_bench", case_pcie_bench_run },
#endif
#if defined(RTOS_CASE_PCIE_7260)
	{ "pcie_7260", case_pcie_7260_run },
#endif
#if defined(RTOS_CASE_IWL7260)
	{ "iwl7260", case_iwl7260_run },
#endif
#if defined(RTOS_CASE_IWL7260_SCAN)
	{ "iwl7260_scan", case_iwl7260_scan_run },
#endif
#if defined(RTOS_CASE_IWL7260_JOIN)
	{ "iwl7260_join", case_iwl7260_join_run },
#endif
#if defined(RTOS_CASE_IWL7260_DATAPLANE)
	{ "iwl7260_dataplane", case_iwl7260_dataplane_run },
#endif
#if defined(RTOS_CASE_IWL7260_NET)
	{ "iwl7260_net", case_iwl7260_net_run },
#endif
#if defined(RTOS_CASE_WLAN)
	{ "wlan", case_wlan_run },
#endif
#if defined(RTOS_CASE_USB_XHCI)
	{ "usb_xhci", case_usb_xhci_run },
#endif
#if defined(RTOS_CASE_USB_EHCI)
	{ "usb_ehci", case_usb_ehci_run },
#endif
#if defined(RTOS_CASE_NVME)
	{ "nvme", case_nvme_run },
#endif
#if defined(RTOS_CASE_PCIE_ECAM)
	{ "pcie_ecam", case_pcie_ecam_run },
#endif
#if defined(RTOS_CASE_RTW89)
	{ "rtw89", case_rtw89_run },
#endif
#if defined(RTOS_CASE_RTW89_SCAN)
	{ "rtw89_scan", case_rtw89_scan_run },
#endif
#if defined(RTOS_CASE_RTW89_JOIN)
	{ "rtw89_join", case_rtw89_join_run },
#endif
#if defined(RTOS_CASE_RTW89_DATAPLANE)
	{ "rtw89_dataplane", case_rtw89_dataplane_run },
#endif
#if defined(RTOS_CASE_RTL8XXXU)
	{ "rtl8xxxu", case_rtl8xxxu_run },
#endif
#if defined(RTOS_CASE_RTL8XXXU_SCAN)
	{ "rtl8xxxu_scan", case_rtl8xxxu_scan_run },
#endif
#if defined(RTOS_CASE_RTL8XXXU_JOIN)
	{ "rtl8xxxu_join", case_rtl8xxxu_join_run },
#endif
#if defined(RTOS_CASE_RTL8XXXU_DATAPLANE)
	{ "rtl8xxxu_dataplane", case_rtl8xxxu_dataplane_run },
#endif
#if defined(RTOS_CASE_RTL8XXXU_NET)
	{ "rtl8xxxu_net", case_rtl8xxxu_net_run },
#endif
#if defined(RTOS_CASE_UVC_DETECT)
	{ "uvc_detect", case_uvc_detect_run },
#endif
#if defined(RTOS_CASE_UVC_OPEN)
	{ "uvc_open", case_uvc_open_run },
#endif
#if defined(RTOS_CASE_UVC_ISO_SMOKE)
	{ "uvc_iso_smoke", case_uvc_iso_smoke_run },
#endif
#if defined(RTOS_CASE_UVC_STREAM)
	{ "uvc_stream", case_uvc_stream_run },
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
#if defined(RTOS_CASE_WLAN) || defined(RTOS_CASE_IWL7260_SCAN) || \
    defined(RTOS_CASE_IWL7260_JOIN) || defined(RTOS_CASE_IWL7260_DATAPLANE) || \
    defined(RTOS_CASE_RTW89) || defined(RTOS_CASE_RTW89_SCAN) || \
    defined(RTOS_CASE_RTW89_JOIN) || defined(RTOS_CASE_RTW89_DATAPLANE) || \
    defined(RTOS_CASE_RTL8XXXU) || defined(RTOS_CASE_RTL8XXXU_SCAN) || \
    defined(RTOS_CASE_RTL8XXXU_JOIN) || defined(RTOS_CASE_RTL8XXXU_DATAPLANE) || \
    defined(RTOS_CASE_NET_MDK_NETIO)
	/* The netio server is brought up from this thread (its socket workers get
	 * their own heap-backed stacks); give the bring-up frame room. */
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
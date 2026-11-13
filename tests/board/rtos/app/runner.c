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
#if defined(RTOS_CASE_TICK)
	CASE_ENTRY("tick", case_tick_run)
#endif
#if defined(RTOS_CASE_CMSIS2)
	CASE_ENTRY("cmsis2", case_cmsis2_run)
#endif
#if defined(RTOS_CASE_NET)
	CASE_ENTRY("net", case_net_run)
#endif
#if defined(RTOS_CASE_EMMC)
	CASE_ENTRY("emmc", case_emmc_run)
#endif
#if defined(RTOS_CASE_PCIE)
	CASE_ENTRY("pcie", case_pcie_run)
#endif
#if defined(RTOS_CASE_USB_XHCI)
	CASE_ENTRY("usb_xhci", case_usb_xhci_run)
#endif
#if defined(RTOS_CASE_USB_EHCI)
	CASE_ENTRY("usb_ehci", case_usb_ehci_run)
#endif
#if defined(RTOS_CASE_KPI_LIVE)
	CASE_ENTRY("kpi_live", case_kpi_live_run)
#endif
#if defined(RTOS_CASE_KPI_MOCK)
	CASE_ENTRY("kpi_mock", case_kpi_mock_run)
#endif
#if defined(RTOS_CASE_MAC80211_C2)
	CASE_ENTRY("mac80211_c2", case_mac80211_c2_run)
#endif
#if defined(RTOS_CASE_IWL7260_C3)
	CASE_ENTRY("iwl7260_c3", case_iwl7260_c3_run)
#endif
#if defined(RTOS_CASE_IWL7260_C4)
	CASE_ENTRY("iwl7260_c4", case_iwl7260_c4_run)
#endif
#if defined(RTOS_CASE_IWL7260_C5)
	CASE_ENTRY("iwl7260_c5", case_iwl7260_c5_run)
#endif
#if defined(RTOS_CASE_IWL7260_C6)
	CASE_ENTRY("iwl7260_c6", case_iwl7260_c6_run)
#endif
#if defined(RTOS_CASE_IWL7260_AP)
	CASE_ENTRY("iwl7260_ap", case_iwl7260_ap_run)
#endif
#if defined(RTOS_CASE_IWL7260_STA_WPA2)
	CASE_ENTRY("iwl7260_sta_wpa2", case_iwl7260_sta_wpa2_run)
#endif
#if defined(RTOS_CASE_IWL7260_AP5G)
	CASE_ENTRY("iwl7260_ap5g", case_iwl7260_ap5g_run)
#endif
#if defined(RTOS_CASE_IWL7260_STA5G)
	CASE_ENTRY("iwl7260_sta5g", case_iwl7260_sta5g_run)
#endif
#if defined(RTOS_CASE_IWL7260_STAB)
	CASE_ENTRY("iwl7260_stab", case_iwl7260_stab_run)
#endif
#if defined(RTOS_CASE_IWL7260_MON)
	CASE_ENTRY("iwl7260_mon", case_iwl7260_mon_run)
#endif
#if defined(RTOS_CASE_RTL8189FS_SDIO)
	CASE_ENTRY("rtl8189fs_sdio", case_rtl8189fs_sdio_run)
#endif
#if defined(RTOS_CASE_RTL8189FS_STA)
	CASE_ENTRY("rtl8189fs_sta", case_rtl8189fs_sta_run)
#endif
#if defined(RTOS_CASE_RTL8189FS_STA_WPA2)
	CASE_ENTRY("rtl8189fs_sta_wpa2", case_rtl8189fs_sta_wpa2_run)
#endif
#if defined(RTOS_CASE_RTL8189FS_C6T)
	CASE_ENTRY("rtl8189fs_c6t", case_rtl8189fs_c6t_run)
#endif
#if defined(RTOS_CASE_RTL8189FS_AP)
	CASE_ENTRY("rtl8189fs_ap", case_rtl8189fs_ap_run)
#endif
#if defined(RTOS_CASE_RTW89_C3)
	CASE_ENTRY("rtw89_c3", case_rtw89_c3_run)
#endif
#if defined(RTOS_CASE_RTW89_C4)
	CASE_ENTRY("rtw89_c4", case_rtw89_c4_run)
#endif
#if defined(RTOS_CASE_RTW89_C5)
	CASE_ENTRY("rtw89_c5", case_rtw89_c5_run)
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

/* The table's byte order is the linker's, not a run order: the same objects
 * produce a different BOARD_TESTS sequence when the link line changes, so a
 * ladder whose second case depends on its first (bring-up -> scan -> join ->
 * data) cannot rely on it.  The profile's CONFIG_CASES declaration is the run
 * order, and it is the one the three-source case gate reconciles, so the table
 * is walked through a permutation built from it.  A case the declaration does
 * not name still runs, after the named ones: silently dropping a case would
 * hide a drift that the gate reports only at build time. */
#define CASE_ORDER_MAX 32u

#if !defined(RTOS_HOST_TIER)
static unsigned char case_order[CASE_ORDER_MAX];
static unsigned case_order_count;
static int case_order_ready;

static unsigned raw_case_count(void)
{
	return CASE_COUNT;
}

static const char *raw_case_name(unsigned index)
{
	return (index < raw_case_count()) ? CASE_TABLE[index].name : NULL;
}

static int raw_case_find(const char *name)
{
	unsigned i;

	for (i = 0u; i < raw_case_count(); i++) {
		if (strcmp(CASE_TABLE[i].name, name) == 0) {
			return (int)i;
		}
	}
	return -1;
}

static void case_order_build(void)
{
	unsigned char seen[CASE_ORDER_MAX];
	char declaration[sizeof(CONFIG_CASES)];
	char *token;
	char *save = NULL;
	unsigned total = raw_case_count();
	unsigned n = 0u;
	unsigned i;

	if (case_order_ready) {
		return;
	}
	memset(seen, 0, sizeof(seen));
	memcpy(declaration, CONFIG_CASES, sizeof(declaration));
	for (token = strtok_r(declaration, " ", &save); token != NULL;
	     token = strtok_r(NULL, " ", &save)) {
		int index = raw_case_find(token);

		if (index < 0 || (unsigned)index >= CASE_ORDER_MAX ||
		    seen[index] != 0u || n >= CASE_ORDER_MAX) {
			continue;
		}
		seen[index] = 1u;
		case_order[n++] = (unsigned char)index;
	}
	for (i = 0u; i < total && n < CASE_ORDER_MAX; i++) {
		if (seen[i] == 0u) {
			case_order[n++] = (unsigned char)i;
		}
	}
	case_order_count = n;
	case_order_ready = 1;
}
#endif /* !RTOS_HOST_TIER */

unsigned rtos_case_count(void)
{
#if defined(RTOS_HOST_TIER)
	return CASE_COUNT;
#else
	case_order_build();
	return case_order_count;
#endif
}

const char *rtos_case_name(unsigned index)
{
#if defined(RTOS_HOST_TIER)
	return (index < rtos_case_count()) ? CASE_TABLE[index].name : NULL;
#else
	case_order_build();
	return (index < case_order_count) ? raw_case_name(case_order[index]) : NULL;
#endif
}

int rtos_case_find(const char *name)
{
	unsigned i;

	if (name == NULL) {
		return -1;
	}
	for (i = 0u; i < rtos_case_count(); i++) {
		const char *candidate = rtos_case_name(i);

		if (candidate != NULL && strcmp(candidate, name) == 0) {
			return (int)i;
		}
	}
	return -1;
}

int rtos_case_run(unsigned index)
{
#if defined(RTOS_HOST_TIER)
	return (index < rtos_case_count()) ? CASE_TABLE[index].run() : -1;
#else
	case_order_build();
	return (index < case_order_count) ? CASE_TABLE[case_order[index]].run() : -1;
#endif
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
	/* The eMMC round trip and the network bring-up hold frame space on this
	 * thread; the stack's own threads run at their own priorities above it.
	 * The knob is the profile's decision, not a per-case branch. */
	.stack_size = CONFIG_RUNNER_STACK_KB * 1024U,
};

static void runner(void *arg)
{
	const unsigned total = rtos_case_count();
	unsigned i;
	unsigned ok = 0u;

	(void)arg;

	CASE_ANCHOR("rtos: runner start cases=%u build=%s\n", total, FW_BUILD_TAG);

	for (i = 0u; i < total; i++) {
		const char *name = rtos_case_name(i);
		int rc;

		CASE_ANCHOR("rtos: case %s start\n", name);
		rc = rtos_case_run(i);
		if (rc == 0) {
			ok++;
			CASE_ANCHOR("rtos: case %s PASS\n", name);
		} else {
			CASE_ANCHOR("rtos: case %s FAIL rc=%d\n", name, rc);
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
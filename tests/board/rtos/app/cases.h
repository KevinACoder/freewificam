/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   cases.h
 * @brief  The case interface shared by the board image and the host tier.
 *
 * One case = one group of checks that prints its own anchor lines and returns
 * 0 on pass.  A case never stops the image: the runner records the verdict and
 * moves to the next group, so one failing group still leaves the others'
 * evidence in the same boot.
 *
 * The anchor text is the contract the board runner judges (tools/rtos/judge.py
 * and the expect file): it is fixed, greppable and prefixed.  Changing a line
 * here means changing the judge in the same commit.
 *
 * This line carries the four hardware acceptance groups plus the platform
 * smoke: tick, cmsis2, net, pcie, emmc, usb_xhci.  The wireless groups the
 * previous tree carried are not imported; each lands here again with its case
 * file when its stack does.
 */

#ifndef RTOS_APP_CASES_H
#define RTOS_APP_CASES_H

#include <stdint.h>

#include "clock.h"

#if defined(RTOS_HOST_TIER)
#include <stdio.h>
#define CASE_INFO(...)   printf(__VA_ARGS__)
#define CASE_ANCHOR(...) printf(__VA_ARGS__)
#else
#include "log.h"
#define CASE_INFO(...)   LOG_INFO(__VA_ARGS__)
#define CASE_ANCHOR(...) LOG_ANCHOR(__VA_ARGS__)
#endif

/* Each case is compiled only when its switch is on, so a short arm builds a
 * short image (`make CASES=tick,cmsis2`). */
#if defined(RTOS_CASE_TICK)
int case_tick_run(void);
#endif
#if defined(RTOS_CASE_CMSIS2)
int case_cmsis2_run(void);
#endif
#if defined(RTOS_CASE_NET)
int case_net_run(void);
#endif
#if defined(RTOS_CASE_EMMC)
int case_emmc_run(void);
#endif
#if defined(RTOS_CASE_PCIE)
int case_pcie_run(void);
#endif
#if defined(RTOS_CASE_USB_XHCI)
int case_usb_xhci_run(void);
#endif
#if defined(RTOS_CASE_USB_EHCI)
int case_usb_ehci_run(void);
#endif

#if defined(RTOS_CASE_USB_XHCI) || defined(RTOS_CASE_USB_EHCI)
int usb_msc_case(unsigned int busid, const char *hcd);
#endif

#if defined(RTOS_CASE_KPI_LIVE)
int case_kpi_live_run(void);
#endif
#if defined(RTOS_CASE_KPI_MOCK)
int case_kpi_mock_run(void);
#endif

/* The wireless stack's own gates: C2 registers a wiphy with a synthetic
 * driver; C3 attaches the AC 7260 and lets iwlwifi call back into the stack. */
#if defined(RTOS_CASE_MAC80211_C2)
int case_mac80211_c2_run(void);
#endif
#if defined(RTOS_CASE_IWL7260_C3)
int case_iwl7260_c3_run(void);
#endif

/* The shared one-shot lwIP stack bring-up (a boot may run more than one NIC
 * case; tcpip_init is once-per-boot by contract), and the boot's one lwiperf
 * TCP server (only one listener can own the port).  The `net` case brings its
 * own stack up; the helper is kept for the wired arms that share a boot. */
#if defined(RTOS_CASE_NET)
int case_net_stack_up(void);
void case_net_lwiperf_server_start(void);
#endif

#endif /* RTOS_APP_CASES_H */

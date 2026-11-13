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
#if defined(RTOS_CASE_IWL7260_C4)
int case_iwl7260_c4_run(void);
#endif
#if defined(RTOS_CASE_IWL7260_C5)
int case_iwl7260_c5_run(void);
#endif
#if defined(RTOS_CASE_IWL7260_C6)
int case_iwl7260_c6_run(void);
void lkpi_c6_peer_ip_set(const char *ip);
#endif
/* The AP round: the default STA interface becomes the AP, the Pi joins, and
 * the AP phase table runs (docs/HANDOFF-AP-MODE.md). */
#if defined(RTOS_CASE_IWL7260_AP)
int case_iwl7260_ap_run(void);
#endif
#if defined(RTOS_CASE_IWL7260_STA_WPA2)
int case_iwl7260_sta_wpa2_run(void);
#endif
#if defined(RTOS_CASE_IWL7260_STAB)
int case_iwl7260_stab_run(void);
#endif
/* The 5 GHz round (docs/HANDOFF-5GHZ.md): the same two roles, with the band as
 * a profile input and the association's frequency as a verdict input. */
#if defined(RTOS_CASE_IWL7260_AP5G)
int case_iwl7260_ap5g_run(void);
#endif
#if defined(RTOS_CASE_IWL7260_STA5G)
int case_iwl7260_sta5g_run(void);
#endif
/* The monitor round (docs/HANDOFF-AC7260-MONITOR.md): the same card as an
 * RX-only monitor, with the port's sink as the frame's consumer. */
#if defined(RTOS_CASE_IWL7260_MON)
int case_iwl7260_mon_run(void);
#endif

/* The 8189FTV vendor stack (RTL8188F, SDIO): C3 attach, the station's C4-C6
 * open join, and the C6T throughput phases. */
#if defined(RTOS_CASE_RTL8189FS_SDIO)
int case_rtl8189fs_sdio_run(void);
#endif
#if defined(RTOS_CASE_RTL8189FS_STA)
int case_rtl8189fs_sta_run(void);
#endif
#if defined(RTOS_CASE_RTL8189FS_STA_WPA2)
int case_rtl8189fs_sta_wpa2_run(void);
#endif
#if defined(RTOS_CASE_RTL8189FS_AP)
/* The AP role (docs/HANDOFF-8189FTV-RTOS-AP.md): the vendor stack's own AP
 * MLME beacons and answers auth/assoc, hostapd supplies the beacon's IEs, and
 * the station table is the vendor's (no netlink in this image). */
int case_rtl8189fs_ap_run(void);
#endif
#if defined(RTOS_CASE_RTL8189FS_C6T)
int case_rtl8189fs_c6t_run(void);
#endif

/* The RTL8851BU (rtw89, USB): C3 attach over the linuxkpi USB engine and
 * CherryUSB, C4 scan, C5/C6 open join + ICMP.  The cases are mac80211 drivers'
 * gates (the stack owns the MLME), not the net80211 bridge's. */
#if defined(RTOS_CASE_RTW89_C3)
int case_rtw89_c3_run(void);
#endif
#if defined(RTOS_CASE_RTW89_C4)
int case_rtw89_c4_run(void);
#endif
#if defined(RTOS_CASE_RTW89_C5)
int case_rtw89_c5_run(void);
#endif
#if defined(RTOS_CASE_RTW89_C6)
int case_rtw89_c6_run(void);
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

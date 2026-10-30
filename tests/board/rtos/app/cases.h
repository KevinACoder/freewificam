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
#if defined(RTOS_CASE_PCIE_BENCH)
int case_pcie_bench_run(void);
#endif

#if defined(RTOS_CASE_PCIE_7260)
int case_pcie_7260_run(void);
#endif

#if defined(RTOS_CASE_IWL7260)
int case_iwl7260_run(void);
#endif

#if defined(RTOS_CASE_IWL7260_SCAN)
int case_iwl7260_scan_run(void);
#endif

#if defined(RTOS_CASE_IWL7260_JOIN)
int case_iwl7260_join_run(void);
#endif

#if defined(RTOS_CASE_IWL7260_DATAPLANE)
int case_iwl7260_dataplane_run(void);
#endif

#if defined(RTOS_CASE_IWL7260_NET)
int case_iwl7260_net_run(void);
#endif

#if defined(RTOS_CASE_WLAN)
int case_wlan_run(void);
void case_wlan_prepare(void);
#endif
#if defined(RTOS_CASE_NVME)
int case_nvme_run(void);
#endif
#if defined(RTOS_CASE_PCIE_ECAM)
int case_pcie_ecam_run(void);
#endif
#if defined(RTOS_CASE_NET_E1000E)
int case_net_e1000e_run(void);
#endif
#if defined(RTOS_CASE_NET_GMAC)
int case_net_gmac_run(void);
#endif
#if defined(RTOS_CASE_NET_MDK_NETIO)
int case_net_mdk_netio_run(void);
#endif
#if defined(RTOS_CASE_NET_LOOP)
int case_net_loop_run(void);
#endif
#if defined(RTOS_CASE_NET_E1000E) || defined(RTOS_CASE_NET_GMAC) || \
	defined(RTOS_CASE_NET_MDK_NETIO) || defined(RTOS_CASE_NET_LOOP)
/* The shared one-shot lwIP stack bring-up (a boot may run both NIC cases;
 * tcpip_init is once-per-boot by contract), and the boot's one lwiperf TCP
 * server (only one listener can own the port). */
int case_net_stack_up(void);
void case_net_lwiperf_server_start(void);
#endif

#if defined(RTOS_CASE_USB_XHCI) || defined(RTOS_CASE_USB_EHCI)
int usb_msc_case(unsigned int busid, const char *hcd);
#endif

#if defined(RTOS_CASE_USB_XHCI)
int case_usb_xhci_run(void);
#endif

#if defined(RTOS_CASE_USB_EHCI)
int case_usb_ehci_run(void);
#endif

#if defined(RTOS_CASE_RTW89)
int case_rtw89_run(void);
#endif

#if defined(RTOS_CASE_RTW89_SCAN)
int case_rtw89_scan_run(void);
#endif

#if defined(RTOS_CASE_RTW89_JOIN)
int case_rtw89_join_run(void);
#endif

#if defined(RTOS_CASE_RTW89_DATAPLANE)
int case_rtw89_dataplane_run(void);
#endif

#if defined(RTOS_CASE_RTL8XXXU)
int case_rtl8xxxu_run(void);
#endif

#if defined(RTOS_CASE_RTL8XXXU_SCAN)
int case_rtl8xxxu_scan_run(void);
#endif

#if defined(RTOS_CASE_RTL8XXXU_JOIN)
int case_rtl8xxxu_join_run(void);
#endif

#if defined(RTOS_CASE_RTL8XXXU_DATAPLANE)
int case_rtl8xxxu_dataplane_run(void);
#endif

#if defined(RTOS_CASE_RTL8XXXU_NET)
int case_rtl8xxxu_net_run(void);
#endif

/* The camera image's four cases, in run order: the controller and the camera,
 * the negotiated open, the raw isochronous transport, then the stream engine.
 * They share the application half's state (`ports/cherryusb/class/uvc_app.h`). */
#if defined(RTOS_CASE_UVC_DETECT)
int case_uvc_detect_run(void);
#endif

#if defined(RTOS_CASE_UVC_OPEN)
int case_uvc_open_run(void);
#endif

#if defined(RTOS_CASE_UVC_ISO_SMOKE)
int case_uvc_iso_smoke_run(void);
#endif

#if defined(RTOS_CASE_UVC_STREAM)
int case_uvc_stream_run(void);
#endif

#endif /* RTOS_APP_CASES_H */
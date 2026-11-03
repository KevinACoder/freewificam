/*
 * @file   aic8800_wifi_config.h
 * @brief   This repository's configuration of the imported AIC8800 demo
 *          stack (the header the vendored sources include as their config).
 *
 * First-party file, not part of the vendored import: the upstream template
 * (aic8800_wifi_config_template.h) carries the reference board's defaults
 * and is included first so every macro this file does not name keeps its
 * upstream default; the defines below are this image's facts and win over
 * the template's because the template guards every default with #ifndef.
 *
 * The bench facts are profile facts, the rtw88 line's rule: the SSID/PSK
 * come from the generated configuration header (CONFIG_AIC8800_AP_SSID /
 * CONFIG_AIC8800_AP_PSK, named in the defconfig), and an empty SSID means a
 * bring-up-only image that stops after the scan table.
 */

#ifndef FREEWIFICAM_AIC8800_WIFI_CONFIG_H
#define FREEWIFICAM_AIC8800_WIFI_CONFIG_H

/* Upstream defaults first (bands, scan table geometry, PBKDF2 iterations,
 * RSN IE bound, BSSID/BAND selection, NOR package mapping fallbacks). */
#include "aic8800_wifi_config_template.h"

/* No memory-mapped AICFWPKG on this board: the five images are linked in
 * (tools/rtos/aic8800-fw-blobs.py generates the unit that registers them). */
#undef AIC8800_FW_PACKAGE_BASE
#undef AIC8800_FW_PACKAGE_CAPACITY
#define AIC8800_FW_PACKAGE_BASE 0UL
#define AIC8800_FW_PACKAGE_CAPACITY 0UL

/* The TX records live in ordinary cacheable .bss: the stack's per-buffer
 * DMA maintenance covers every transfer buffer the controller reads, the
 * same discipline the RK3568 images run with. */
#undef AIC8800_FMAC_TX_QUEUE_SECTION
#define AIC8800_FMAC_TX_QUEUE_SECTION ".bss"

/* The stack owns its tcpip thread (this image brings lwIP through the port
 * face only - no net_stack.c case runs tcpip_init first). */
#undef AIC8800_LWIP_OWN_TCPIP
#define AIC8800_LWIP_OWN_TCPIP 1

/* Connection policy: the case drives scan/join itself after READY (it
 * prints the scan table as the B3 anchor), so the stack's own
 * auto-connect is off. */
#undef AIC8800_WIFI_AUTO_CONNECT
#define AIC8800_WIFI_AUTO_CONNECT 0

/* The demo's self-test extras stay off; the bring-up case measures. */
#undef AIC8800_WIFI_DEMO_PING
#define AIC8800_WIFI_DEMO_PING 0
#undef AIC8800_WIFI_DEMO_IPERF
#define AIC8800_WIFI_DEMO_IPERF 0

/* The AP to join: WPA2-PSK only (the vendor stack implements no other
 * mode). Empty SSID = bring-up-only image (B0-B3). */
#ifndef CONFIG_AIC8800_AP_SSID
#error "CONFIG_AIC8800_AP_SSID must be defined by the image configuration"
#endif
#undef AIC8800_WIFI_SSID
#define AIC8800_WIFI_SSID CONFIG_AIC8800_AP_SSID

#ifndef CONFIG_AIC8800_AP_PSK
#error "CONFIG_AIC8800_AP_PSK must be defined by the image configuration"
#endif
#undef AIC8800_WIFI_PASSWORD
#define AIC8800_WIFI_PASSWORD CONFIG_AIC8800_AP_PSK

#endif /* FREEWIFICAM_AIC8800_WIFI_CONFIG_H */

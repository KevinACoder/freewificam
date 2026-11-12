/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    hostap_config.h
 * @brief   Build-time configuration for the imported NXP wpa_supplicant-rtos
 *          tree in this port (forced include on every hostap translation unit).
 *
 * The NXP tree derives its internal hostapd feature macros from a handful of
 * CONFIG_* symbols (src/utils/includes.h), so this header only states the
 * platform choices; it does not restate hostapd's own macros:
 *
 *   - CONFIG_OS_LKPI steers includes.h / build_config.h away from the NXP SDK
 *     headers (wm_net.h, fsl_debug_console.h) and mbedtls, exactly the way the
 *     Embox fork's CONFIG_OS_EMBOX does.  The two hunks that add the branch
 *     live in third-party/wpa_supplicant-rtos/patches/.
 *   - CONFIG_HOSTAPD selects the hostapd daemon path in the NXP entry
 *     (freertos/src/supp_main.c: hostapd_main_task) and, through includes.h,
 *     the AP MLME feature set (CONFIG_AP, NEED_AP_MLME, HOSTAPD, ...).
 *   - CONFIG_WPA_SUPP_CRYPTO 0 selects the internal crypto implementation
 *     (BSD-3); mbedtls (Apache-2.0) must never enter this image.
 *   - CONFIG_DRIVER_LKPI is this port's driver row in src/drivers/drivers.c
 *     (patched), the same shape the Embox fork added for its net80211 driver.
 *
 * The AP round is open + HT20 + WMM: WPA/WPS/P2P/DPP/ACS/DFS/11ac/11ax are off,
 * which also matches the transferable part of NXP's own CONFIG_HOSTAPD source
 * list (it carries no acs.c / wnm_ap.c / eth_p_oui.c).
 */
#ifndef LKPI_HOSTAP_CONFIG_H
#define LKPI_HOSTAP_CONFIG_H

/* the OS face: no sockets, no NXP SDK headers, no mbedtls */
#define CONFIG_OS_LKPI 1

/* the kernel is FreeRTOS: os.h then leaves os_malloc()/os_free() to the port
 * (src/utils/os_freertos.c defines them) instead of aliasing libc */
#define CONFIG_FREERTOS 1

#define CONFIG_WPA_SUPP 1
#define CONFIG_HOSTAPD 1
#define CONFIG_IPV6 0

/* internal crypto only: the open AP needs none, WPA2-CCMP needs no TLS engine */
#define CONFIG_WPA_SUPP_CRYPTO 0
#define CONFIG_CRYPTO_INTERNAL 1

/* no file backend, no config blobs, no entropy pool: the config is built in C */
#define CONFIG_NO_CONFIG_WRITE 1
#define CONFIG_NO_CONFIG_BLOBS 1
#define CONFIG_NO_RANDOM_POOL 1
#define CONFIG_BACKEND_NONE 1

/* no hostapd control interface (no UNIX socket): ctrl_iface.h then provides
 * the inline no-ops supp_main.c installs in hapd_interfaces */
#define CONFIG_NO_CTRL_IFACE 1

/* our driver in src/drivers/drivers.c (patched) */
#define CONFIG_DRIVER_LKPI 1

/* EAP server stubs only (ieee802_1x_init runs even for an open AP) */
#define EAP_SERVER 1
#define EAP_SERVER_IDENTITY 1

/* the entry's task/knobs (freertos/src/supp_main.c) */
#define CONFIG_SUPP_MAIN_THREAD_STACK_SIZE (32 * 1024)
#define CONFIG_WPA_SUPP_DEBUG_LEVEL 3 /* MSG_INFO */

/*
 * supp_main.c's event post asks whether it runs in an ISR with the Cortex-M
 * intrinsic __get_IPSR().  This image is aarch64 and posts events from task
 * context only (the RX seam copies frames and a task forwards them), so the
 * honest answer is "not an ISR".
 */
#ifndef __get_IPSR
#define __get_IPSR() 0u
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof((a)) / sizeof((a)[0]))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#endif /* LKPI_HOSTAP_CONFIG_H */

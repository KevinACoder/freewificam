/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    hostap_config_sta.h
 * @brief   Build-time configuration for the imported wpa_supplicant tree when
 *          this image is the STA (G2), not the AP.
 *
 * The AP profile's hostap_config.h sets CONFIG_HOSTAPD 1; this header sets it
 * to 0 so the NXP entry takes the supplicant branch in supp_main.c.  The rest
 * is the same platform choice: no sockets, no NXP SDK headers, no mbedtls, and
 * the internal crypto implementation only.
 */
#ifndef LKPI_HOSTAP_CONFIG_STA_H
#define LKPI_HOSTAP_CONFIG_STA_H

#define CONFIG_OS_LKPI 1
#define CONFIG_FREERTOS 1

#define CONFIG_WPA_SUPP 1
#define CONFIG_HOSTAPD 0
#define CONFIG_IPV6 0

#define CONFIG_WPA_SUPP_CRYPTO 0
#define CONFIG_CRYPTO_INTERNAL 1

#define CONFIG_NO_CONFIG_WRITE 1
#define CONFIG_NO_CONFIG_BLOBS 1
#define CONFIG_NO_RANDOM_POOL 1
#define CONFIG_BACKEND_NONE 1

#define CONFIG_NO_CTRL_IFACE 1
#define CONFIG_DRIVER_LKPI 1

/* the supplicant task (freertos/src/supp_main.c) */
#define CONFIG_SUPP_MAIN_THREAD_STACK_SIZE (32 * 1024)
#define CONFIG_WPA_SUPP_DEBUG_LEVEL 3 /* MSG_INFO */

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

#endif /* LKPI_HOSTAP_CONFIG_STA_H */

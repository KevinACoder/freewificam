/*
 * @file   wpa_port_config.h
 * @brief  Build-time configuration for the wpa_supplicant port.
 *
 * Forced-include on every translation unit of the wpa world (-include,
 * vendored sources and the port layer alike). It must be usable before
 * includes.h: CONFIG_OS_EMBOX steers the NXP/Zephyr conditionals there
 * onto the plain-libc paths, and the hostapd/EAP/WPS/P2P features this
 * port does not build stay off.
 *
 * The image has no generated config.h; the build-shape keys arrive as
 * -D from the Makefile and #error here when missing (the tx_user.h
 * pattern, SPEC S3 §3.5.3).
 *
 * Ported from the prior project's wpa_port_config.h ([S7a] §3.3.1,
 * board-proven PSK-only shape).
 */

#ifndef WPA_PORT_CONFIG_H_
#define WPA_PORT_CONFIG_H_

/* steers includes.h / build_config.h away from the NXP SDK headers */
#define CONFIG_OS_EMBOX 1

#define CONFIG_WPA_SUPP 1
#define CONFIG_HOSTAPD 0
#define CONFIG_IPV6 0

/* internal crypto only: PSK/WPA2-CCMP needs no TLS engine */
#define CONFIG_WPA_SUPP_CRYPTO 0

/* MSG_DEBUG (2) is the bring-up level: the select/associate decision
 * path is invisible at INFO and that silence cost a SCANNING hang in
 * the prior project. Keys from the Makefile. */
#ifndef CONFIG_WPA_SUPP_DEBUG_LEVEL
#error "CONFIG_WPA_SUPP_DEBUG_LEVEL must come from the build (Makefile key)"
#endif

/* the supplicant's call chains (scan/assoc/crypto) are the deepest in
 * the image; the prior project measured 64 KiB */
#ifndef CONFIG_WPA_THREAD_STACK
#error "CONFIG_WPA_THREAD_STACK must come from the build (Makefile key)"
#endif

/* PSK-only: no file backend, no blobs, no ctrl-iface, no random pool */
#define CONFIG_NO_CONFIG_WRITE 1
#define CONFIG_NO_CONFIG_BLOBS 1
#define CONFIG_NO_RANDOM_POOL 1
#define CONFIG_BACKEND_NONE 1

/* The driver slot in src/drivers/drivers.c. The vendor wrapper keeps
 * the vanilla "embox" export name so the vendored tree stays untouched
 * ([S7a] §3.2.1); the smoke image rides the vendored none stub. */
#if defined(CONFIG_WPA_BACKEND_VENDOR)
#define CONFIG_DRIVER_EMBOX 1
#elif defined(CONFIG_WPA_BACKEND_NONE)
#define CONFIG_DRIVER_NONE 1
#else
#error "CONFIG_WPA_BACKEND_VENDOR or CONFIG_WPA_BACKEND_NONE must be set"
#endif

/* the driver name add_iface passes (embox = vendor wrapper slot) */
#ifndef WPA_PORT_DRIVER_NAME
#error "WPA_PORT_DRIVER_NAME must come from the build (Makefile key)"
#endif

/* the SDK headers used to provide this on the NXP builds */
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof((a)) / sizeof((a)[0]))
#endif

/* openssl-style entropy unavailable; nonces come from os_get_random */
#define CONFIG_CRYPTO_INTERNAL 1
#define CONFIG_INTERNAL_AES 1
#define CONFIG_INTERNAL_SHA1 1
#define CONFIG_INTERNAL_MD5 1
#define CONFIG_INTERNAL_SHA256 1
#define CONFIG_INTERNAL_RC4 1

#endif /* WPA_PORT_CONFIG_H_ */

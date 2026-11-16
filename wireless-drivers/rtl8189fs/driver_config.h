/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    driver_config.h
 * @brief   The configuration face of the vendored rtl8189fs (RTL8188F, SDIO).
 *
 * @design The vendor tree is written against a Makefile that states its answers
 *         as `-D` flags.  This project has one configuration channel
 *         (docs/BUILD.md §1) and no `-D` on any compile line, so the answers are
 *         restated here once, in the same shape build_defs.h uses: a feature is
 *         present only on the images whose config names this driver.  The header
 *         is force-included in the driver's flag world
 *         (tests/board/rtos/build/wl80211.mk) and in no other world.
 *
 *         The baseline is the GT build's own flag list — the in-tree Kbuild at
 *         linux-upstream/drivers/net/wireless/realtek/rtl8189fs/Kbuild — which
 *         is what the 2026-11-12/13 GT rounds compiled and what the reference
 *         numbers in docs/GT-8189FTV-REFERENCE.md were measured with.  Three
 *         answers differ, each for a reason stated at its line:
 *
 *           - CONFIG_PROC_DEBUG is off: this project has no filesystem, and the
 *             proc surface is deliberately a declaration-only face
 *             (components/liblinux80211/include/linux/proc_fs.h).  The vendor
 *             unit (os_dep/linux/rtw_proc.c) is one #ifdef around its body and
 *             compiles empty, so the link table keeps all 131 units.
 *           - CONFIG_WIFI_MONITOR is off: this round is the station role.
 *           - CONFIG_BR_EXT is off: the same choice the GT Kbuild makes (the
 *             bridge extension calls br_fdb_find_port(), which only the bridge
 *             module exports).
 *
 *         Everything else is byte-for-byte the GT answer, including the two
 *         file paths (both absent on this board, and opened only when present)
 *         and the runtime log level's default.  The station case lowers
 *         rtw_drv_log_level before module init, exactly as the GT rounds did on
 *         the 115200 console.
 * @date    13.11.2026
 */

#ifndef _RTL8189FS_DRIVER_CONFIG_H_
#define _RTL8189FS_DRIVER_CONFIG_H_

/* --- the chip -------------------------------------------------------------
 *
 * CONFIG_RTL8188F must be stated here, not left to the vendor tree's own
 * include/autoconf.h: the driver's headers gate themselves on it before the
 * first include (hal8188f_fw.c:16 tests it above #include "drv_types.h"), so a
 * face that waits for autoconf compiles the whole file away.  The GT Kbuild
 * passes it as a -D for the same reason.  It is defined without a value, which
 * is how autoconf.h states it too, so the later identical #define is benign.
 *
 * CONFIG_SDIO_HCI is autoconf's (the interface half of the chip
 * configuration) and is not repeated here.
 */
#define CONFIG_RTL8188F

/* --- the control plane: cfg80211, as on GT -------------------------------- */
#define CONFIG_IOCTL_CFG80211			1
#define RTW_USE_CFG80211_STA_EVENT		1

/* The vendor's multi-interface core; GT compiled it in, and the station path
 * shares the adapter/interface records it sets up. */
#if CONFIG_RTL8189FS_CONCURRENT
#define CONFIG_CONCURRENT_MODE			1
#endif

/* --- feature set, as on GT ------------------------------------------------ */
#if CONFIG_RTL8189FS_MP
#define CONFIG_MP_INCLUDED			1
#endif
#if CONFIG_RTL8189FS_POWER_SAVING
#define CONFIG_POWER_SAVING			1
#endif
#if CONFIG_RTL8189FS_DEBUG
#define CONFIG_RTW_DEBUG			1
#endif
#if CONFIG_RTL8189FS_EFUSE_FILE
#define CONFIG_EFUSE_CONFIG_FILE		1
#endif
#if CONFIG_RTL8189FS_LOAD_PHY_FILE
#define CONFIG_LOAD_PHY_PARA_FROM_FILE		1
#endif
#if CONFIG_RTL8189FS_NETIF_SG
#define CONFIG_RTW_NETIF_SG			1
#endif
#define CONFIG_TXPWR_BY_RATE			1
#define CONFIG_TXPWR_BY_RATE_EN			1
#define CONFIG_TXPWR_LIMIT			1
#define CONFIG_TXPWR_LIMIT_EN			0
#define CONFIG_RTW_ADAPTIVITY_EN		0
#define CONFIG_RTW_ADAPTIVITY_MODE		0
#define CONFIG_RTW_SDIO_PM_KEEP_POWER		1
#define CONFIG_LITTLE_ENDIAN			1
#define DM_ODM_SUPPORT_TYPE			0x04
#define HIGH_ACTIVE_HST2DEV			0

/* The GT flag list's default verbosity; the case lowers the runtime variable
 * before module init (RTW_LOG_LEVEL is only the initial value). */
#define RTW_LOG_LEVEL				4

/* --- the two vendor search paths (absent files, opened only if present) --- */
#define EFUSE_MAP_PATH		"/system/etc/wifi/wifi_efuse_rtl8189fs.map"
#define WIFIMAC_PATH		"/data/wifimac.txt"
#define REALTEK_CONFIG_PATH	"/lib/firmware/"

/* --- departure switches (defaults are the GT face) ------------------------ */
#if CONFIG_RTL8189FS_WIFI_MONITOR
#define CONFIG_WIFI_MONITOR		1
#endif
#if CONFIG_RTL8189FS_TDLS
#define CONFIG_TDLS			1
#endif
#if CONFIG_RTL8189FS_BT_COEX
#define CONFIG_BT_COEXIST		1
#endif
#if CONFIG_RTL8189FS_PROC_DEBUG
#define CONFIG_PROC_DEBUG		1
#endif
#if CONFIG_RTL8189FS_BR_EXT
#define CONFIG_BR_EXT			1
#endif

/* --- the two unconditional autoconf answers ------------------------------
 *
 * autoconf.h defines CONFIG_AP_MODE and CONFIG_P2P unconditionally (it has no
 * include guard of its own, but drv_conf.h does).  Pull the vendor config in
 * here, after this face's own defines, then undo the two answers the profile
 * turned off.  drv_types.h's later #include <drv_conf.h> is the guarded second
 * include and does not re-define them.  This is the one place a profile can
 * turn the vendor's AP/P2P surface off without editing the vendor tree.
 */
#include "src/include/drv_conf.h"

#if !CONFIG_RTL8189FS_AP
#undef CONFIG_AP_MODE
#undef CONFIG_NATIVEAP_MLME
#undef CONFIG_HOSTAPD_MLME
#endif

#if !CONFIG_RTL8189FS_P2P
#undef CONFIG_P2P
#undef CONFIG_WFD
#undef CONFIG_P2P_REMOVE_GROUP_INFO
#undef CONFIG_P2P_PS
#undef CONFIG_P2P_OP_CHK_SOCIAL_CH
#undef CONFIG_CFG80211_ONECHANNEL_UNDER_CONCURRENT
#endif

#endif /* _RTL8189FS_DRIVER_CONFIG_H_ */

/*
 * Build-fixed definitions.
 *
 * This header is the only place where structural facts about the platform,
 * the board, the wireless family and the vendor ABI are turned into macros.
 * Values that come from the image configuration are in freewificam_config.h,
 * which this file includes.  There is no build-line -D and no #ifndef
 * fallback: a missing configuration symbol is a compile error.
 */
#ifndef FREEWIFCAM_BUILD_DEFS_H
#define FREEWIFCAM_BUILD_DEFS_H

#include "freewificam_config.h"

/* Toolchain / libc / kernel vocabulary. */
#define _GNU_SOURCE 1
#define _DEFAULT_SOURCE 1
#define __KERNEL__ 1
#define _KERNEL 1
#define MODULE 1
#define KBUILD_MODNAME "freewificam"
#define __KBUILD_MODNAME KBUILD_MODNAME
#define WLAN_PORT_TARGET 1
#define LKPI_ABS_PLATFORM_PROVIDED 1

/* CPU / exception level: the RTOS runs at EL1 in the guest. */
#ifndef GUEST
#define GUEST 1
#endif
#ifndef EL1
#define EL1 1
#endif

/* CMSIS-RTOS2 backend identity and limits.  The limits come from the image
 * configuration; there is no per-world fallback here. */
#if CONFIG_BACKEND_FREERTOS
#define CMSIS2_BACKEND_FREERTOS 1
#endif
#if CONFIG_BACKEND_THREADX
#define CMSIS2_BACKEND_THREADX 1
#define TX_INCLUDE_USER_DEFINE_FILE 1
#define TX_LINUX_MULTI_CORE 1
#endif
#if CONFIG_BACKEND_ZEPHYR
#define CMSIS2_BACKEND_ZEPHYR 1
#endif

/* ULOG / printf trimming: Kconfig booleans, mapped to the names the code and
 * the vendor printf use.  False means "not defined", matching the old -D. */
#if CONFIG_ULOG_ENABLED
#define ULOG_ENABLED 1
#endif
#if CONFIG_PRINTF_DISABLE_FLOAT
#define PRINTF_DISABLE_SUPPORT_FLOAT 1
#endif
#if CONFIG_PRINTF_DISABLE_EXPONENTIAL
#define PRINTF_DISABLE_SUPPORT_EXPONENTIAL 1
#endif

/* Board / port constants.  These are properties of the board, not knobs. */
#define USB_BOARD_IRQ_PRIORITY_RAW BOARD_IRQ_PRIORITY_API_CALL_RAW

/* The USB mass-storage diagnostic arm used to be three hand flags.  They are a
 * consequence of the Kconfig symbol, so the fixed header derives them here. */
#if CONFIG_USB_MSC_DIAG
#define MSC_DIAG 1
#define CONFIG_USBHOST_XHCI_DBG_DUMP 1
#endif
#if CONFIG_USB_MSC_DIAG_FULL
#define MSC_DIAG_FULL 1
#endif
#define EQOS_BOARD_IRQ_PRIORITY_RAW 0xd0u
#if CONFIG_BOARD_FIREFLY_V1
#define BOARD_FIREFLY_V1 1
#endif
#if CONFIG_BOARD_FIREFLY_V2
#define BOARD_FIREFLY_V2 1
#endif

/* Wireless family / vendor ABI.  The family is selected by the case set, so
 * the same fixed header serves every image and no per-image -D is needed. */
#if defined(RTOS_CASE_WLAN)
#define CONFIG_RTL8188F 1
#define CONFIG_MP_INCLUDED 1
#define CONFIG_BR_EXT 1
#define CONFIG_BR_EXT_BRNAME "br0"
#define CONFIG_CONCURRENT_MODE 1
#define CONFIG_POWER_SAVING 1
#define CONFIG_RTW_DEBUG 1
#define CONFIG_EFUSE_CONFIG_FILE 1
#define CONFIG_LOAD_PHY_PARA_FROM_FILE 1
#define CONFIG_TXPWR_BY_RATE 1
#define CONFIG_TXPWR_BY_RATE_EN 1
#define CONFIG_TXPWR_LIMIT 1
#define CONFIG_TXPWR_LIMIT_EN 0
#define CONFIG_RTW_ADAPTIVITY_EN 0
#define CONFIG_RTW_ADAPTIVITY_MODE 0
#define CONFIG_RTW_NETIF_SG 1
#define CONFIG_RTW_SDIO_PM_KEEP_POWER 1
#define CONFIG_IOCTL_CFG80211 1
#define RTW_USE_CFG80211_STA_EVENT 1
#define CONFIG_LITTLE_ENDIAN 1
#define RTW_LOG_LEVEL 2
#define EFUSE_MAP_PATH "/system/etc/wifi/wifi_efuse_8189fs.map"
#define REALTEK_CONFIG_PATH "/lib/firmware/"
#define WIFIMAC_PATH "/data/wifimac.txt"
#define DM_ODM_SUPPORT_TYPE 0x04
#define HIGH_ACTIVE_HST2DEV 0
#define __KBUILD_MODNAME 8189fs
#define WLAN_BRIDGE_ADAPTER_NAME "rtl8189fs"
#define WLAN_BRIDGE_FIRMWARE_NAME ""
#define SDMMC_PORT_SDIO_HOST_TYPE kSDMMCHOST_TYPE_FDWMMC
#endif

#if defined(RTOS_CASE_RTW89)
#define CONFIG_RTW89_CORE 1
#define CONFIG_RTW89_8851B 1
#define CONFIG_RTW89_USB 1
#define CONFIG_RTW89_8851BU 1
#define WLAN_BRIDGE_ADAPTER_NAME "rtw89"
#define WLAN_BRIDGE_FIRMWARE_NAME "rtw89/rtw8851b_fw-1.bin"
#endif

#if defined(RTOS_CASE_RTW88)
#define CONFIG_RTW88_CORE 1
#define CONFIG_RTW88_8821C 1
#define CONFIG_RTW88_SDIO 1
#define CONFIG_RTW88_8821CS 1
#define WLAN_BRIDGE_ADAPTER_NAME "rtw88"
#define WLAN_BRIDGE_FIRMWARE_NAME "rtw88/rtw8821c_fw.bin"
#define WLAN_SEAM_NO_USB_OBS 1
#endif

#if defined(RTOS_CASE_RTL8XXXU)
#define WLAN_BRIDGE_ADAPTER_NAME "rtl8xxxu"
#ifdef CONFIG_RTL8XXXU_CHIP_8723BU
#define WLAN_BRIDGE_FIRMWARE_NAME "rtl8723bu_fw.bin"
#define RTL8XXXU_FW_REGISTER lkpi_fw_rtl8723bu_register
#define RTL8XXXU_CLAIM_ID "0bda:b720"
#else
#define WLAN_BRIDGE_FIRMWARE_NAME "rtl8188eufw.bin"
#define RTL8XXXU_FW_REGISTER lkpi_fw_rtl8188eufw_register
#define RTL8XXXU_CLAIM_ID "0bda:8179"
#endif
#endif

#if defined(RTOS_CASE_RTW88) || defined(RTOS_CASE_SDIO_MCI)
#define SDMMC_PORT_SDIO_HOST_TYPE kSDMMCHOST_TYPE_FIREFLY_MCI
#define SDMMC_PORT_SDIO_HOST_ID 1
#endif

/* Firefly v2 uses the same MCI host for the rtw88 SDIO line. */
#if defined(CONFIG_BOARD_FIREFLY_V2) && defined(RTOS_CASE_RTW88)
#define SDMMC_PORT_SDIO_HOST_TYPE kSDMMCHOST_TYPE_FIREFLY_MCI
#define SDMMC_PORT_SDIO_HOST_ID 1
#endif

#ifdef RTOS_CASE_IWL7260
#define CONFIG_WLAN_DRIVER_IWLWIFI 1
#define CONFIG_IWLWIFI 1
#define CONFIG_IWLMVM 1
#define CONFIG_IWLWIFI_DEBUG 1
#define WLAN_BRIDGE_ADAPTER_NAME "iwlwifi"
#define WLAN_BRIDGE_FIRMWARE_NAME "iwlwifi-7260-17.ucode"
#define WLAN_SEAM_NO_USB_OBS 1
#endif

/* Case-derived FSL SDIO/MMC selection: the old CASE_DEFS did this, and the
 * FSL wrapper has no other source for it. */
#if defined(RTOS_CASE_UVC_DETECT) || defined(RTOS_CASE_UVC_ISO_SMOKE) || \
    defined(RTOS_CASE_UVC_OPEN) || defined(RTOS_CASE_UVC_STREAM)
#define CONFIG_USB_EHCI_ISO 1
#endif

#if defined(RTOS_CASE_SDIO_MCI)
#define CONFIG_FSL_SDMMC_ENABLE_MMC 1
#define CONFIG_FSL_SDMMC_ENABLE_SDIO 1
#endif
#if defined(RTOS_CASE_WLAN) || defined(RTOS_CASE_RTW88) || defined(RTOS_CASE_SDIO_MCI)
#define CONFIG_FSL_SDMMC_ENABLE_SDIO 1
#endif

/* WLAN observation is a Kconfig choice, but the seam code uses the older
 * CONFIG_LKPI_CENSUS spelling. */
#if CONFIG_WLAN_OBS
#define CONFIG_LKPI_CENSUS 1
#define LKPI_SDIO_OBS 1
#define LKPI_SDIO_OBS_HOST_SUBMIT 1
#define LKPI_SDIO_OBS_IRQ 1
#endif

/* rtw88 / iwlwifi host builds use the host tier; the forced-include path is
 * only used for target objects, so there is no host-only definition here. */

#endif /* FREEWIFCAM_BUILD_DEFS_H */

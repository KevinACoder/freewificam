/*
 * Copyright 2016-2022 NXP
 * Copyright (c) 2026 freewificam contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Minimal first-party shadow of the SDK's fsl_common.h for the imported SDIO
 * units. Only the declarations the SDIO compile set uses are kept; the
 * signatures and constant values are verbatim from the upstream header. The
 * platform glue the upstream header pulls in (debug console, SDK configuration,
 * kernel helpers) is not carried - the console macro is bound to this project's
 * log outlet instead.
 */

#ifndef FSL_COMMON_H_SHADOW
#define FSL_COMMON_H_SHADOW

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Console binding: the imported units print progress through these macros. The
 * upstream tree binds the levelled forms to its own debug console, whose macro
 * takes the unit's tag as the first argument; the tag is carried here as a
 * suffix on the message, so a line states which unit emitted it and the
 * project's log levels stay the ones that decide whether it is shown. */
#define SDMMC_LOG(...) LOG_INFO(__VA_ARGS__)
#define SDMMC_LOGD(tag, format, ...) LOG_DEBUG(format "%s", ##__VA_ARGS__, tag)
#define SDMMC_LOGI(tag, format, ...) LOG_INFO(format "%s", ##__VA_ARGS__, tag)
#define SDMMC_LOGW(tag, format, ...) LOG_WARNING(format "%s", ##__VA_ARGS__, tag)
#define SDMMC_LOGE(tag, format, ...) LOG_ERROR(format "%s", ##__VA_ARGS__, tag)

/* Sizes, values verbatim from the SDK's kernel helper header. The MMC card
 * dump prints a capacity in gigabytes and divides by the first of them. */
#if !defined(SZ_1K)
#define SZ_1K				0x00000400
#endif
#if !defined(SZ_4K)
#define SZ_4K				0x00001000
#endif
#if !defined(SZ_1M)
#define SZ_1M				0x00100000
#endif
#if !defined(SZ_1G)
#define SZ_1G				0x40000000
#endif

/*! @brief Construct a status code value from a group and code number. */
#define MAKE_STATUS(group, code) ((((group)*100L) + (code)))

/*! @brief Construct the version number for drivers. */
#define MAKE_VERSION(major, minor, bugfix) (((major)*65536L) + ((minor)*256L) + (bugfix))

/*! @brief common driver version. */
#define FSL_COMMON_DRIVER_VERSION (MAKE_VERSION(2, 4, 0))

/*! @brief Status group numbers (values verbatim from upstream). */
enum _status_groups
{
    kStatusGroup_Generic               = 0,
    kStatusGroup_FLASH                 = 1,
    kStatusGroup_LPSPI                 = 4,
    kStatusGroup_FLEXIO_SPI            = 5,
    kStatusGroup_DSPI                  = 6,
    kStatusGroup_FLEXIO_UART           = 7,
    kStatusGroup_FLEXIO_I2C            = 8,
    kStatusGroup_LPI2C                 = 9,
    kStatusGroup_UART                  = 10,
    kStatusGroup_I2C                   = 11,
    kStatusGroup_LPSCI                 = 12,
    kStatusGroup_LPUART                = 13,
    kStatusGroup_SPI                   = 14,
    kStatusGroup_XRDC                  = 15,
    kStatusGroup_SEMA42                = 16,
    kStatusGroup_SDHC                  = 17,
    kStatusGroup_SDMMC                 = 18,
    kStatusGroup_SAI                   = 19,
    kStatusGroup_MCG                   = 20,
    kStatusGroup_SCG                   = 21,
    kStatusGroup_SDSPI                 = 22,
    kStatusGroup_FLEXIO_I2S            = 23,
    kStatusGroup_FLEXIO_MCULCD         = 24,
    kStatusGroup_FLASHIAP              = 25,
    kStatusGroup_FLEXCOMM_I2C          = 26,
    kStatusGroup_I2S                   = 27,
    kStatusGroup_IUART                 = 28,
    kStatusGroup_CSI                   = 29,
    kStatusGroup_MIPI_DSI              = 30,
    kStatusGroup_SDRAMC                = 35,
    kStatusGroup_POWER                 = 39,
    kStatusGroup_ENET                  = 40,
    kStatusGroup_PHY                   = 41,
    kStatusGroup_TRGMUX                = 42,
    kStatusGroup_SMARTCARD             = 43,
    kStatusGroup_LMEM                  = 44,
    kStatusGroup_QSPI                  = 45,
    kStatusGroup_DMA                   = 50,
    kStatusGroup_EDMA                  = 51,
    kStatusGroup_DMAMGR                = 52,
    kStatusGroup_FLEXCAN               = 53,
    kStatusGroup_LTC                   = 54,
    kStatusGroup_FLEXIO_CAMERA         = 55,
    kStatusGroup_LPC_SPI               = 56,
    kStatusGroup_LPC_USART             = 57,
    kStatusGroup_DMIC                  = 58,
    kStatusGroup_SDIF                  = 59,
    kStatusGroup_SPIFI                 = 60,
    kStatusGroup_OTP                   = 61,
    kStatusGroup_MCAN                  = 62,
    kStatusGroup_CAAM                  = 63,
    kStatusGroup_ECSPI                 = 64,
    kStatusGroup_USDHC                 = 65,
    kStatusGroup_LPC_I2C               = 66,
    kStatusGroup_DCP                   = 67,
    kStatusGroup_MSCAN                 = 68,
    kStatusGroup_ESAI                  = 69,
    kStatusGroup_FLEXSPI               = 70,
    kStatusGroup_MMDC                  = 71,
    kStatusGroup_PDM                   = 72,
    kStatusGroup_SDMA                  = 73,
    kStatusGroup_ICS                   = 74,
    kStatusGroup_SPDIF                 = 75,
    kStatusGroup_LPC_MINISPI           = 76,
    kStatusGroup_HASHCRYPT             = 77,
    kStatusGroup_LPC_SPI_SSP           = 78,
    kStatusGroup_I3C                   = 79,
    kStatusGroup_LPC_I2C_1             = 97,
    kStatusGroup_NOTIFIER              = 98,
    kStatusGroup_DebugConsole          = 99,
    kStatusGroup_SEMC                  = 100,
    kStatusGroup_ApplicationRangeStart = 101,
    kStatusGroup_IAP                   = 102,
    kStatusGroup_SFA                   = 103,
    kStatusGroup_SPC                   = 104,
    kStatusGroup_PUF                   = 105,
    kStatusGroup_TOUCH_PANEL           = 106,
    kStatusGroup_VBAT                  = 107,
    kStatusGroup_HAL_GPIO              = 121,
    kStatusGroup_HAL_UART              = 122,
    kStatusGroup_HAL_TIMER             = 123,
    kStatusGroup_HAL_SPI               = 124,
    kStatusGroup_HAL_I2C               = 125,
    kStatusGroup_HAL_FLASH             = 126,
    kStatusGroup_HAL_PWM               = 127,
    kStatusGroup_HAL_RNG               = 128,
    kStatusGroup_HAL_I2S               = 129,
    kStatusGroup_HAL_ADC_SENSOR        = 130,
    kStatusGroup_TIMERMANAGER          = 135,
    kStatusGroup_SERIALMANAGER         = 136,
    kStatusGroup_LED                   = 137,
    kStatusGroup_BUTTON                = 138,
    kStatusGroup_EXTERN_EEPROM         = 139,
    kStatusGroup_SHELL                 = 140,
    kStatusGroup_MEM_MANAGER           = 141,
    kStatusGroup_LIST                  = 142,
    kStatusGroup_OSA                   = 143,
    kStatusGroup_COMMON_TASK           = 144,
    kStatusGroup_MSG                   = 145,
    kStatusGroup_SDK_OCOTP             = 146,
    kStatusGroup_SDK_FLEXSPINOR        = 147,
    kStatusGroup_CODEC                 = 148,
    kStatusGroup_ASRC                  = 149,
    kStatusGroup_OTFAD                 = 150,
    kStatusGroup_SDIOSLV               = 151,
    kStatusGroup_MECC                  = 152,
    kStatusGroup_ENET_QOS              = 153,
    kStatusGroup_LOG                   = 154,
    kStatusGroup_I3CBUS                = 155,
    kStatusGroup_QSCI                  = 156,
    kStatusGroup_SNT                   = 157,
    kStatusGroup_QUEUEDSPI             = 158,
    kStatusGroup_POWER_MANAGER         = 159,
    kStatusGroup_IPED                  = 160,
    kStatusGroup_ELS_PKC              = 161,
    kStatusGroup_CSS_PKC              = 162,
    kStatusGroup_HOSTIF                = 163,
    kStatusGroup_CLIF                  = 164,
    kStatusGroup_BMA                   = 165,
    kStatusGroup_NETC                  = 166,
};

/*! @brief Generic status return codes (values verbatim from upstream). */
enum
{
    kStatus_Success         = MAKE_STATUS(kStatusGroup_Generic, 0),
    kStatus_Fail            = MAKE_STATUS(kStatusGroup_Generic, 1),
    kStatus_ReadOnly        = MAKE_STATUS(kStatusGroup_Generic, 2),
    kStatus_OutOfRange      = MAKE_STATUS(kStatusGroup_Generic, 3),
    kStatus_InvalidArgument = MAKE_STATUS(kStatusGroup_Generic, 4),
    kStatus_Timeout         = MAKE_STATUS(kStatusGroup_Generic, 5),
    kStatus_NoTransferInProgress = MAKE_STATUS(kStatusGroup_Generic, 6),
    kStatus_Busy            = MAKE_STATUS(kStatusGroup_Generic, 7),
    kStatus_NoData          = MAKE_STATUS(kStatusGroup_Generic, 8),
};

/*! @brief Type used for all status and error return values. */
typedef int32_t status_t;

#if !defined(MIN)
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#if !defined(MAX)
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#if !defined(ARRAY_SIZE)
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#if !defined(UINT16_MAX)
#define UINT16_MAX ((uint16_t)-1)
#endif

#if !defined(UINT32_MAX)
#define UINT32_MAX ((uint32_t)-1)
#endif

#if !defined(UINTPTR_SIZE)
#if UINTPTR_MAX > UINT32_MAX
#define UINTPTR_SIZE 8
#elif UINTPTR_MAX > UINT16_MAX
#define UINTPTR_SIZE 4
#else
#error "UINTPTR_SIZE is unknown!"
#endif
#endif

#if defined(__GNUC__) && !defined(__ARMCC_VERSION)
#define SUPPRESS_FALL_THROUGH_WARNING() __attribute__((fallthrough))
#else
#define SUPPRESS_FALL_THROUGH_WARNING()
#endif

static inline uint32_t __REV(uint32_t value)
{
    return __builtin_bswap32(value);
}

static inline uint16_t __REV16(uint16_t value)
{
    return __builtin_bswap16(value);
}

static inline uintptr_t __ALIGN(uintptr_t addr, uintptr_t alignment, size_t size, size_t *usage)
{
    uintptr_t mask  = alignment - 1;
    uintptr_t start = addr;

    start += ((addr & mask) ? (alignment - (addr & mask)) : 0);
    if (usage)
    {
        *usage = ((start + size) - addr);
    }
    return start;
}

static inline unsigned int __FIND_MSB_SET(uint32_t op)
{
    if (op == 0)
    {
        return 0;
    }

    return 32 - __builtin_clz(op);
}

#ifdef __cplusplus
}
#endif

#endif /* FSL_COMMON_H_SHADOW */

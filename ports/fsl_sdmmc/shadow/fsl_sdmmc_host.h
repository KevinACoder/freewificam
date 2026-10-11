/*
 * Copyright (c) 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2020 NXP
 * Copyright (c) 2026 freewificam contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Minimal first-party shadow of the SDK's sdmmc host header. The card command
 * and data descriptors, the host ops table, the host instance and the inline
 * dispatchers are verbatim from the upstream header; the wrappers the imported
 * SDIO compile set does not call (the MMC boot path, HS400/strobe helpers, the
 * endian converter, card-busy and card-interrupt enabling) are not carried - the
 * interrupt tier is deferred by the design and will re-add its wrapper when needed.
 */

#ifndef FSL_SDMMC_HOST_H_SHADOW
#define FSL_SDMMC_HOST_H_SHADOW

#include "fsl_sdmmc_common.h"
#include "fsl_sdmmc_osa.h"

#ifdef __cplusplus
extern "C" {
#endif

/*! @brief Middleware adapter version. */
#define FSL_SDMMC_HOST_ADAPTER_VERSION (MAKE_VERSION(2U, 6U, 3U))

/*! @brief sdmmc host capability */
enum
{
    kSDMMCHOST_SupportHighSpeed         = 1U << 0U,
    kSDMMCHOST_SupportSuspendResume     = 1U << 1U,
    kSDMMCHOST_SupportVoltage3v3        = 1U << 2U,
    kSDMMCHOST_SupportVoltage3v0        = 1U << 3U,
    kSDMMCHOST_SupportVoltage1v8        = 1U << 4U,
    kSDMMCHOST_SupportVoltage1v2        = 1U << 5U,
    kSDMMCHOST_Support4BitDataWidth     = 1U << 6U,
    kSDMMCHOST_Support8BitDataWidth     = 1U << 7U,
    kSDMMCHOST_SupportDDRMode           = 1U << 8U,
    kSDMMCHOST_SupportDetectCardByData3 = 1U << 9U,
    kSDMMCHOST_SupportDetectCardByCD    = 1U << 10U,
    kSDMMCHOST_SupportAutoCmd12         = 1U << 11U,
    kSDMMCHOST_SupportSDR104            = 1U << 12U,
    kSDMMCHOST_SupportSDR50             = 1U << 13U,
    kSDMMCHOST_SupportHS200             = 1U << 14U,
    kSDMMCHOST_SupportHS400             = 1U << 15U,
    kSDMMCHOST_SupportDriverTypeC       = 1U << 16U,
    kSDMMCHOST_SupportSetCurrent        = 1U << 17U,
};

/*!@brief SDMMC host dma descriptor buffer address align size */
#define SDMMCHOST_DMA_DESCRIPTOR_BUFFER_ALIGN_SIZE (4U)

#define SDHC_BLKATTR_BLKCNT_MASK  (0xFFFF0000U)
#define SDHC_BLKATTR_BLKCNT_SHIFT (16U)

/*! @brief Maximum block count can be set one time */
#define SDMMCHOST_SUPPORT_MAX_BLOCK_LENGTH (4096U)
#define SDMMCHOST_SUPPORT_MAX_BLOCK_COUNT  (SDHC_BLKATTR_BLKCNT_MASK >> SDHC_BLKATTR_BLKCNT_SHIFT)

/*! @brief SDMMC host reset timoue value */
#define SDMMCHOST_RESET_TIMEOUT_VALUE (1000000U)

/*! @brief The command type */
typedef enum sdmmc_card_command_type_t
{
    kCARD_CommandTypeNormal  = 0U,
    kCARD_CommandTypeSuspend = 1U,
    kCARD_CommandTypeResume  = 2U,
    kCARD_CommandTypeAbort   = 3U,
    kCARD_CommandTypeEmpty   = 4U,
} sdmmc_card_command_type_t;

/*! @brief The command response type. */
typedef enum sdmmc_card_response_type_t
{
    kCARD_ResponseTypeNone = 0U,
    kCARD_ResponseTypeR1   = 1U,
    kCARD_ResponseTypeR1b  = 2U,
    kCARD_ResponseTypeR2   = 3U,
    kCARD_ResponseTypeR3   = 4U,
    kCARD_ResponseTypeR4   = 5U,
    kCARD_ResponseTypeR5   = 6U,
    kCARD_ResponseTypeR5b  = 7U,
    kCARD_ResponseTypeR6   = 8U,
    kCARD_ResponseTypeR7   = 9U,
} sdmmc_card_response_type_t;

/*! @brief MMC card boot mode */
typedef enum sdmmc_boot_mode_t
{
    kUSDHC_BootModeNormal      = 0U,
    kUSDHC_BootModeAlternative = 1U,
} sdmmc_boot_mode_t;

/*! @brief Card data descriptor. */
typedef struct sdmmchost_data_t
{
    bool streamTransfer;
    bool enableAutoCommand12;
    bool enableAutoCommand23;
    bool enableIgnoreError;
    uint8_t dataType;
    size_t blockSize;
    uint32_t blockCount;
    uint32_t *rxData;
    const uint32_t *txData;
} sdmmchost_data_t;

/*! @brief Card command descriptor. */
typedef struct sdmmchost_cmd_t
{
    uint32_t index;
    uint32_t argument;
    sdmmc_card_command_type_t type;
    sdmmc_card_response_type_t responseType;
    uint32_t response[4U];
    uint32_t responseErrorFlags;
    uint32_t flags;
} sdmmchost_cmd_t;

/*! @brief Transfer state. */
typedef struct sdmmchost_transfer_t
{
    sdmmchost_data_t *data;
    sdmmchost_cmd_t *command;
} sdmmchost_transfer_t;

typedef struct sdmmchost_boot_config_t
{
    uint32_t ackTimeoutCount;
    sdmmc_boot_mode_t bootMode;
    uint32_t blockCount;
    size_t blockSize;
    bool enableBootAck;
    bool enableAutoStopAtBlockGap;
} sdmmchost_boot_config_t;

/*! @brief host Endian mode */
enum
{
    kSDMMCHOST_EndianModeBig         = 0U,
    kSDMMCHOST_EndianModeHalfWordBig = 1U,
    kSDMMCHOST_EndianModeLittle      = 2U,
};

/*! @brief sdmmc host tuning type */
enum
{
    kSDMMCHOST_StandardTuning = 0U,
    kSDMMCHOST_ManualTuning   = 1U,
};

/*! @brief sdmmc host maintain cache flag */
enum
{
    kSDMMCHOST_NoCacheControl       = 0U,
    kSDMMCHOST_CacheControlRWBuffer = 1U,
};

typedef struct _sdmmchost_ sdmmchost_t;

/*!@brief sdmmc host operations (verbatim interface; members a later feature
 * does not use are left unset by the host driver). */
typedef struct _sdmmchost_ops
{
    void (*deinit)(sdmmchost_t *host);
    status_t (*reset)(sdmmchost_t *host);

    void (*switchToVoltage)(sdmmchost_t *host, uint32_t voltage);
    status_t (*executeTuning)(sdmmchost_t *host, uint32_t tuningCmd, uint32_t *revBuf, uint32_t blockSize);
    void (*enableDDRMode)(sdmmchost_t *host, bool enable, uint32_t nibblePos);
    void (*enableHS400Mode)(sdmmchost_t *host, bool enable);
    void (*enableStrobeDll)(sdmmchost_t *host, bool enable);
    uint32_t (*getSignalLineStatus)(sdmmchost_t *host, uint32_t signalLine);
    void (*convertDataToLittleEndian)(sdmmchost_t *host, uint32_t *data, uint32_t wordSize, uint32_t format);

    status_t (*cardDetectInit)(sdmmchost_t *host, void *cd);
    void (*cardSetPower)(sdmmchost_t *host, bool enable);
    void (*cardForceClockOn)(sdmmchost_t *host, bool enable);
    void (*cardEnableInt)(sdmmchost_t *host, bool enable);
    status_t (*cardIntInit)(sdmmchost_t *host, void *sdioInt);
    void (*cardSetBusWidth)(sdmmchost_t *host, uint32_t dataBusWidth);
    status_t (*cardPollingDetectStatus)(sdmmchost_t *host, uint32_t waitCardStatus, uint32_t timeout);
    uint32_t (*cardDetectStatus)(sdmmchost_t *host);
    void (*cardSendActive)(sdmmchost_t *host);
    uint32_t (*cardSetClock)(sdmmchost_t *host, uint32_t targetClock);
    bool (*cardIsBusy)(sdmmchost_t *host);

    status_t (*transferFunction)(sdmmchost_t *host, sdmmchost_transfer_t *content);

    status_t (*startBoot)(sdmmchost_t *host, sdmmchost_boot_config_t *hostConfig, sdmmchost_cmd_t *cmd, uint8_t *buffer);
    status_t (*readBootData)(sdmmchost_t *host, sdmmchost_boot_config_t *hostConfig, uint8_t *buffer);
    void (*enableBoot)(sdmmchost_t *host, bool enable);
} sdmmchost_ops_t;

/*!@brief sdmmc instance configurations */
typedef struct _sdmmchost_config_
{
    uint32_t hostId;
    uint32_t hostType;
#define kSDMMCHOST_TYPE_FSDMMC   1
#define kSDMMCHOST_TYPE_FSDIF    2
#define kSDMMCHOST_TYPE_FSDIF_V2 3
#define kSDMMCHOST_TYPE_FDWMMC   4
#define kSDMMCHOST_TYPE_FDWMSHC  5
    uint32_t cardType;
#define kSDMMCHOST_CARD_TYPE_STANDARD_SD 1
#define kSDMMCHOST_CARD_TYPE_MICRO_SD    2
#define kSDMMCHOST_CARD_TYPE_EMMC        3
#define kSDMMCHOST_CARD_TYPE_SDIO        4
    bool enableIrq;
    bool enableDMA;
    bool prefixedTiming;
    uint32_t endianMode;
    size_t maxTransSize;
    size_t defBlockSize;
    uint32_t cardClock;
    bool isUHSCard;
    void (*sdioCardInt)(void *userData);
    void *sdioCardIntArg;
} sdmmchost_config_t;

/*!@brief sdmmc host handler */
typedef struct _sdmmchost_
{
    void *dev;
    sdmmchost_ops_t ops;
    sdmmchost_config_t config;
    uint32_t currVoltage;
    uint32_t currBusWidth;
    uint32_t currClockFreq;

    uint32_t sourceClock_Hz;
    uint32_t capability;
    uint32_t maxBlockCount;
    uint32_t maxBlockSize;

    uint8_t tuningType;

    sdmmc_osa_event_t hostEvent;
    void *card;
    void *cd;
    void *cardInt;

    sdmmc_osa_mutex_t lock;
} sdmmchost_t;

/*! @brief Init host controller (implemented by the board host driver). */
status_t SDMMCHOST_Init(sdmmchost_t *host);

static inline status_t SDMMCHOST_Reset(sdmmchost_t *host)
{
    if (host->ops.reset)
    {
        return host->ops.reset(host);
    }
    return kStatus_Fail;
}

static inline void SDMMCHOST_Deinit(sdmmchost_t *host)
{
    if (host->ops.deinit)
    {
        host->ops.deinit(host);
    }
}

static inline status_t SDMMCHOST_CardDetectInit(sdmmchost_t *host, void *cd)
{
    if (host->ops.cardDetectInit)
    {
        return host->ops.cardDetectInit(host, cd);
    }
    return kStatus_Fail;
}

static inline void SDMMCHOST_SetCardPower(sdmmchost_t *host, bool enable)
{
    if (host->ops.cardSetPower)
    {
        host->ops.cardSetPower(host, enable);
    }
}

static inline void SDMMCHOST_SetCardBusWidth(sdmmchost_t *host, uint32_t dataBusWidth)
{
    if (host->ops.cardSetBusWidth)
    {
        host->ops.cardSetBusWidth(host, dataBusWidth);
    }
}

static inline void SDMMCHOST_SendCardActive(sdmmchost_t *host)
{
    if (host->ops.cardSendActive)
    {
        host->ops.cardSendActive(host);
    }
}

static inline uint32_t SDMMCHOST_SetCardClock(sdmmchost_t *host, uint32_t targetClock)
{
    if (host->ops.cardSetClock)
    {
        return host->ops.cardSetClock(host, targetClock);
    }
    return 0U;
}

static inline uint32_t SDMMCHOST_GetSignalLineStatus(sdmmchost_t *host, uint32_t signalLine)
{
    if (host->ops.getSignalLineStatus)
    {
        return host->ops.getSignalLineStatus(host, signalLine);
    }
    return 1U;
}

static inline void SDMMCHOST_EnableDDRMode(sdmmchost_t *host, bool enable, uint32_t nibblePos)
{
    if (host->ops.enableDDRMode)
    {
        host->ops.enableDDRMode(host, enable, nibblePos);
    }
}

static inline status_t SDMMCHOST_CardIntInit(sdmmchost_t *host, void *sdioInt)
{
    if (host->ops.cardIntInit)
    {
        return host->ops.cardIntInit(host, sdioInt);
    }
    return kStatus_Fail;
}

static inline void SDMMCHOST_ForceClockOn(sdmmchost_t *host, bool enable)
{
    if (host->ops.cardForceClockOn)
    {
        host->ops.cardForceClockOn(host, enable);
    }
}

static inline void SDMMCHOST_SwitchToVoltage(sdmmchost_t *host, uint32_t voltage)
{
    if (host->ops.switchToVoltage)
    {
        host->ops.switchToVoltage(host, voltage);
    }
}

static inline status_t SDMMCHOST_PollingCardDetectStatus(sdmmchost_t *host, uint32_t waitCardStatus, uint32_t timeout)
{
    if (host->ops.cardPollingDetectStatus)
    {
        return host->ops.cardPollingDetectStatus(host, waitCardStatus, timeout);
    }
    return kStatus_Fail;
}

static inline uint32_t SDMMCHOST_CardDetectStatus(sdmmchost_t *host)
{
    if (host->ops.cardDetectStatus)
    {
        return host->ops.cardDetectStatus(host);
    }
    return 0U;
}

static inline status_t SDMMCHOST_TransferFunction(sdmmchost_t *host, sdmmchost_transfer_t *content)
{
    if (host->ops.transferFunction)
    {
        return host->ops.transferFunction(host, content);
    }
    return kStatus_Fail;
}

static inline status_t SDMMCHOST_ExecuteTuning(sdmmchost_t *host, uint32_t tuningCmd, uint32_t *revBuf, uint32_t blockSize)
{
    if (host->ops.executeTuning)
    {
        return host->ops.executeTuning(host, tuningCmd, revBuf, blockSize);
    }
    return kStatus_Fail;
}

#ifdef __cplusplus
}
#endif

#endif /* FSL_SDMMC_HOST_H_SHADOW */

/*
 * @file   sdmmc_host_dwcmshc.c
 * @brief  The imported stack's host glue for this board's eMMC controller: the
 *         ops table and the MMC capability wiring.
 *
 * The imported stack reaches its controller through one ops table, and the
 * upstream tree fills it in only for its own controllers - its MMC path arms
 * the FSDIF controller and nothing else. This file supplies the table over the
 * board driver in board/rk3568-itx/drivers/dwc_mshc.c, and
 * FDWMSHCHOST_MMCConfig, which states what this socket can do: eight data
 * lines, high speed up to the 50 MHz CRU slot, a card power rail that is always
 * on, and a 1.8 V IO rail that is fixed (so there is no voltage switch to
 * perform).
 *
 * What is deliberately absent from the table is the half of the stack's MMC
 * feature ladder this round does not carry: the tuning hook, the DDR/HS400
 * mode hooks and the voltage switch are left unset, and the capability bits
 * below do not claim them, so the stack's ladder stops at high speed instead of
 * taking a path whose clock chain is not implemented. An op left out is refused
 * by the stack's own dispatcher, which is the honest answer for a mode this
 * board's driver does not drive yet.
 *
 * @design adapter - ref ports/fsl_sdmmc/sdmmc_host_dwmmc.c (the same table for
 *         the SDIO socket, which this file mirrors) and upstream's
 *         host/fdwmshc/fsl_hc_fdwmshc.c (the MMC configuration function for
 *         this controller in the board lab's tree).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 freewificam contributors
 */

#include "fsl_common.h"
#include "fsl_sdmmc.h"

#include "dwc_mshc.h"
#include "sdmmc_port.h"
#include "sdmmc_mmc.h"

/* The glue's per-host state. One controller, one card. */
struct sdmmc_host_dev {
	struct dwc_mshc hc;
};

/* The instance SDMMCHOST_Init allocated, kept so the evidence lines can reach
 * the controller's own record of what it programmed without the card service
 * having to hold the stack's host structure open. */
static struct sdmmc_host_dev *sdmmc_host_dev_current;

int sdmmc_mmc_host_state_get(struct sdmmc_mmc_host_state *out)
{
	struct sdmmc_host_dev *dev = sdmmc_host_dev_current;

	if ((out == NULL) || (dev == NULL)) {
		return -1;
	}

	(void)memset(out, 0, sizeof(*out));
	out->clock_asked_hz = dev->hc.clock_hz;
	out->clock_hz = dev->hc.achieved_hz;
	out->cclk_sel = dev->hc.cclk_sel;
	out->width = dev->hc.width;
	out->present_bit = dwc_mshc_card_present(&dev->hc);

	return 0;
}

/* --- ops: lifecycle ------------------------------------------------------- */

static void fdwmshchost_deinit(sdmmchost_t *host)
{
	struct sdmmc_host_dev *dev = (struct sdmmc_host_dev *)host->dev;

	if (dev == NULL) {
		return;
	}

	dwc_mshc_deinit(&dev->hc);
	if (sdmmc_host_dev_current == dev) {
		sdmmc_host_dev_current = NULL;
	}
	SDMMC_OSAMemoryFree(dev);
	host->dev = NULL;
}

static status_t fdwmshchost_reset(sdmmchost_t *host)
{
	struct sdmmc_host_dev *dev = (struct sdmmc_host_dev *)host->dev;

	return (dwc_mshc_reset(&dev->hc) == DWC_MSHC_OK) ? kStatus_Success : kStatus_Fail;
}

/* --- ops: card interface -------------------------------------------------- */

static status_t fdwmshchost_card_detect_init(sdmmchost_t *host, void *cd)
{
	(void)host;
	(void)cd;

	/* The card is soldered: there is no detect line to arm. */
	return kStatus_Success;
}

static void fdwmshchost_card_set_power(sdmmchost_t *host, bool enable)
{
	(void)host;
	(void)enable;

	/* The card's 3.3 V rail is always on and the controller's bus-power bit is
	 * raised once at bring-up, so a power-down owes the next bring-up
	 * nothing. */
}

/* The bus-width mapping: the stack hands over an enumeration (1, 4 and 8 lines
 * are 0, 1 and 2) while the controller encodes a count of signal lines. */
static void fdwmshchost_card_set_bus_width(sdmmchost_t *host, uint32_t dataBusWidth)
{
	struct sdmmc_host_dev *dev = (struct sdmmc_host_dev *)host->dev;
	unsigned int width;

	switch (dataBusWidth) {
	case kSDMMC_BusWdith8Bit:
		width = 8U;
		break;
	case kSDMMC_BusWdith4Bit:
		width = 4U;
		break;
	case kSDMMC_BusWdith1Bit:
	default:
		width = 1U;
		break;
	}

	dwc_mshc_set_bus_width(&dev->hc, width);
	host->currBusWidth = width;
}

static status_t fdwmshchost_card_polling_detect_status(sdmmchost_t *host,
						       uint32_t waitCardStatus,
						       uint32_t timeout)
{
	(void)waitCardStatus;
	(void)timeout;

	/* Soldered card: there is nothing to wait for. */
	return (host != NULL) ? kStatus_Success : kStatus_SDMMC_CardDetectFailed;
}

static uint32_t fdwmshchost_card_detect_status(sdmmchost_t *host)
{
	(void)host;

	/* The card is on the board, not in a socket; the controller's own
	 * presence bit is not wired to anything on an eMMC package and is read
	 * out for the evidence lines instead of being trusted here. */
	return (uint32_t)kSD_Inserted;
}

static void fdwmshchost_card_send_active(sdmmchost_t *host)
{
	(void)host;

	/* The card is started by CMD0 with the initialization argument; this
	 * controller needs no host-side clock priming. */
}

static uint32_t fdwmshchost_card_set_clock(sdmmchost_t *host, uint32_t targetClock)
{
	struct sdmmc_host_dev *dev = (struct sdmmc_host_dev *)host->dev;
	uint32_t achieved = dwc_mshc_set_clock(&dev->hc, targetClock);

	if (achieved == 0U) {
		SDMMC_LOG("dwmshc: clock %u Hz refused\n", (unsigned)targetClock);
	}

	return achieved;
}

static bool fdwmshchost_card_is_busy(sdmmchost_t *host)
{
	struct sdmmc_host_dev *dev = (struct sdmmc_host_dev *)host->dev;

	/* DAT0 held low means the card is still programming. */
	return dwc_mshc_card_busy(&dev->hc);
}

static uint32_t fdwmshchost_signal_line_status(sdmmchost_t *host, uint32_t signalLine)
{
	(void)host;
	(void)signalLine;

	/* This controller has no line-status register, and the voltage-switch
	 * handshake that would ask is never reached: the capability declaration
	 * below claims no switchable voltage. */
	return 1U;
}

/* --- ops: not carried ----------------------------------------------------- */

/*
 * Everything below stays unset in the ops table, and each one belongs to a mode
 * this round does not drive:
 *   - executeTuning, enableDDRMode, enableHS400Mode, enableStrobeDll: the modes
 *     whose clock chain (the DLL lock and the CMD21 tuning loop) is the next
 *     round's work, and the capability bits below do not offer them;
 *   - switchToVoltage: this socket's IO rail is fixed at 1.8 V, so there is
 *     nothing to switch;
 *   - forceClockOn, cardEnableInt, cardIntInit: this controller's clock gating
 *     is not forced, and the card's interrupt line is not wired;
 *   - convertDataToLittleEndian: this host's memory order already matches the
 *     packet order;
 *   - startBoot, readBootData, enableBoot: the eMMC boot partitions, which the
 *     storage line does not use.
 */

/* --- the transfer path ---------------------------------------------------- */

static void fdwmshchost_convert_cmd(const sdmmchost_transfer_t *content,
				    struct dwc_mshc_cmd *cmd,
				    struct dwc_mshc_data *data)
{
	const sdmmchost_cmd_t *in = content->command;

	(void)memset(cmd, 0, sizeof(*cmd));
	cmd->index = in->index;
	cmd->arg = in->argument;
	cmd->resp = (in->responseType != kCARD_ResponseTypeNone);
	cmd->resp_long = (in->responseType == kCARD_ResponseTypeR2);

	/* Check the response CRC wherever one exists: R3 and R4 are the two
	 * response types without a CRC field. */
	cmd->resp_crc = cmd->resp && (in->responseType != kCARD_ResponseTypeR3) &&
			(in->responseType != kCARD_ResponseTypeR4);

	if (content->data == NULL) {
		cmd->data = NULL;
		return;
	}

	(void)memset(data, 0, sizeof(*data));
	if (content->data->rxData != NULL) {
		data->buf = (uint8_t *)content->data->rxData;
		data->write = false;
	} else {
		data->buf = (uint8_t *)content->data->txData;
		data->write = true;
	}
	data->blksz = (uint32_t)content->data->blockSize;
	data->blkcnt = content->data->blockCount;
	data->len = data->blksz * data->blkcnt;

	/* A per-transfer trace: a storage round moves tens of thousands of
	 * blocks, so it stays at the debug level. */
	LOG_DEBUG("emmc: data cmd=%u arg=0x%x blksz=%u blkcnt=%u len=%u %s\n",
		  cmd->index, cmd->arg, data->blksz, data->blkcnt, data->len,
		  data->write ? "write" : "read");

	cmd->data = data;
}

static status_t fdwmshchost_transfer(sdmmchost_t *host, sdmmchost_transfer_t *content)
{
	struct sdmmc_host_dev *dev = (struct sdmmc_host_dev *)host->dev;
	struct dwc_mshc_cmd cmd;
	struct dwc_mshc_data data;
	int ret;

	if ((content == NULL) || (content->command == NULL)) {
		return kStatus_InvalidArgument;
	}

	fdwmshchost_convert_cmd(content, &cmd, &data);

	ret = dwc_mshc_transfer(&dev->hc, &cmd);
	if (ret != DWC_MSHC_OK) {
		return kStatus_SDMMC_TransferFailed;
	}

	if (cmd.resp) {
		(void)memcpy(content->command->response, cmd.response,
			     sizeof(cmd.response));
	}

	return kStatus_Success;
}

static const sdmmchost_ops_t fdwmshchost_ops = {
	.deinit = fdwmshchost_deinit,
	.reset = fdwmshchost_reset,

	.switchToVoltage = NULL,
	.executeTuning = NULL,
	.enableDDRMode = NULL,
	.enableHS400Mode = NULL,
	.enableStrobeDll = NULL,
	.getSignalLineStatus = fdwmshchost_signal_line_status,
	.convertDataToLittleEndian = NULL,

	.cardDetectInit = fdwmshchost_card_detect_init,
	.cardSetPower = fdwmshchost_card_set_power,
	.cardForceClockOn = NULL,
	.cardEnableInt = NULL,
	.cardIntInit = NULL,
	.cardSetBusWidth = fdwmshchost_card_set_bus_width,
	.cardPollingDetectStatus = fdwmshchost_card_polling_detect_status,
	.cardDetectStatus = fdwmshchost_card_detect_status,
	.cardSendActive = fdwmshchost_card_send_active,
	.cardSetClock = fdwmshchost_card_set_clock,
	.cardIsBusy = fdwmshchost_card_is_busy,

	.transferFunction = fdwmshchost_transfer,

	.startBoot = NULL,
	.readBootData = NULL,
	.enableBoot = NULL,
};

/* --- the two entry points ------------------------------------------------- */

status_t SDMMCHOST_Init(sdmmchost_t *host)
{
	struct sdmmc_host_dev *dev;
	const struct dwc_mshc_cfg *cfg;

	assert(host != NULL);

	dev = SDMMC_OSAMemoryAllocate(sizeof(*dev));
	if (dev == NULL) {
		return kStatus_OutOfRange;
	}
	(void)memset(dev, 0, sizeof(*dev));

	host->ops = fdwmshchost_ops;
	host->dev = dev;

	cfg = dwc_mshc_board_cfg(host->config.hostId);
	if (cfg == NULL) {
		SDMMC_LOG("dwmshc: no controller for host id %u\n",
			  (unsigned int)host->config.hostId);
		SDMMC_OSAMemoryFree(dev);
		host->dev = NULL;
		return kStatus_InvalidArgument;
	}

	if (dwc_mshc_init(&dev->hc, cfg) != DWC_MSHC_OK) {
		SDMMC_LOG("dwmshc: controller bring-up failed\n");
		SDMMC_OSAMemoryFree(dev);
		host->dev = NULL;
		return kStatus_Fail;
	}

	sdmmc_host_dev_current = dev;

	/* The interrupt tier is not carried: a host that asks for it is refused
	 * rather than quietly run on the polling path. */
	if (host->config.enableIrq) {
		SDMMC_LOG("dwmshc: interrupt transfers are not carried\n");
		dwc_mshc_deinit(&dev->hc);
		SDMMC_OSAMemoryFree(dev);
		host->dev = NULL;
		return kStatus_SDMMC_HostNotSupport;
	}

	return kStatus_Success;
}

status_t FDWMSHCHOST_MMCConfig(sdmmc_mmc_t *sdmmc, sdmmchost_config_t *config)
{
	sdmmchost_t *host = &sdmmc->host;
	mmc_card_t *card = &sdmmc->card;

	assert(sdmmc != NULL);
	assert(config != NULL);

	card->usrParam.ioStrength = NULL;
	/* The clock the stack may use is this port's ceiling; the stack rounds its
	 * per-mode targets down to it. */
	card->usrParam.maxFreq = config->cardClock;
	card->usrParam.capability = (uint32_t)kSDMMC_Support8BitWidth |
				    (uint32_t)kMMC_SupportHighSpeed26MHZFlag |
				    (uint32_t)kMMC_SupportHighSpeed52MHZFlag;
	/* The card's supply is the board's 3.3 V rail; its IO rail is fixed at
	 * 1.8 V, which is why the window below is stated and no switch is
	 * offered. */
	card->hostVoltageWindowVCC = (uint32_t)kMMC_VoltageWindows270to360;
	card->hostVoltageWindowVCCQ = (uint32_t)kMMC_VoltageWindow170to195;
	card->noInteralAlign = false;
	/* Multi-block transfers are pre-announced with the block count (CMD23),
	 * which is what keeps a write from depending on the stop command's
	 * timing. */
	card->enablePreDefinedBlockCount = true;

	host->capability |= (uint32_t)kSDMMCHOST_Support4BitDataWidth |
			    (uint32_t)kSDMMCHOST_Support8BitDataWidth |
			    (uint32_t)kSDMMCHOST_SupportHighSpeed |
			    (uint32_t)kSDMMCHOST_SupportAutoCmd12;

	host->maxBlockCount = (uint32_t)(config->maxTransSize / config->defBlockSize);
	host->maxBlockSize = SDMMCHOST_SUPPORT_MAX_BLOCK_LENGTH;
	/* The source the clock table picks for the high-speed rate: the CRU's
	 * 50 MHz slot. Nothing in the stack divides by it - this host programs
	 * its own divider and reports the achieved rate - but it is the truth
	 * about where the clock comes from. */
	host->sourceClock_Hz = 50000000U;

	return kStatus_Success;
}

/*
 * @file   sdmmc_host_dwmmc.c
 * @brief  The imported SDIO stack's host glue for this board's DesignWare MMC
 *         controller: the ops table, the controller bring-up, and the SDIO
 *         capability wiring the upstream tree never wrote.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 freewificam contributors
 *
 * The imported stack reaches its controller through one ops table. Upstream
 * fills it for its own controllers only - its SDIO path arms the FSDIF
 * controller and nothing else, so there is no SDIO configuration function for
 * this controller anywhere in the tree. This file supplies both halves: the
 * table over the board driver in board/rk3568-itx/drivers/dwc_mmc.c, and
 * FDWMMCHOST_SDIOConfig, which states what this board's socket can do (3.3 V,
 * four data lines, high speed at 50 MHz, no card-detect pin, no 1.8 V rail).
 *
 * @design adapter - ref upstream:third-party/fsl_sdmmc host/fdwmmc/
 *         fsl_hc_fdwmmc.c (the ops table, the controller bring-up and the SD
 *         configuration function, one per card type) with two deliberate
 *         differences: the bus-width mapping is present (see
 *         fdwmmchost_card_set_bus_width), and only the polling transfer is
 *         carried, because this port's configuration brings the card up without
 *         a card interrupt line.
 */

#include "fsl_common.h"
#include "fsl_sdmmc.h"

#include "dwc_mmc.h"
#include "sdmmc_port.h"

/* The glue's per-host state. One controller, one card. */
struct sdmmc_host_dev {
	struct dwc_mmc hc;
};

/* --- ops: lifecycle ------------------------------------------------------- */

static void fdwmmchost_deinit(sdmmchost_t *host)
{
	struct sdmmc_host_dev *dev = (struct sdmmc_host_dev *)host->dev;

	if (dev == NULL) {
		return;
	}

	dwc_mmc_deinit(&dev->hc);
	SDMMC_OSAMemoryFree(dev);
	host->dev = NULL;
}

static status_t fdwmmchost_reset(sdmmchost_t *host)
{
	struct sdmmc_host_dev *dev = (struct sdmmc_host_dev *)host->dev;

	return (dwc_mmc_reset(&dev->hc) == DWC_MMC_OK) ? kStatus_Success : kStatus_Fail;
}

/* --- ops: card interface -------------------------------------------------- */

static status_t fdwmmchost_card_detect_init(sdmmchost_t *host, void *cd)
{
	(void)host;
	(void)cd;

	/* The socket is wired straight to the card: detection is the controller's
	 * own slot-status bit, read by fdwmmchost_card_detect_status. */
	return kStatus_Success;
}

static void fdwmmchost_card_set_power(sdmmchost_t *host, bool enable)
{
	(void)host;
	(void)enable;

	/* The rail is always on and the controller's power-enable bit is raised
	 * once at bring-up, so a power-down owes the next bring-up nothing: this
	 * socket never leaves 3.3 V signalling, and there is no 1.8 V level to
	 * restore. */
}

/*
 * The bus-width mapping, and the reason it exists.
 *
 * The imported stack hands over a width ENUMERATION - kSDMMC_BusWdith1Bit,
 * 4Bit and 8Bit are 0, 1 and 2 - while the controller encodes a count of
 * signal lines in its CTYPE field (1, 4 or 8). Passing the enumeration through
 * unchanged puts every four-bit request in the one-bit branch, and the
 * consequence is exactly the misleading kind: the controller talks one bit to
 * a four-bit card, so the direct commands still answer and every block
 * transfer fails its data CRC. This mapping is the fix, and it is the one the
 * board round paid for.
 */
static void fdwmmchost_card_set_bus_width(sdmmchost_t *host, uint32_t dataBusWidth)
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

	dwc_mmc_set_bus_width(&dev->hc, width);

	/* Record what the controller was actually programmed to: the host record
	 * is what the seam's card identity reports, and the width in use is the
	 * one piece of evidence that the mapping above happened. */
	host->currBusWidth = width;
}

static status_t fdwmmchost_card_polling_detect_status(sdmmchost_t *host,
						     uint32_t waitCardStatus,
						     uint32_t timeout)
{
	(void)waitCardStatus;
	(void)timeout;

	/* Fixed socket: the status is read once, never waited on. */
	return dwc_mmc_card_present(&((struct sdmmc_host_dev *)host->dev)->hc) ?
		       kStatus_Success : kStatus_SDMMC_CardDetectFailed;
}

static uint32_t fdwmmchost_card_detect_status(sdmmchost_t *host)
{
	struct sdmmc_host_dev *dev = (struct sdmmc_host_dev *)host->dev;

	return dwc_mmc_card_present(&dev->hc) ? (uint32_t)kSD_Inserted :
						(uint32_t)kSD_Removed;
}

static void fdwmmchost_card_send_active(sdmmchost_t *host)
{
	(void)host;

	/* This controller starts a card with CMD0 and the initialization flag on
	 * the command; it needs no host-side clock priming. */
}

static uint32_t fdwmmchost_card_set_clock(sdmmchost_t *host, uint32_t targetClock)
{
	struct sdmmc_host_dev *dev = (struct sdmmc_host_dev *)host->dev;
	uint32_t achieved = dwc_mmc_set_clock(&dev->hc, targetClock);

	if (achieved == 0U) {
		SDMMC_LOG("dwmmc: clock %u Hz refused\n", (unsigned int)targetClock);
	}

	return achieved;
}

static uint32_t fdwmmchost_signal_line_status(sdmmchost_t *host, uint32_t signalLine)
{
	(void)host;
	(void)signalLine;

	/* No line-status register on this controller; the voltage-switch handshake
	 * that asks is never reached (this board declares no 1.8 V support). */
	return 1U;
}

/* --- ops: not carried ----------------------------------------------------- */

/*
 * Everything below is left unset in the ops table. Each one belongs to a mode
 * this board or this port does not have, and each is guarded by a null check
 * in the stack's dispatcher, so leaving it out refuses the call instead of
 * pretending it worked:
 *   - tuning, DDR, HS400, strobe DLL: UHS-I and MMC features, and this socket
 *     is 3.3 V SDR only;
 *   - switchToVoltage: the 1.8 V rail the switch would drive does not exist;
 *   - forceClockOn: this controller has no clock-gating handshake to force;
 *   - cardEnableInt/cardIntInit: the card interrupt line is not wired.
 */

/* --- the transfer path ---------------------------------------------------- */

static void fdwmmchost_convert_cmd(const sdmmchost_transfer_t *content,
				   struct dwc_mmc_cmd *cmd,
				   struct dwc_mmc_data *data)
{
	const sdmmchost_cmd_t *in = content->command;

	(void)memset(cmd, 0, sizeof(*cmd));
	cmd->index = in->index;
	cmd->arg = in->argument;
	cmd->resp = (in->responseType != kCARD_ResponseTypeNone);
	cmd->resp_long = (in->responseType == kCARD_ResponseTypeR2);
	cmd->send_init = (in->index == (uint32_t)kSDMMC_GoIdleState);

	/*
	 * Check the response CRC wherever one exists. R3 and R4 are the two
	 * response types without a CRC field, so asking for one there would turn
	 * every CMD5 answer into a failure.
	 */
	cmd->resp_crc = cmd->resp && !cmd->resp_long &&
			(in->responseType != kCARD_ResponseTypeR3) &&
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

	/*
	 * What fsl asked for, printed next to what the controller reports it was
	 * handed: the two disagreeing is the difference between a stack that built
	 * the wrong transfer and one whose transfer never started at all.
	 */
	SDMMC_LOG("sdio: data cmd=%u arg=0x%x blksz=%u blkcnt=%u len=%u %s\n",
		  cmd->index, cmd->arg, data->blksz, data->blkcnt, data->len,
		  data->write ? "write" : "read");

	cmd->data = data;
}

static status_t fdwmmchost_transfer(sdmmchost_t *host, sdmmchost_transfer_t *content)
{
	struct sdmmc_host_dev *dev = (struct sdmmc_host_dev *)host->dev;
	struct dwc_mmc_cmd cmd;
	struct dwc_mmc_data data;
	int ret;

	if ((content == NULL) || (content->command == NULL)) {
		return kStatus_InvalidArgument;
	}

	fdwmmchost_convert_cmd(content, &cmd, &data);

	ret = dwc_mmc_transfer(&dev->hc, &cmd);
	if (ret != DWC_MMC_OK) {
		return kStatus_SDMMC_TransferFailed;
	}

	if (cmd.resp) {
		(void)memcpy(content->command->response, cmd.response,
			     sizeof(cmd.response));
	}

	return kStatus_Success;
}

static const sdmmchost_ops_t fdwmmchost_ops = {
	.deinit = fdwmmchost_deinit,
	.reset = fdwmmchost_reset,

	.switchToVoltage = NULL,
	.executeTuning = NULL,
	.enableDDRMode = NULL,
	.enableHS400Mode = NULL,
	.enableStrobeDll = NULL,
	.getSignalLineStatus = fdwmmchost_signal_line_status,
	.convertDataToLittleEndian = NULL,

	.cardDetectInit = fdwmmchost_card_detect_init,
	.cardSetPower = fdwmmchost_card_set_power,
	.cardForceClockOn = NULL,
	.cardEnableInt = NULL,
	.cardIntInit = NULL,
	.cardSetBusWidth = fdwmmchost_card_set_bus_width,
	.cardPollingDetectStatus = fdwmmchost_card_polling_detect_status,
	.cardDetectStatus = fdwmmchost_card_detect_status,
	.cardSendActive = fdwmmchost_card_send_active,
	.cardSetClock = fdwmmchost_card_set_clock,
	.cardIsBusy = NULL,

	.transferFunction = fdwmmchost_transfer,

	.startBoot = NULL,
	.readBootData = NULL,
	.enableBoot = NULL,
};

/* --- the two entry points ------------------------------------------------- */

status_t SDMMCHOST_Init(sdmmchost_t *host)
{
	struct sdmmc_host_dev *dev;
	const struct dwc_mmc_cfg *cfg;

	assert(host != NULL);

	dev = SDMMC_OSAMemoryAllocate(sizeof(*dev));
	if (dev == NULL) {
		return kStatus_OutOfRange;
	}
	(void)memset(dev, 0, sizeof(*dev));

	host->ops = fdwmmchost_ops;
	host->dev = dev;

	cfg = dwc_mmc_board_cfg(host->config.hostId);
	if (cfg == NULL) {
		SDMMC_LOG("dwmmc: no controller for host id %u\n",
			  (unsigned int)host->config.hostId);
		SDMMC_OSAMemoryFree(dev);
		host->dev = NULL;
		return kStatus_InvalidArgument;
	}

	if (dwc_mmc_init(&dev->hc, cfg) != DWC_MMC_OK) {
		SDMMC_LOG("dwmmc: controller bring-up failed\n");
		SDMMC_OSAMemoryFree(dev);
		host->dev = NULL;
		return kStatus_Fail;
	}

	/* The interrupt tier is not carried, so a host that asks for it is refused
	 * rather than quietly run on the polling path. */
	if (host->config.enableIrq) {
		SDMMC_LOG("dwmmc: interrupt transfers are not carried\n");
		dwc_mmc_deinit(&dev->hc);
		SDMMC_OSAMemoryFree(dev);
		host->dev = NULL;
		return kStatus_SDMMC_HostNotSupport;
	}

	return kStatus_Success;
}

status_t FDWMMCHOST_SDIOConfig(sdmmc_sdio_t *sdmmc, sdmmchost_config_t *config)
{
	sdmmchost_t *host = &sdmmc->host;
	sdio_card_t *card = &sdmmc->card;
	sd_detect_card_t *card_cd = &sdmmc->cardDetect;
	sd_io_voltage_t *io_voltage = &sdmmc->ioVoltage;

	assert(sdmmc != NULL);
	assert(config != NULL);

	/* The socket is wired straight to the card: no detect pin, no removable
	 * media, so the type is the host's own slot status and the debounce is
	 * unused. */
	card_cd->type = kSD_DetectCardByHostCD;
	card_cd->cdDebounce_ms = 10U;

	card->usrParam.pwr = NULL;
	card->usrParam.powerOnDelayMS = 0U;
	card->usrParam.powerOffDelayMS = 0U;
	card->usrParam.ioStrength = NULL;

	/*
	 * The card runs at 3.3 V: this socket has no 1.8 V rail and no UHS-I mode,
	 * so the voltage switch is declared unsupported. It has to be a real object
	 * rather than a null pointer, because the stack reads the type before it
	 * asks whether a switch is possible at all.
	 */
	card->usrParam.ioVoltage = io_voltage;
	io_voltage->type = kSD_IOVoltageCtrlNotSupport;
	io_voltage->func = NULL;

	/* The clock the stack may use is this port's ceiling; the stack rounds its
	 * per-mode targets down to it. */
	card->usrParam.maxFreq = config->cardClock;
	card->usrParam.capability = 0U; /* no 8-bit data width on this socket */
	card->noInternalAlign = false;

	host->capability |= (uint32_t)kSDMMCHOST_SupportVoltage3v3 |
			    (uint32_t)kSDMMCHOST_Support4BitDataWidth;
	if (config->cardClock >= SD_CLOCK_50MHZ) {
		host->capability |= (uint32_t)kSDMMCHOST_SupportHighSpeed;
	}

	host->maxBlockCount = (uint32_t)(config->maxTransSize / config->defBlockSize);
	host->maxBlockSize = SDMMCHOST_SUPPORT_MAX_BLOCK_LENGTH;
	host->sourceClock_Hz = 50000000U; /* the CRU source this port selects */

	return kStatus_Success;
}

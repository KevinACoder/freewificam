/*
 * @file   sdmmc_host_firefly_mci.c
 * @brief  The imported SDIO stack's host glue for this board's Phytium MCI
 *         controller, over the ported Linux driver in
 *         board/firefly/drivers/mci.c.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 freewificam contributors
 *
 * Why this file exists at all: the imported stack reaches a controller through
 * one ops table (sdmmchost_ops_t), and this board's SDIO socket is a Phytium
 * MCI block, not the DesignWare MMC block the other glue in this directory
 * drives. The two are the same *family* of register map but not the same
 * programming model, and the differences are the ones that matter: the clock
 * is programmed by a first-divider field plus a sample/drive tap pair rather
 * than a CRU divider chain, the external clock has to be re-latched with a
 * private command after every change, and the data path has two shapes whose
 * submission order is not interchangeable.
 *
 * @design adapter - ref upstream:third-party/fsl_sdmmc host/fdwmmc/
 *         fsl_hc_fdwmmc.c (the ops table and the SDIO configuration function),
 *         with the polling TRANSFER path only and the card-interrupt tier
 *         recorded but not armed (this port's one consumer of RAW_INTS is the
 *         command wait; the 2026-10-29 round on this board showed that a
 *         second consumer of that register is what breaks data commands).
 */

#include "fsl_common.h"
#include "fsl_sdmmc.h"

#include "log.h"

#include "mci.h"
#include "sdmmc_port.h"

/*
 * The controller instance, for the board case's evidence lines: the register
 * fields the Linux GT trace records, the counters and the failure snapshot.
 * NULL until SDMMCHOST_Init has run. One controller, one card, one instance.
 */
static struct mci_host *mcihost_instance;

/* The glue's per-host state. One controller, one card. */
struct sdmmc_mci_dev {
	struct mci_host hc;
	/* the stack's card-interrupt record; recorded here so a later round can
	 * arm DAT1 without touching the bring-up */
	void *card_int_rec;
};

/* --- ops: lifecycle ------------------------------------------------------- */

static void mcihost_deinit(sdmmchost_t *host)
{
	struct sdmmc_mci_dev *dev = (struct sdmmc_mci_dev *)host->dev;

	if (dev == NULL) {
		return;
	}

	mci_deinit(&dev->hc);
	mcihost_instance = NULL;
	SDMMC_OSAMemoryFree(dev);
	host->dev = NULL;
}

static status_t mcihost_reset(sdmmchost_t *host)
{
	struct sdmmc_mci_dev *dev = (struct sdmmc_mci_dev *)host->dev;

	if (dev == NULL) {
		return kStatus_Fail;
	}

	return (mci_reset(&dev->hc) == MCI_OK) ? kStatus_Success : kStatus_Fail;
}

/* --- ops: card interface -------------------------------------------------- */

static status_t mcihost_card_detect_init(sdmmchost_t *host, void *cd)
{
	(void)host;
	(void)cd;

	/* mmc1 is wired straight to the card and the DTS says non-removable:
	 * the controller's own detect bit is the only status there is. */
	return kStatus_Success;
}

static void mcihost_card_set_power(sdmmchost_t *host, bool enable)
{
	struct sdmmc_mci_dev *dev = (struct sdmmc_mci_dev *)host->dev;

	if (dev != NULL) {
		mci_power(&dev->hc, enable);
	}
}

/*
 * The bus-width mapping, and the reason it exists here too: the stack hands
 * over an ENUMERATION (kSDMMC_BusWdith1Bit/4Bit/8Bit = 0/1/2) while the
 * controller's CTYPE field encodes a count of signal lines. Passing the
 * enumeration through would put every four-bit request in the one-bit branch -
 * direct commands still answer and every block transfer fails its data CRC.
 */
static void mcihost_card_set_bus_width(sdmmchost_t *host, uint32_t dataBusWidth)
{
	struct sdmmc_mci_dev *dev = (struct sdmmc_mci_dev *)host->dev;
	unsigned int width;

	switch (dataBusWidth) {
	case kSDMMC_BusWdith8Bit:
		width = 8u;
		break;
	case kSDMMC_BusWdith4Bit:
		width = 4u;
		break;
	case kSDMMC_BusWdith1Bit:
	default:
		width = 1u;
		break;
	}

	mci_set_bus_width(&dev->hc, width);
	host->currBusWidth = width;
}

static status_t mcihost_card_polling_detect_status(sdmmchost_t *host,
						   uint32_t waitCardStatus,
						   uint32_t timeout)
{
	(void)waitCardStatus;
	(void)timeout;

	return mci_card_present(&((struct sdmmc_mci_dev *)host->dev)->hc) ?
		       kStatus_Success : kStatus_SDMMC_CardDetectFailed;
}

static uint32_t mcihost_card_detect_status(sdmmchost_t *host)
{
	struct sdmmc_mci_dev *dev = (struct sdmmc_mci_dev *)host->dev;

	return mci_card_present(&dev->hc) ? (uint32_t)kSD_Inserted :
					    (uint32_t)kSD_Removed;
}

/*
 * The card-side reset sequence, and the step whose absence made every command
 * on this socket time out.
 *
 * Linux runs two things between powering the socket up and its first CMD0
 * (mmc_rescan_try_freq in drivers/mmc/core/core.c):
 *
 *   1. mmc_hw_reset_for_init() -> firefly_mci_hw_reset(), whose tail is a
 *      5 us low pulse on the controller's card-reset line followed by 300 us;
 *   2. sdio_reset() -> a CMD52 to the card's IO_ABORT register writing 0x08
 *      (RES), then CMD0.
 *
 * The imported stack does neither: it powers up and sends CMD0. On a card
 * that has already been through a reset - a warm reboot, a previous image's
 * bring-up - that is enough; on this board's part after the loader has touched
 * the socket it is not, and CMD5 then times out at every clock (which is what
 * this port measured: CMD0 "succeeds" - it has no response - and every CMD5
 * afterwards comes back as RTO with resp0=0).
 *
 * The stack calls this hook from SDIO_ProbeBusVoltage with the identification
 * clock already running and one-bit width set, which is exactly where the two
 * steps belong. The CMD52 is issued through the controller driver directly
 * rather than the stack's card API: at this point the stack has no card
 * context worth speaking of, and the command is a fixed write.
 */
static void mcihost_card_send_active(sdmmchost_t *host)
{
	struct sdmmc_mci_dev *dev = (struct sdmmc_mci_dev *)host->dev;
	struct mci_cmd cmd;
	int ret;

	mci_reset_card(&dev->hc);

	(void)memset(&cmd, 0, sizeof(cmd));
	cmd.index = 52u;
	/* write, function 0, IO_ABORT(0x06) = 0x08 (RES) */
	cmd.arg = (1u << 31) | (0x06u << 9) | 0x08u;
	cmd.resp_class = MCI_RESP_SHORT_CRC;

	ret = mci_transfer(&dev->hc, &cmd);
	LOG_ANCHOR("mci: card reset pulse + CMD52 abort rc=%d resp0=%08lx\n",
		   ret, (unsigned long)cmd.response[0]);
}

static uint32_t mcihost_card_set_clock(sdmmchost_t *host, uint32_t targetClock)
{
	struct sdmmc_mci_dev *dev = (struct sdmmc_mci_dev *)host->dev;
	uint32_t achieved = mci_set_clock(&dev->hc, targetClock);

	if (achieved == 0u) {
		SDMMC_LOG("mci: clock %u Hz refused\n", (unsigned int)targetClock);
	}

	return achieved;
}

static uint32_t mcihost_signal_line_status(sdmmchost_t *host, uint32_t signalLine)
{
	(void)host;
	(void)signalLine;

	/* No line-status register on this controller, and the voltage-switch
	 * handshake that would ask is never reached: this socket is 3.3 V. */
	return 1u;
}

/* --- ops: the card interrupt (DAT1), recorded but not armed ---------------- */

static status_t mcihost_card_int_init(sdmmchost_t *host, void *sdioInt)
{
	struct sdmmc_mci_dev *dev = (struct sdmmc_mci_dev *)host->dev;

	dev->card_int_rec = sdioInt;
	host->cardInt = sdioInt;

	/* Deliberately not armed: this port polls, and the one consumer of
	 * RAW_INTS has to stay one consumer. Returning success keeps the
	 * stack's bring-up shape identical to a host that supports DAT1. */
	return kStatus_Success;
}

static void mcihost_card_enable_int(sdmmchost_t *host, bool enable)
{
	(void)host;
	(void)enable;
}

void FDWMMCHOST_CardIntEnable(sdmmchost_t *host, bool enable)
{
	if (host == NULL) {
		return;
	}

	mcihost_card_enable_int(host, enable);
}

/* --- the transfer path ---------------------------------------------------- */

static uint32_t mcihost_resp_class(sdmmc_card_response_type_t type)
{
	switch (type) {
	case kCARD_ResponseTypeR1:
	case kCARD_ResponseTypeR1b:
	case kCARD_ResponseTypeR5:
	case kCARD_ResponseTypeR5b:
	case kCARD_ResponseTypeR6:
	case kCARD_ResponseTypeR7:
		return MCI_RESP_SHORT_CRC;
	case kCARD_ResponseTypeR2:
		return MCI_RESP_LONG_CRC;
	case kCARD_ResponseTypeR3:
	case kCARD_ResponseTypeR4:
		return MCI_RESP_SHORT;
	case kCARD_ResponseTypeNone:
	default:
		return MCI_RESP_NONE;
	}
}

static status_t mcihost_transfer(sdmmchost_t *host, sdmmchost_transfer_t *content)
{
	struct sdmmc_mci_dev *dev = (struct sdmmc_mci_dev *)host->dev;
	const sdmmchost_cmd_t *in;
	struct mci_cmd cmd;
	struct mci_data data;
	int ret;

	if ((content == NULL) || (content->command == NULL)) {
		return kStatus_InvalidArgument;
	}

	in = content->command;
	(void)memset(&cmd, 0, sizeof(cmd));
	cmd.index = in->index;
	cmd.arg = in->argument;
	cmd.resp_class = mcihost_resp_class(in->responseType);
	cmd.data = NULL;

	if (content->data != NULL) {
		(void)memset(&data, 0, sizeof(data));
		if (content->data->rxData != NULL) {
			data.buf = (uint8_t *)content->data->rxData;
			data.write = false;
		} else {
			data.buf = (uint8_t *)(uintptr_t)content->data->txData;
			data.write = true;
		}
		data.blksz = (uint32_t)content->data->blockSize;
		data.blkcnt = content->data->blockCount;
		data.len = data.blksz * data.blkcnt;
		cmd.data = &data;
	}

	ret = mci_transfer(&dev->hc, &cmd);
	if (ret != MCI_OK) {
		return kStatus_SDMMC_TransferFailed;
	}

	if (cmd.resp_class != MCI_RESP_NONE) {
		/* the stack's command record is handed to us by the card layer as
		 * a const pointer, but its response slots are the output of the
		 * transfer: this is the one place the port writes through it */
		(void)memcpy((void *)(uintptr_t)in->response, cmd.response,
			     sizeof(cmd.response));
	}

	return kStatus_Success;
}

static const sdmmchost_ops_t mcihost_ops = {
	.deinit = mcihost_deinit,
	.reset = mcihost_reset,

	.switchToVoltage = NULL,
	.executeTuning = NULL,
	.enableDDRMode = NULL,
	.enableHS400Mode = NULL,
	.enableStrobeDll = NULL,
	.getSignalLineStatus = mcihost_signal_line_status,
	.convertDataToLittleEndian = NULL,

	.cardDetectInit = mcihost_card_detect_init,
	.cardSetPower = mcihost_card_set_power,
	.cardForceClockOn = NULL,
	.cardEnableInt = mcihost_card_enable_int,
	.cardIntInit = mcihost_card_int_init,
	.cardSetBusWidth = mcihost_card_set_bus_width,
	.cardPollingDetectStatus = mcihost_card_polling_detect_status,
	.cardDetectStatus = mcihost_card_detect_status,
	.cardSendActive = mcihost_card_send_active,
	.cardSetClock = mcihost_card_set_clock,
	.cardIsBusy = NULL,

	.transferFunction = mcihost_transfer,

	.startBoot = NULL,
	.readBootData = NULL,
	.enableBoot = NULL,
};

/* --- the evidence hook ---------------------------------------------------- */

struct mci_host *sdmmc_port_mci_host(void)
{
	return mcihost_instance;
}

/* --- the two entry points ------------------------------------------------- */

status_t SDMMCHOST_Init(sdmmchost_t *host)
{
	struct sdmmc_mci_dev *dev;
	const struct mci_cfg *cfg;

	assert(host != NULL);

	dev = SDMMC_OSAMemoryAllocate(sizeof(*dev));
	if (dev == NULL) {
		return kStatus_OutOfRange;
	}
	(void)memset(dev, 0, sizeof(*dev));

	host->ops = mcihost_ops;
	host->dev = dev;

	cfg = mci_board_cfg(host->config.hostId);
	if (cfg == NULL) {
		SDMMC_LOG("mci: no controller for host id %u\n",
			  (unsigned int)host->config.hostId);
		SDMMC_OSAMemoryFree(dev);
		host->dev = NULL;
		return kStatus_InvalidArgument;
	}

	mcihost_instance = &dev->hc;

	if (mci_init(&dev->hc, cfg) != MCI_OK) {
		SDMMC_LOG("mci: controller bring-up failed\n");
		SDMMC_OSAMemoryFree(dev);
		host->dev = NULL;
		return kStatus_Fail;
	}

	/* Polled port: a host that asks for interrupt-driven completion
	 * transfers is refused rather than quietly run on the polling path. */
	if (host->config.enableIrq) {
		SDMMC_LOG("mci: interrupt transfers are not carried\n");
		mci_deinit(&dev->hc);
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

	/* Fixed socket: non-removable, so the detect type is the host's own
	 * status and the debounce is unused. */
	card_cd->type = kSD_DetectCardByHostCD;
	card_cd->cdDebounce_ms = 10U;

	card->usrParam.pwr = NULL;
	card->usrParam.powerOnDelayMS = 0U;
	card->usrParam.powerOffDelayMS = 0U;
	card->usrParam.ioStrength = NULL;

	/* 3.3 V only. It has to be a real object rather than a null pointer:
	 * the stack reads the type before it asks whether a switch is possible
	 * at all, and a null here drags a 3.3 V board into a CMD11 timeout. */
	card->usrParam.ioVoltage = io_voltage;
	io_voltage->type = kSD_IOVoltageCtrlNotSupport;
	io_voltage->func = NULL;

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
	/* the SoC feeds both MCI instances from sysclk_1200mhz */
	host->sourceClock_Hz = (uint32_t)MCI_SRC_CLK_HZ;

	return kStatus_Success;
}

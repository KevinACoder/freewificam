/*
 * @file   sdmmc_host_stm32f4_sdio.c
 * @brief  The imported SDIO stack's host glue for the STM32F429 SDIO
 *         peripheral: the ops table over the ST HAL LL SDMMC driver.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 freewificam contributors
 *
 * The card service (sdmmc_card.c) speaks the imported stack's one ops table.
 * This file maps that table onto the ST LL driver carried in
 * third-party/stm32f4xx-hal: SDIO_Init, SDIO_SendCommand, SDIO_ConfigData,
 * the FIFO accessors and the controller's status flags.  The transfer path is
 * polled PIO, the same shape the vendor SD example uses; DMA is a later arm.
 *
 * Board facts live in sdio_board.c (pins, PB13/AP6181, controller reset).  The
 * socket has no card-detect pin, so the host reports the card present and the
 * command path is the detector.
 */

/*
 * Two libraries name their card-level init SDIO_Init: the imported ST LL
 * driver (SDIO_TypeDef *) and the imported fsl card service (sdio_card_t *).
 * The host adapter needs both headers, so the ST one is compiled under a
 * private name for this image; the fsl name stays the stack's.
 */
#define SDIO_Init STM32F4_SDIO_LL_Init
#include "stm32f4xx_hal.h"
#undef SDIO_Init

#include "fsl_common.h"
#include "fsl_sdmmc.h"

#include "sdio_board.h"
#include "sdmmc_port.h"

#define F4SDIO_CMD_SPIN		10000000U
#define F4SDIO_DATA_SPIN	10000000U
#define F4SDIO_MAX_DIV		118U

struct sdmmc_host_dev {
	uint32_t bus_width;	/* signal lines: 1, 4 or 8 */
	uint32_t div;		/* CLKCR CLKDIV in use */
	uint32_t clock_hz;	/* achieved SDIO_CK */
	bool powered;
	void *card_int_rec;
};

static status_t f4sdio_program_clock(struct sdmmc_host_dev *dev, uint32_t div)
{
	SDIO_InitTypeDef init;
	uint32_t width;

	init.ClockEdge = SDIO_CLOCK_EDGE_RISING;
	init.ClockBypass = SDIO_CLOCK_BYPASS_DISABLE;
	init.ClockPowerSave = SDIO_CLOCK_POWER_SAVE_DISABLE;
	init.BusWide = (dev->bus_width == 4U) ? SDIO_BUS_WIDE_4B :
		       (dev->bus_width == 8U) ? SDIO_BUS_WIDE_8B :
						SDIO_BUS_WIDE_1B;
	init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
	init.ClockDiv = div;
	(void)STM32F4_SDIO_LL_Init(SDIO, init);
	__SDIO_ENABLE();

	width = dev->bus_width;
	dev->clock_hz = 48000000U / (2U + div);
	return (width == 4U || width == 1U) ? kStatus_Success : kStatus_Fail;
}

static uint32_t f4sdio_div_for(uint32_t target)
{
	uint32_t div;

	if (target >= 24000000U)
		return 0U;
	if (target <= 400000U)
		return F4SDIO_MAX_DIV;

	div = (48000000U / target);
	if (div < 2U)
		div = 2U;
	div -= 2U;
	if (div > F4SDIO_MAX_DIV)
		div = F4SDIO_MAX_DIV;
	return div;
}

/* --- ops: lifecycle ------------------------------------------------------- */

static void f4sdio_deinit(sdmmchost_t *host)
{
	struct sdmmc_host_dev *dev = (struct sdmmc_host_dev *)host->dev;

	if (dev == NULL)
		return;

	SDIO_PowerState_OFF(SDIO);
	SDIO->CLKCR = 0U;
	SDIO->DCTRL = 0U;
	SDIO->ICR = 0xFFFFFFFFUL;
	sdio_board_irq_enable(false);
	SDMMC_OSAMemoryFree(dev);
	host->dev = NULL;
}

static status_t f4sdio_reset(sdmmchost_t *host)
{
	struct sdmmc_host_dev *dev = (struct sdmmc_host_dev *)host->dev;

	if (dev == NULL)
		return kStatus_Fail;

	RCC->APB2RSTR |= RCC_APB2RSTR_SDIORST;
	RCC->APB2RSTR &= ~RCC_APB2RSTR_SDIORST;
	sdio_board_init();

	dev->bus_width = 1U;
	dev->div = 0U;
	dev->clock_hz = 0U;
	dev->powered = false;
	return kStatus_Success;
}

/* --- ops: card interface -------------------------------------------------- */

static status_t f4sdio_card_detect_init(sdmmchost_t *host, void *cd)
{
	(void)host;
	(void)cd;
	return kStatus_Success;
}

static void f4sdio_card_set_power(sdmmchost_t *host, bool enable)
{
	struct sdmmc_host_dev *dev = (struct sdmmc_host_dev *)host->dev;

	if (dev == NULL)
		return;

	if (enable) {
		(void)SDIO_PowerState_ON(SDIO);
	} else {
		(void)SDIO_PowerState_OFF(SDIO);
	}
	dev->powered = enable;
}

static void f4sdio_card_force_clock_on(sdmmchost_t *host, bool enable)
{
	(void)host;
	(void)enable;
}

static void f4sdio_card_set_bus_width(sdmmchost_t *host, uint32_t dataBusWidth)
{
	struct sdmmc_host_dev *dev = (struct sdmmc_host_dev *)host->dev;
	uint32_t width;

	if (dev == NULL)
		return;

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

	dev->bus_width = width;
	if (dev->div != 0U || dev->clock_hz != 0U)
		(void)f4sdio_program_clock(dev, dev->div);
	host->currBusWidth = width;
}

static status_t f4sdio_card_polling_detect_status(sdmmchost_t *host,
						  uint32_t waitCardStatus,
						  uint32_t timeout)
{
	(void)host;
	(void)waitCardStatus;
	(void)timeout;

	/* Fixed socket: no card-detect pin.  The command path reports a missing
	 * card as a command timeout. */
	return kStatus_Success;
}

static uint32_t f4sdio_card_detect_status(sdmmchost_t *host)
{
	(void)host;
	return (uint32_t)kSD_Inserted;
}

static void f4sdio_card_send_active(sdmmchost_t *host)
{
	(void)host;
}

static uint32_t f4sdio_card_set_clock(sdmmchost_t *host, uint32_t targetClock)
{
	struct sdmmc_host_dev *dev = (struct sdmmc_host_dev *)host->dev;
	uint32_t div;

	if (dev == NULL)
		return 0U;

	div = f4sdio_div_for(targetClock);
	dev->div = div;
	(void)f4sdio_program_clock(dev, div);
	host->currClockFreq = dev->clock_hz;
	return dev->clock_hz;
}

static uint32_t f4sdio_signal_line_status(sdmmchost_t *host, uint32_t signalLine)
{
	(void)host;
	(void)signalLine;
	return 1U;
}

/* --- ops: the card interrupt (DAT1) --------------------------------------- */

static struct sdmmc_host_dev *s_f4sdio_irq_dev;

static status_t f4sdio_card_int_init(sdmmchost_t *host, void *sdioInt)
{
	struct sdmmc_host_dev *dev = (struct sdmmc_host_dev *)host->dev;

	if (dev == NULL)
		return kStatus_Fail;

	dev->card_int_rec = sdioInt;
	host->cardInt = sdioInt;
	s_f4sdio_irq_dev = dev;
	return kStatus_Success;
}

static void f4sdio_card_enable_int(sdmmchost_t *host, bool enable)
{
	(void)host;
	if (enable) {
		/* Arm the card's DAT1 line and the controller's SDIO interrupt. */
		SDIO->ICR = SDIO_FLAG_SDIOIT;
		SDIO->MASK |= SDIO_IT_SDIOIT;
		sdio_board_irq_enable(true);
	} else {
		SDIO->MASK &= ~SDIO_IT_SDIOIT;
		sdio_board_irq_enable(false);
	}
}

void SDIO_IRQHandler(void)
{
	uint32_t sta = SDIO->STA;

	if ((sta & SDIO_FLAG_SDIOIT) == 0U)
		return;

	SDIO->ICR = SDIO_FLAG_SDIOIT;
	if ((s_f4sdio_irq_dev != NULL) &&
	    (s_f4sdio_irq_dev->card_int_rec != NULL)) {
		const sdio_card_int_t *rec =
			(const sdio_card_int_t *)s_f4sdio_irq_dev->card_int_rec;

		if (rec->cardInterrupt != NULL)
			rec->cardInterrupt(rec->userData);
	}
}

void FDWMMCHOST_CardIntEnable(sdmmchost_t *host, bool enable)
{
	f4sdio_card_enable_int(host, enable);
}

/* --- the transfer path ---------------------------------------------------- */

static uint32_t f4sdio_block_size(size_t blockSize)
{
	switch (blockSize) {
	case 1U:    return SDIO_DATABLOCK_SIZE_1B;
	case 2U:    return SDIO_DATABLOCK_SIZE_2B;
	case 4U:    return SDIO_DATABLOCK_SIZE_4B;
	case 8U:    return SDIO_DATABLOCK_SIZE_8B;
	case 16U:   return SDIO_DATABLOCK_SIZE_16B;
	case 32U:   return SDIO_DATABLOCK_SIZE_32B;
	case 64U:   return SDIO_DATABLOCK_SIZE_64B;
	case 128U:  return SDIO_DATABLOCK_SIZE_128B;
	case 256U:  return SDIO_DATABLOCK_SIZE_256B;
	case 512U:  return SDIO_DATABLOCK_SIZE_512B;
	case 1024U: return SDIO_DATABLOCK_SIZE_1024B;
	case 2048U: return SDIO_DATABLOCK_SIZE_2048B;
	case 4096U: return SDIO_DATABLOCK_SIZE_4096B;
	case 8192U: return SDIO_DATABLOCK_SIZE_8192B;
	case 16384U:return SDIO_DATABLOCK_SIZE_16384B;
	default:    return 0xFFFFFFFFUL;
	}
}

static status_t f4sdio_wait_command(uint32_t want, bool no_crc,
				    uint32_t *sta_out)
{
	uint32_t spin;
	uint32_t sta = 0U;

	for (spin = 0U; spin < F4SDIO_CMD_SPIN; spin++) {
		sta = SDIO->STA;
		if (no_crc) {
			/*
			 * R4 (CMD5's OCR) has no CRC on the wire: the
			 * controller reports CCRCFAIL as a matter of course.
			 * The ST reference ignores that bit and waits for the
			 * command to leave the active state; the card's presence
			 * is told by the response, not by a CRC we cannot get.
			 */
			if (((sta & SDIO_FLAG_CMDACT) == 0U) &&
			    ((sta & (SDIO_FLAG_CMDREND | SDIO_FLAG_CCRCFAIL |
				     SDIO_FLAG_CTIMEOUT)) != 0U)) {
				if ((sta & SDIO_FLAG_CTIMEOUT) != 0U)
					break;
				return kStatus_Success;
			}
		} else {
			if ((sta & want) != 0U)
				return kStatus_Success;
			if ((sta & (SDIO_FLAG_CCRCFAIL |
				    SDIO_FLAG_CTIMEOUT)) != 0U)
				break;
		}
	}
	if (sta_out != NULL)
		*sta_out = sta;
	SDIO->ICR = SDIO_FLAG_CCRCFAIL | SDIO_FLAG_CTIMEOUT |
		    SDIO_FLAG_CMDREND | SDIO_FLAG_CMDSENT;
	return kStatus_SDMMC_TransferFailed;
}

static status_t f4sdio_send_command(sdmmchost_t *host,
				    sdmmchost_cmd_t *command)
{
	SDIO_CmdInitTypeDef cmd;
	uint32_t response = SDIO_RESPONSE_NO;
	uint32_t want = SDIO_FLAG_CMDSENT;
	uint32_t sta = 0U;
	bool no_crc = (command->responseType == kCARD_ResponseTypeR4) ||
		      (command->index == 5U);

	(void)host;

	switch (command->responseType) {
	case kCARD_ResponseTypeNone:
		response = SDIO_RESPONSE_NO;
		want = SDIO_FLAG_CMDSENT;
		break;
	case kCARD_ResponseTypeR2:
		response = SDIO_RESPONSE_LONG;
		want = SDIO_FLAG_CMDREND;
		break;
	default:
		response = SDIO_RESPONSE_SHORT;
		want = SDIO_FLAG_CMDREND;
		break;
	}

	SDIO->ICR = SDIO_FLAG_CCRCFAIL | SDIO_FLAG_CTIMEOUT |
		    SDIO_FLAG_CMDREND | SDIO_FLAG_CMDSENT;

	cmd.Argument = command->argument;
	cmd.CmdIndex = (uint32_t)command->index;
	cmd.Response = response;
	cmd.WaitForInterrupt = SDIO_WAIT_NO;
	cmd.CPSM = SDIO_CPSM_ENABLE;
	(void)SDIO_SendCommand(SDIO, &cmd);

	if (f4sdio_wait_command(want, no_crc, &sta) != kStatus_Success) {
		/* The first failing command names the enumeration step. */
		SDMMC_LOGE("f4sdio", "cmd=%u arg=0x%08x resp=%u sta=0x%08x\n",
			   (unsigned)command->index,
			   (unsigned)command->argument,
			   (unsigned)command->responseType,
			   (unsigned)sta);
		return kStatus_SDMMC_TransferFailed;
	}

	if (response != SDIO_RESPONSE_NO) {
		command->response[0] = SDIO_GetResponse(SDIO, SDIO_RESP1);
		if ((command->responseErrorFlags != 0U) &&
		    ((command->response[0] & command->responseErrorFlags) != 0U)) {
			SDMMC_LOGE("f4sdio", "r5 cmd=%u arg=0x%08x resp=0x%08x mask=0x%08x\n",
				   (unsigned)command->index,
				   (unsigned)command->argument,
				   (unsigned)command->response[0],
				   (unsigned)command->responseErrorFlags);
			return kStatus_SDMMC_TransferFailed;
		}
		if (response == SDIO_RESPONSE_LONG) {
			command->response[1] = SDIO_GetResponse(SDIO, SDIO_RESP2);
			command->response[2] = SDIO_GetResponse(SDIO, SDIO_RESP3);
			command->response[3] = SDIO_GetResponse(SDIO, SDIO_RESP4);
		}
	}

	/* R1b holds DAT0 busy after the response; the ST LL has no wait-pend
	 * path, so give the card a short settling window before the next
	 * command.  The SDIO card's own CMD7 busy is well below it. */
	if (command->responseType == kCARD_ResponseTypeR1b)
		SDMMC_OSADelay(2U);

	return kStatus_Success;
}

/*
 * A byte-mode read carries the exact number of bytes waiting in the card's
 * FIFO (472, 212, ...), which DBLOCKSIZE cannot express.  SDIO multibyte mode
 * can: with DTMODE=1 and SDIOEN=1, DLEN is the byte count and no block size is
 * encoded.  Writes keep block framing, which is the path the register writes
 * already use.
 */
static bool f4sdio_byte_read(const sdmmchost_data_t *data)
{
	return (data != NULL) && (data->rxData != NULL) &&
	       (f4sdio_block_size(data->blockSize) == 0xFFFFFFFFUL);
}

static status_t f4sdio_data_config(const sdmmchost_data_t *data)
{
	SDIO_DataInitTypeDef cfg;
	uint32_t blockSize;
	bool byte_read = f4sdio_byte_read(data);

	blockSize = f4sdio_block_size(data->blockSize);
	if (byte_read) {
		blockSize = SDIO_DATABLOCK_SIZE_1B;
	} else if (blockSize == 0xFFFFFFFFUL) {
		return kStatus_SDMMC_TransferFailed;
	}

	cfg.DataTimeOut = 0xFFFFFFFFUL;
	cfg.DataLength = (uint32_t)(data->blockSize * data->blockCount);
	cfg.DataBlockSize = blockSize;
	cfg.TransferDir = (data->rxData != NULL) ?
			  SDIO_TRANSFER_DIR_TO_SDIO :
			  SDIO_TRANSFER_DIR_TO_CARD;
	cfg.TransferMode = byte_read ? SDIO_TRANSFER_MODE_STREAM :
				       SDIO_TRANSFER_MODE_BLOCK;
	cfg.DPSM = SDIO_DPSM_ENABLE;

	/* SDIOEN selects what DTMODE means (SDIO multibyte vs MMC stream).  It
	 * must be in place before SDIO_ConfigData writes DTEN; the LL call's
	 * read-modify-write leaves this bit alone.  Only the byte-read path
	 * touches it: an extra DCTRL write on every block transfer runs into
	 * the peripheral's write-delay rule and breaks the next command. */
	if (byte_read)
		SDIO->DCTRL |= SDIO_DCTRL_SDIOEN;

	(void)SDIO_ConfigData(SDIO, &cfg);
	return kStatus_Success;
}

static status_t f4sdio_wait_data_end(void)
{
	uint32_t spin;
	uint32_t sta;

	for (spin = 0U; spin < F4SDIO_DATA_SPIN; spin++) {
		sta = SDIO->STA;
		/*
		 * A single-block CMD53 (the CCCR/CIS reads) ends with DBCKEND;
		 * DATAEND is the multi-block/whole-transfer flag.  Waiting for
		 * DATAEND alone turns every 4-byte read into a 10M-spin failure.
		 */
		if ((sta & (SDIO_FLAG_DATAEND | SDIO_FLAG_DBCKEND)) != 0U) {
			SDIO->ICR = SDIO_FLAG_DATAEND | SDIO_FLAG_DBCKEND;
			return kStatus_Success;
		}
		if ((sta & (SDIO_FLAG_RXOVERR | SDIO_FLAG_TXUNDERR |
			    SDIO_FLAG_DCRCFAIL | SDIO_FLAG_DTIMEOUT |
			    SDIO_FLAG_STBITERR)) != 0U)
			break;
	}
	SDIO->ICR = SDIO_FLAG_DATAEND | SDIO_FLAG_DBCKEND |
		    SDIO_FLAG_RXOVERR | SDIO_FLAG_TXUNDERR |
		    SDIO_FLAG_DCRCFAIL | SDIO_FLAG_DTIMEOUT |
		    SDIO_FLAG_STBITERR;
	return kStatus_SDMMC_TransferFailed;
}

static status_t f4sdio_transfer_data(const sdmmchost_data_t *data)
{
	uint32_t remaining = (uint32_t)(data->blockSize * data->blockCount);
	uint8_t *rx = (uint8_t *)data->rxData;
	const uint8_t *tx = (const uint8_t *)data->txData;
	uint32_t spin;

	if (data->rxData != NULL) {
		while (remaining != 0U) {
			if ((SDIO->STA & (SDIO_FLAG_RXOVERR |
					  SDIO_FLAG_DCRCFAIL |
					  SDIO_FLAG_DTIMEOUT |
					  SDIO_FLAG_STBITERR)) != 0U)
				break;
			if ((SDIO->STA & SDIO_FLAG_RXDAVL) != 0U) {
				uint32_t word = SDIO_ReadFIFO(SDIO);
				uint32_t n = (remaining < 4U) ? remaining : 4U;

				(void)memcpy(rx, &word, n);
				rx += n;
				remaining -= n;
			}
		}
	} else if (data->txData != NULL) {
		while (remaining != 0U) {
			if ((SDIO->STA & (SDIO_FLAG_TXUNDERR |
					  SDIO_FLAG_DCRCFAIL |
					  SDIO_FLAG_DTIMEOUT)) != 0U)
				break;
			/*
			 * TXFIFOHE ("half empty") is the write-side credit;
			 * TXDAVL ("data available") is the read-side view of
			 * the same FIFO and stalls the writer once it drains.
			 */
			if ((SDIO->STA & SDIO_FLAG_TXFIFOHE) != 0U) {
				uint32_t word = 0U;
				uint32_t n = (remaining < 4U) ? remaining : 4U;

				(void)memcpy(&word, tx, n);
				tx += n;
				remaining -= n;
				(void)SDIO_WriteFIFO(SDIO, &word);
			}
		}
	}

	if (remaining != 0U)
		return f4sdio_wait_data_end();

	/* Drain the FIFO tail before declaring the transfer done; the flag
	 * reports the end of the data path, not the end of the reads. */
	if (data->rxData != NULL) {
		for (spin = 0U; spin < F4SDIO_DATA_SPIN; spin++) {
			if ((SDIO->STA & SDIO_FLAG_RXDAVL) == 0U)
				break;
			(void)SDIO_ReadFIFO(SDIO);
		}
	}

	return f4sdio_wait_data_end();
}

static status_t f4sdio_transfer(sdmmchost_t *host, sdmmchost_transfer_t *content)
{
	status_t status;

	if ((content == NULL) || (content->command == NULL))
		return kStatus_InvalidArgument;

	SDIO->ICR = 0xFFFFFFFFUL;

	if (content->data != NULL) {
		status = f4sdio_data_config(content->data);
		if (status != kStatus_Success)
			return status;
	}

	status = f4sdio_send_command(host, content->command);
	if (status != kStatus_Success)
		return status;

	if (content->data != NULL) {
		bool byte_read = f4sdio_byte_read(content->data);

		status = f4sdio_transfer_data(content->data);
		if (status != kStatus_Success) {
			if (byte_read)
				SDIO->DCTRL &= ~SDIO_DCTRL_SDIOEN;
			SDMMC_LOGE("f4sdio",
				   "data cmd=%u len=%u dir=%s sta=0x%08x",
				   (unsigned)content->command->index,
				   (unsigned)(content->data->blockSize *
					      content->data->blockCount),
				   (content->data->rxData != NULL) ? "rx" : "tx",
				   (unsigned)SDIO->STA);
			return status;
		}
		if (byte_read)
			SDIO->DCTRL &= ~SDIO_DCTRL_SDIOEN;
	}

	return kStatus_Success;
}

static const sdmmchost_ops_t f4sdio_ops = {
	.deinit = f4sdio_deinit,
	.reset = f4sdio_reset,

	.switchToVoltage = NULL,
	.executeTuning = NULL,
	.enableDDRMode = NULL,
	.enableHS400Mode = NULL,
	.enableStrobeDll = NULL,
	.getSignalLineStatus = f4sdio_signal_line_status,
	.convertDataToLittleEndian = NULL,

	.cardDetectInit = f4sdio_card_detect_init,
	.cardSetPower = f4sdio_card_set_power,
	.cardForceClockOn = f4sdio_card_force_clock_on,
	.cardEnableInt = f4sdio_card_enable_int,
	.cardIntInit = f4sdio_card_int_init,
	.cardSetBusWidth = f4sdio_card_set_bus_width,
	.cardPollingDetectStatus = f4sdio_card_polling_detect_status,
	.cardDetectStatus = f4sdio_card_detect_status,
	.cardSendActive = f4sdio_card_send_active,
	.cardSetClock = f4sdio_card_set_clock,
	.cardIsBusy = NULL,

	.transferFunction = f4sdio_transfer,

	.startBoot = NULL,
	.readBootData = NULL,
	.enableBoot = NULL,
};

/* --- the entry points ----------------------------------------------------- */

status_t SDMMCHOST_Init(sdmmchost_t *host)
{
	struct sdmmc_host_dev *dev;

	assert(host != NULL);

	if (host->config.enableIrq) {
		SDMMC_LOG("f4sdio: interrupt transfers are not carried\n");
		return kStatus_SDMMC_HostNotSupport;
	}

	dev = SDMMC_OSAMemoryAllocate(sizeof(*dev));
	if (dev == NULL)
		return kStatus_OutOfRange;
	(void)memset(dev, 0, sizeof(*dev));

	host->ops = f4sdio_ops;
	host->dev = dev;
	s_f4sdio_irq_dev = dev;

	sdio_board_init();
	dev->bus_width = 1U;
	dev->div = 0U;
	dev->clock_hz = 0U;
	dev->powered = false;

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

	card_cd->type = kSD_DetectCardByHostCD;
	card_cd->cdDebounce_ms = 10U;

	card->usrParam.pwr = NULL;
	card->usrParam.powerOnDelayMS = 0U;
	card->usrParam.powerOffDelayMS = 0U;
	card->usrParam.ioStrength = NULL;
	card->usrParam.ioVoltage = io_voltage;
	io_voltage->type = kSD_IOVoltageCtrlNotSupport;
	io_voltage->func = NULL;

	card->usrParam.maxFreq = config->cardClock;
	card->usrParam.capability = 0U;
	card->noInternalAlign = false;

	host->capability = (uint32_t)kSDMMCHOST_SupportVoltage3v3 |
			   (uint32_t)kSDMMCHOST_Support4BitDataWidth;
	host->maxBlockCount = (uint32_t)(config->maxTransSize / config->defBlockSize);
	host->maxBlockSize = SDMMCHOST_SUPPORT_MAX_BLOCK_LENGTH;
	host->sourceClock_Hz = sdio_board_source_hz();

	return kStatus_Success;
}

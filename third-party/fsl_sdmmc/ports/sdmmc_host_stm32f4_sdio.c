/*
 * @file   sdmmc_host_stm32f4_sdio.c
 * @brief  The imported SDIO/SD stack's host glue for the STM32F429 SDIO
 *         peripheral: the ops table over the ST HAL LL SDMMC driver.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 freewificam contributors
 *
 * Two card services speak the imported stack's one ops table through this
 * file: the SDIO instance (sdmmc_card.c, the 8189FTV line) and the SD memory
 * card instance (sdmmc_sd_card.c, the F429 TF-slot line).  This file maps the
 * table onto the ST LL driver carried in third-party/stm32f4xx-hal: SDIO_Init,
 * SDIO_SendCommand, SDIO_ConfigData, the FIFO accessors and the controller's
 * status flags.  The transfer path is polled PIO, the same shape the vendor SD
 * example uses; DMA is a later arm.
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
#include "sdmmc_host_stm32f4_sdio_dbg.h"

/* The DWT cycle counter is the fine time base of this core; the platform's
 * clock init enables it before the first stamped console line. */
static inline uint32_t f4_cyccnt(void)
{
	return *(volatile uint32_t *)0xE0001004UL;
}

static inline uint32_t f4_cyccnt_to_us(uint32_t cycles)
{
	uint32_t per_us = SystemCoreClock / 1000000U;

	return (per_us == 0U) ? 0U : cycles / per_us;
}

static struct f4sdio_dbg s_f4dbg;

void f4sdio_dbg_get(struct f4sdio_dbg *out)
{
	if (out != NULL)
		*out = s_f4dbg;
}

#define F4SDIO_CMD_SPIN		10000000U
#define F4SDIO_DATA_SPIN	10000000U
/*
 * The byte-mode write's settle bound (CONFIG_F4SDIO_TX_SETTLE): how long the
 * data path may take to report it is done, and the floor the run-out path
 * waits instead.  The core runs at 168 MHz, so 33,600 cycles is 200 us and
 * 3,000 is about 18 us - both far above one four-bit word's eight clocks
 * (333 ns at 24 MHz), so neither can be mistaken for the transfer's own pace.
 */
#define F4SDIO_TX_SETTLE_CYCLES		33600U
#define F4SDIO_TX_SETTLE_TAIL_CYCLES	3000U
/*
 * Consecutive empty-FIFO polls that end an SDIO multibyte read.  The card
 * stops when it has sent what it has, not when DLEN is reached, so a finished
 * transfer and a stuck one look the same until the FIFO has been quiet long
 * enough to mean it.
 */
/* Consecutive empty-FIFO polls that end an SDIO multibyte read: the card has
 * stopped sending, so the transfer is over whatever DLEN said. */
#define F4SDIO_RX_IDLE		200000U
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
	/*
	 * Hardware flow control stays off: it uses DAT1 as the busy signal, and
	 * the 8189FTV does not drive it - enabling it hangs the card in the
	 * middle of its own initialisation (reproduced on two cold boots, the
	 * console stops right after the capability dump).  Keeping up with the
	 * card is the data loop's job instead, see f4sdio_transfer_data().
	 */
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

/*
 * The card-interrupt line, armed by the engine's pump_irq_set() (the engine's
 * card_irq_mode op): the one runtime state this glue keeps for it.
 *
 * The reference manual's ordering is what the whole section implements: the
 * card's in-band interrupt is detected on DAT1 only while DCTRL[11] (SDIOEN) is
 * set; a detected interrupt sets STA.SDIOIT, and SDIO_MASK.SDIOITIE gates the
 * request; serving it is "disable the request, clear the card's source, clear
 * SDIOIT, re-enable the request".  The service step is a bus transfer, which
 * belongs to the pump thread (the ISR must not touch the bus), so the ISR only
 * stands the request down and wakes the pump; the pump's re-arm is the step
 * that clears SDIOIT and re-enables the request.
 */
static bool s_f4sdio_irq_armed;

/* DAT1 is PC9 (sdio_board.c); the card holds it low while it wants service,
 * and the socket's only pull-up is the pin's internal one. */
static bool f4sdio_dat1_low(void)
{
	return (GPIOC->IDR & (1UL << 9U)) == 0U;
}

/* The short critical section the MASK read-modify-writes need: a thread that
 * re-enables the request can otherwise lose the ISR's mask between its read and
 * its write, leaving a level-held line enabled while the pump is not looking. */
static uint32_t f4sdio_crit_enter(void)
{
	uint32_t primask = __get_PRIMASK();

	__disable_irq();
	return primask;
}

static void f4sdio_crit_exit(uint32_t primask)
{
	__set_PRIMASK(primask);
}

static void f4sdio_sample_state(void)
{
	s_f4dbg.mask = SDIO->MASK;
	s_f4dbg.dctrl = SDIO->DCTRL;
	s_f4dbg.sta = SDIO->STA;
	s_f4dbg.dat1_level = f4sdio_dat1_low() ? 0U : 1U;
	s_f4dbg.irq_armed = s_f4sdio_irq_armed ? 1U : 0U;
}

/*
 * The line is level held and the detector samples it during the interrupt
 * periods only, so a set flag can be cleared by an unrelated transfer without
 * the level ever re-latching it.  This is the software restatement of "the card
 * is still asking", and it only fires in exactly that lost case: armed, DAT1
 * low, flag clear.  The pump's next cycle then services the card; if the
 * handler clears the card's source, DAT1 goes high and this stops.
 */
static void f4sdio_soft_poke(void)
{
	const sdio_card_int_t *rec;

	if (!s_f4sdio_irq_armed)
		return;
	if ((s_f4sdio_irq_dev == NULL) ||
	    (s_f4sdio_irq_dev->card_int_rec == NULL))
		return;
	rec = (const sdio_card_int_t *)s_f4sdio_irq_dev->card_int_rec;
	if (rec->cardInterrupt == NULL)
		return;
	s_f4dbg.self_poke++;
	rec->cardInterrupt(rec->userData);
}

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
	uint32_t primask;
	bool first;

	(void)host;

	if (!enable) {
		primask = f4sdio_crit_enter();
		SDIO->MASK &= ~SDIO_IT_SDIOIT;
		s_f4sdio_irq_armed = false;
		f4sdio_crit_exit(primask);
		sdio_board_irq_enable(false);
		f4sdio_sample_state();
		return;
	}

	first = !s_f4sdio_irq_armed;
	primask = f4sdio_crit_enter();
	/*
	 * SDIOEN is what makes the peripheral sample DAT1 at all.  The byte-mode
	 * read path used to clear it at the end of every FIFO read, so even an
	 * armed mask would have listened only between two of those transfers.
	 */
	SDIO->DCTRL |= SDIO_DCTRL_SDIOEN;
	if (first || !f4sdio_dat1_low()) {
		/* A stale flag from before the line was armed would fire as soon as
		 * the request is enabled; clear it only when the card is not
		 * holding the line, which is the manual's order. */
		SDIO->ICR = SDIO_FLAG_SDIOIT;
	}
	SDIO->MASK |= SDIO_IT_SDIOIT;
	s_f4sdio_irq_armed = true;
	f4sdio_crit_exit(primask);

	sdio_board_irq_enable(true);

	/* The card may already be holding the line with no flag to re-latch. */
	if (f4sdio_dat1_low() && (SDIO->STA & SDIO_FLAG_SDIOIT) == 0U)
		f4sdio_soft_poke();
	f4sdio_sample_state();
}

void SDIO_IRQHandler(void)
{
	const sdio_card_int_t *rec;
	uint32_t sta = SDIO->STA;
	uint32_t primask;

	s_f4dbg.irq_entries++;
	s_f4dbg.sta = sta;
	s_f4dbg.dat1_level = f4sdio_dat1_low() ? 0U : 1U;

	if ((sta & SDIO_FLAG_SDIOIT) == 0U) {
		s_f4dbg.irq_spurious++;
		return;
	}

	s_f4dbg.irq_sdioit++;
	if (s_f4dbg.dat1_level != 0U)
		s_f4dbg.dat1_high++;

	/*
	 * Stand the request down.  Clearing SDIOIT here would be premature: the
	 * card's own source is cleared later, by the handler on the pump thread,
	 * and the manual puts the flag clear after that.  A line that is still
	 * held when the pump re-arms re-asserts from the flag it kept.
	 */
	primask = f4sdio_crit_enter();
	SDIO->MASK &= ~SDIO_IT_SDIOIT;
	f4sdio_crit_exit(primask);

	if ((s_f4sdio_irq_dev == NULL) ||
	    (s_f4sdio_irq_dev->card_int_rec == NULL))
		return;
	rec = (const sdio_card_int_t *)s_f4sdio_irq_dev->card_int_rec;
	if (rec->cardInterrupt != NULL)
		rec->cardInterrupt(rec->userData);
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
		      (command->responseType == kCARD_ResponseTypeR3) ||
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
		if (response == SDIO_RESPONSE_LONG) {
			/*
			 * R2 (CID/CSD) is 128 bits.  The imported stack's decoders
			 * number the four words from the least significant end:
			 * SD_DecodeCid reads the manufacturer from response[3] and
			 * SD_DecodeCsd reads CSD[127:126] from rawCsd[3].  The ST
			 * controller's RESP1 is the most significant word (the ST
			 * HAL's own GetCardCID reads CID[0]=RESP1 the same way), so
			 * fill the stack's order, not the controller's.  Without
			 * this the CID/CSD decode into a plausible-looking but
			 * wrong identity and a 448 KB geometry for a real card.
			 */
			command->response[0] = SDIO_GetResponse(SDIO, SDIO_RESP4);
			command->response[1] = SDIO_GetResponse(SDIO, SDIO_RESP3);
			command->response[2] = SDIO_GetResponse(SDIO, SDIO_RESP2);
			command->response[3] = SDIO_GetResponse(SDIO, SDIO_RESP1);
		} else {
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
#define F4SDIO_CMD53_BLOCK_MODE  (1UL << 27)

/*
 * One framing for every SDIO byte-mode transfer, in both directions:
 * SDIOEN + DTMODE.  A byte-mode CMD53 carries an arbitrary byte count that
 * DBLOCKSIZE cannot express, and the device ends the data phase itself
 * instead of the host waiting for the CRC status token that a memory-card
 * write gets and an I/O device never sends.
 *
 * Block framing here is not merely a size problem: an 84-byte write framed as
 * 21 four-byte blocks sends 21 data CRCs and then waits for 21 CRC status
 * tokens that never arrive, which leaves TXACT up and every later transfer
 * failing.
 *
 * The framing follows the CMD53 argument's block bit, not the byte count: a
 * 4-byte register write is byte mode too, and its count is a power of two, so
 * the count cannot tell the two apart.
 */
static bool f4sdio_byte_mode(const sdmmchost_cmd_t *cmd)
{
	return (cmd != NULL) && (cmd->index == 53U) &&
	       ((cmd->argument & F4SDIO_CMD53_BLOCK_MODE) == 0U);
}

static bool f4sdio_byte_read(const sdmmchost_data_t *data)
{
	return (data != NULL) && (data->rxData != NULL) &&
	       (f4sdio_block_size(data->blockSize) == 0xFFFFFFFFUL);
}

static uint32_t s_f4sdio_tx_dctrl;
static bool s_f4sdio_tx_deferred;

static status_t f4sdio_data_config(const sdmmchost_data_t *data, bool byte_mode,
				   bool stream_rx)
{
	SDIO_DataInitTypeDef cfg;
	uint32_t blockSize;
	/*
	 * Reads keep the classification that has been working: the power-of-two
	 * counts (4, 8, 512) go through block framing and the FIFO counts that
	 * DBLOCKSIZE cannot express (84, 212, 408, 472) through SDIO
	 * multibyte.  Forcing every byte-mode CMD53 into multibyte looks more
	 * principled and breaks the 4- and 8-byte register reads on this
	 * peripheral, so the rule stays as measured.
	 */
	/*
	 * Reads: block framing for the counts DBLOCKSIZE can express and SDIO
	 * multibyte for the FIFO counts it cannot.  Forcing every byte-mode
	 * CMD53 into multibyte looks more principled and fails the 4- and
	 * 8-byte register reads on this peripheral (measured: the data arrives
	 * but the transfer reports failure), so the classification stays on
	 * the byte count for reads.
	 */
	bool byte_read = f4sdio_byte_read(data);

	s_f4sdio_tx_deferred = false;

	/*
	 * An SDIO-multibyte write first: its length is DLEN, it never encodes a
	 * block size, so a count DBLOCKSIZE cannot express is exactly the case
	 * it exists for.  Doing the block-size lookup before this branch made
	 * every transmit whose length is not a power of two fail in silence -
	 * the card write path's byte count is the frame length (a probe request
	 * is 84/100 bytes, an auth frame about 40), and the block lookup
	 * answered 0xFFFFFFFF, which the caller saw as -EIO with no line on the
	 * console: the card never received a frame, so nothing was transmitted.
	 */
	if (byte_mode && (data->txData != NULL)) {
		uint32_t total = (uint32_t)(data->blockSize * data->blockCount);

		SDIO->DTIMER = 0xFFFFFFFFUL;
		SDIO->DLEN = total;
		s_f4sdio_tx_dctrl = SDIO_DATABLOCK_SIZE_1B | SDIO_DCTRL_DTEN |
				    SDIO_DCTRL_SDIOEN | SDIO_DCTRL_DTMODE;
		/*
		 * Arm the data path before the command, not after its response.
		 * The card's SDIO data phase follows the response, so a data
		 * path enabled afterwards starts with an empty FIFO and the
		 * peripheral reports TXUNDERR a couple of words in (measured:
		 * 8 of 84 bytes written, then the transfer stops and the card
		 * keeps a truncated frame).  The reference manual and the Linux
		 * hosts arm the path first for SDIO multibyte writes; the
		 * response wait then does not clock the data phase.
		 */
		s_f4sdio_tx_deferred = true;
		return kStatus_Success;
	}

	blockSize = f4sdio_block_size(data->blockSize);
	if (byte_read) {
		blockSize = SDIO_DATABLOCK_SIZE_1B;
	} else if (blockSize == 0xFFFFFFFFUL) {
		return kStatus_SDMMC_TransferFailed;
	}

	/* An SDIO multibyte read ends where the card stops sending, which can
	 * be a word short of the requested count; a data timeout turns that
	 * into an event instead of a wait that never ends.  It is per data
	 * token, so a card that is still sending never trips it. */
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

/* What the last multibyte read left over and how it ended; the FIFO record
 * needs both, and they are local to f4sdio_transfer_data(). */
static uint32_t s_f4sdio_rx_total;
static uint32_t s_f4sdio_rx_left;
static uint32_t s_f4sdio_rx_end;

/* The transmit FIFO holds 32 words (128 bytes). */
#define F4SDIO_TX_FIFO_WORDS	32u

static uint32_t s_f4sdio_tx_left;	/* bytes still unpushed when the loop left */
static uint32_t s_f4sdio_tx_sta;	/* STA at that instant */
static uint32_t s_f4sdio_tx_dcount;	/* DCOUNT at that instant */
/* The completion instant of the last byte-mode write: what the criterion saw
 * and whether the data path was still on the wire.  Read by f4sdio_txq_record. */
static uint32_t s_f4sdio_tx_sta_drop;
static uint32_t s_f4sdio_tx_dcnt_drop;
static uint32_t s_f4sdio_tx_txact_drop;
/* Which condition ended the byte-mode write's completion wait.  A write that
 * ended on an error flag is a write the seam still reports as success, so the
 * two have to be told apart by something, and this is it. */
#define F4SDIO_TX_DROP_NONE	0u
#define F4SDIO_TX_DROP_DONE	1u
#define F4SDIO_TX_DROP_ERR	2u
#define F4SDIO_TX_DROP_SPIN	3u
static uint32_t s_f4sdio_tx_drop_end = F4SDIO_TX_DROP_NONE;
/* The data path right after the frame was loaded into the FIFO, and the
 * control word the deferred arming actually left there. */
static uint32_t s_f4sdio_tx_dctrl_arm;
static uint32_t s_f4sdio_tx_sta_arm;
static uint32_t s_f4sdio_tx_sta_burst;
static uint32_t s_f4sdio_tx_dcnt_burst;
static uint32_t s_f4sdio_tx_burst_cycles;	/* the fill's own duration */

static status_t f4sdio_transfer_data(const sdmmchost_data_t *data, bool byte_mode,
				     bool stream_rx)
{
	uint32_t remaining = (uint32_t)(data->blockSize * data->blockCount);
	uint8_t *rx = (uint8_t *)data->rxData;
	const uint8_t *tx = (const uint8_t *)data->txData;
	uint32_t spin = 0U;

	if (data->rxData != NULL) {
		/*
		 * An SDIO multibyte read ends where the card says it ends, not
		 * where the host asked it to: the driver requests RND4 of the
		 * length the RX FIFO reported, and the card sends only what it
		 * has - 470 of the 472 asked for, and then stops.  DLEN is
		 * never reached, so the DPSM sits in RXACT for ever and every
		 * frame is lost.  An idle FIFO after real progress is the end
		 * of the transfer.
		 */
		uint32_t idle = 0U;
		bool got = false;
		uint32_t req0 = remaining;
		uint32_t end = F4SDIO_RX_END_OTHER;
		uint32_t t0 = f4_cyccnt();

		/* Bounded: a data path that delivers nothing at all must fail,
		 * not poll for ever - that is what turns a bad transfer into a
		 * firmware that stops reporting. */
		for (; (remaining != 0U) && (spin < F4SDIO_DATA_SPIN); spin++) {
			uint32_t sta = SDIO->STA;

			if ((sta & (SDIO_FLAG_RXOVERR | SDIO_FLAG_DCRCFAIL |
				    SDIO_FLAG_DTIMEOUT | SDIO_FLAG_STBITERR)) != 0U) {
				end = F4SDIO_RX_END_ERR;
				break;
			}
			if ((sta & (SDIO_FLAG_RXDAVL | SDIO_FLAG_RXFIFOHF)) == 0U) {
				if (stream_rx && got && (++idle > F4SDIO_RX_IDLE)) {
					end = F4SDIO_RX_END_IDLE;
					(void)memset(rx, 0, remaining);
					rx += remaining;
					remaining = 0U;
					break;
				}
				continue;
			}
			idle = 0U;
			/*
			 * Drain the FIFO at the card's rate.  The card writes at 12 MB/s
			 * on a 24 MHz four-bit bus, so the loop may spend about ten
			 * cycles per word: one FIFO read and one word store.  Anything
			 * heavier here (a call to memcpy per word, for instance) falls
			 * behind, the 32-word FIFO fills and the transfer ends in
			 * RXOVERR - which is what every long receive read was doing.
			 */
			do {
				uint32_t word = SDIO->FIFO;

				if (remaining >= 4U) {
					*(uint32_t *)(void *)rx = word;
					rx += 4U;
					remaining -= 4U;
				} else {
					uint8_t tail[4];
					uint32_t i;

					(void)memcpy(tail, &word, 4U);
					for (i = 0U; i < remaining; i++)
						rx[i] = tail[i];
					rx += remaining;
					remaining = 0U;
				}
				got = true;
			} while ((remaining != 0U) &&
				 ((SDIO->STA & SDIO_FLAG_RXDAVL) != 0U));
		}
		if (end == F4SDIO_RX_END_OTHER) {
			if (remaining == 0U)
				end = F4SDIO_RX_END_COUNT;
			else
				end = F4SDIO_RX_END_NOBODY;
		}
		s_f4sdio_rx_total = req0;
		s_f4sdio_rx_left = remaining;
		s_f4sdio_rx_end = end;
		{
			struct f4sdio_rx_rec *rec =
			    &s_f4dbg.rec[s_f4dbg.rec_head % F4SDIO_RX_REC_MAX];

			rec->req = req0;
			rec->got = req0 - remaining;
			rec->end = end;
			rec->us = f4_cyccnt_to_us(f4_cyccnt() - t0);
			rec->sta = SDIO->STA;
			rec->dctrl = SDIO->DCTRL;
			rec->dcount = SDIO->DCOUNT;
			s_f4dbg.rec_head++;
			s_f4dbg.rx_xfers++;
			s_f4dbg.rx_bytes += rec->got;
			switch (end) {
			case F4SDIO_RX_END_COUNT:
				s_f4dbg.rx_count_end++;
				break;
			case F4SDIO_RX_END_IDLE:
				s_f4dbg.rx_idle_end++;
				break;
			case F4SDIO_RX_END_NOBODY:
				s_f4dbg.rx_zero++;
				break;
			default:
				s_f4dbg.rx_err++;
				break;
			}
		}
	} else if (data->txData != NULL) {
		uint32_t tx_primask = 0U;
		bool tx_crit = false;

		s_f4sdio_tx_left = remaining;
		s_f4sdio_tx_sta = 0U;
		s_f4sdio_tx_dcount = 0U;
		/* Fresh captures for this write: the record printed for it must not
		 * repeat the previous write's completion word. */
		s_f4sdio_tx_sta_drop = 0U;
		s_f4sdio_tx_dcnt_drop = 0U;
		s_f4sdio_tx_txact_drop = 0U;
		s_f4sdio_tx_drop_end = F4SDIO_TX_DROP_NONE;

		/*
		 * SDIO multibyte mode cannot pause: the DPSM keeps sending while
		 * the byte counter is nonzero and sets TXUNDERR the moment the
		 * FIFO runs empty (the reference manual's stream-mode text).  A
		 * per-word loop that also reads the status register between
		 * words runs at about the card's own rate at 24 MHz, so the
		 * FIFO drains under it after two words - measured: 8 of 84
		 * bytes written.  Fill the whole 32-word FIFO first in one
		 * straight-line burst (every management frame fits: a probe
		 * request is 84/100 bytes, an auth frame about 40) and only
		 * fall into the status-checked loop for a longer frame.
		 *
		 * A memory-card block write is the same problem over a longer
		 * window: 512 bytes take about 43 us, and an RTOS tick or another
		 * ISR inside that window drains the FIFO faster than this loop
		 * refills it (measured: 448 of 512 bytes, TXUNDERR at DCOUNT 80).
		 * The block write therefore runs with the CPU's interrupts held
		 * off, prefill included, until the last word is in the FIFO.
		 */
		if (!byte_mode) {
			tx_primask = f4sdio_crit_enter();
			tx_crit = true;
		}
		{
			uint32_t burst = remaining;
			uint32_t t0 = f4_cyccnt();

			if (burst > F4SDIO_TX_FIFO_WORDS * 4u)
				burst = F4SDIO_TX_FIFO_WORDS * 4u;
#if defined(CONFIG_F4SDIO_TX_FASTFILL) && CONFIG_F4SDIO_TX_FASTFILL
			/*
			 * The card drains the FIFO one word per eight SDIO clocks -
			 * 333 ns at 24 MHz four-bit - and the FIFO is 32 words, so the
			 * host has 10.6 us to put a frame in.  A per-word call chain
			 * (memcpy plus the imported FIFO helper) costs more than a
			 * word's worth of card time each, so the data path underruns
			 * after the first word however many are queued: measured, an
			 * 84-byte write ended with TXUNDERR and DCOUNT=80.  The fast
			 * path is the same bytes with the copy and the store inlined,
			 * about ten cycles a word.
			 */
			if ((((uintptr_t)tx & 3u) == 0u) && (burst >= 4u)) {
				uint32_t words = burst >> 2;

				if (words > F4SDIO_TX_FIFO_WORDS)
					words = F4SDIO_TX_FIFO_WORDS;
				while (words-- != 0u) {
					SDIO->FIFO = *(const uint32_t *)(const void *)tx;
					tx += 4;
					remaining -= 4u;
				}
				burst = remaining;
				if (burst > F4SDIO_TX_FIFO_WORDS * 4u)
					burst = F4SDIO_TX_FIFO_WORDS * 4u;
			}
#endif
			while (burst != 0U) {
				uint32_t word = 0U;
				uint32_t n = (burst < 4U) ? burst : 4U;

				(void)memcpy(&word, tx, n);
				tx += n;
				remaining -= n;
				burst -= n;
				(void)SDIO_WriteFIFO(SDIO, &word);
			}
			s_f4sdio_tx_burst_cycles = (uint32_t)(f4_cyccnt() - t0);
		}
		/* What the data path looked like with the frame loaded: an empty
		 * FIFO here means the words never landed (the FIFO is written only
		 * while the path is armed for transmit), and a count that has not
		 * moved means the DPSM never started. */
		s_f4sdio_tx_dctrl_arm = SDIO->DCTRL;
		s_f4sdio_tx_sta_burst = SDIO->STA;
		s_f4sdio_tx_dcnt_burst = SDIO->DCOUNT;

		if (!byte_mode) {
			/*
			 * Memory-card block writes: keep the FIFO full rather than
			 * waiting for the half-empty flag.  The flag-based loop
			 * underran near the end of the block (measured: 440/448 of
			 * 512 bytes, TXUNDERR while 64 bytes were still unwritten);
			 * filling every free slot with a direct store and holding
			 * interrupts off gives the card no window to starve.
			 */
			while (remaining != 0U) {
				if ((SDIO->STA & (SDIO_FLAG_TXUNDERR |
						  SDIO_FLAG_DCRCFAIL |
						  SDIO_FLAG_DTIMEOUT)) != 0U) {
					s_f4sdio_tx_left = remaining;
					s_f4sdio_tx_sta = SDIO->STA;
					s_f4sdio_tx_dcount = SDIO->DCOUNT;
					break;
				}
				while ((remaining != 0U) &&
				       ((SDIO->STA & SDIO_FLAG_TXFIFOF) == 0U)) {
					if ((((uintptr_t)tx & 3u) == 0u)) {
						SDIO->FIFO =
						    *(const uint32_t *)(const void *)tx;
					} else {
						uint32_t word = 0U;

						(void)memcpy(&word, tx, 4U);
						SDIO->FIFO = word;
					}
					tx += 4U;
					remaining -= 4U;
				}
			}
		} else {
			while (remaining != 0U) {
				if ((SDIO->STA & (SDIO_FLAG_TXUNDERR |
						  SDIO_FLAG_DCRCFAIL |
						  SDIO_FLAG_DTIMEOUT)) != 0U) {
					s_f4sdio_tx_left = remaining;
					s_f4sdio_tx_sta = SDIO->STA;
					s_f4sdio_tx_dcount = SDIO->DCOUNT;
					break;
				}
				/*
				 * Write credit is "the FIFO is not full", not "the FIFO
				 * is at least half empty": with 21 words to push into a
				 * 32-word FIFO, waiting for the half-empty flag stops a
				 * third of the way and the card sees a short transfer.
				 */
				if ((SDIO->STA & SDIO_FLAG_TXFIFOF) == 0U) {
					uint32_t word = 0U;
					uint32_t n = (remaining < 4U) ? remaining : 4U;

					(void)memcpy(&word, tx, n);
					tx += n;
					remaining -= n;
					(void)SDIO_WriteFIFO(SDIO, &word);
				}
			}
		}
		if (tx_crit) {
			f4sdio_crit_exit(tx_primask);
		}
	}

	if ((remaining == 0U) && byte_mode && (data->txData != NULL)) {
		/*
		 * A byte-mode write has no CRC status token, so DATAEND never
		 * comes; what says the device has every byte is the byte
		 * counter reaching zero with the FIFO empty.  Dropping DTEN any
		 * earlier stops the clock with bytes still in the FIFO and the
		 * device silently keeps the truncated write.
		 *
		 * The counter is not the last word, though: DCOUNT decrements
		 * when the data path loads a word out of the FIFO, so
		 * `DCOUNT==0 && !TXDAVL` means "the FIFO is empty", not "the
		 * last word and its CRC have left the pins".  STA.TXACT is the
		 * peripheral's own answer to the second question, and with a
		 * 24 MHz four-bit clock one word is eight clock cycles wide -
		 * clearing DTEN in that window cuts the frame's tail.
		 */
		s_f4sdio_tx_sta_drop = 0U;
		s_f4sdio_tx_dcnt_drop = 0U;
		s_f4sdio_tx_txact_drop = 0U;
		s_f4sdio_tx_drop_end = F4SDIO_TX_DROP_SPIN;
		for (spin = 0U; spin < F4SDIO_DATA_SPIN; spin++) {
			uint32_t sta = SDIO->STA;

			if ((sta & (SDIO_FLAG_TXUNDERR |
				    SDIO_FLAG_DCRCFAIL |
				    SDIO_FLAG_DTIMEOUT)) != 0U) {
				s_f4sdio_tx_drop_end = F4SDIO_TX_DROP_ERR;
				s_f4dbg.tx_drop_err++;
			} else if ((SDIO->DCOUNT == 0U) &&
				   ((sta & SDIO_FLAG_TXDAVL) == 0U)) {
				s_f4sdio_tx_drop_end = F4SDIO_TX_DROP_DONE;
			} else {
				continue;
			}
			s_f4sdio_tx_sta_drop = sta;
			s_f4sdio_tx_dcnt_drop = SDIO->DCOUNT;
			s_f4sdio_tx_txact_drop =
			    ((sta & SDIO_FLAG_TXACT) != 0U) ? 1U : 0U;
			break;
		}
		if (s_f4sdio_tx_txact_drop != 0U)
			s_f4dbg.tx_txact_set++;
#if defined(CONFIG_F4SDIO_TX_SETTLE) && CONFIG_F4SDIO_TX_SETTLE
		/*
		 * Diagnostic arm: let the data path finish before the clock
		 * stops.  Bounded in the core's own cycle counter so a DPSM
		 * that never leaves Send cannot hold the claim; the run-out
		 * path still gives the last word its eight clock cycles.
		 */
		{
			uint32_t t0 = f4_cyccnt();
			uint32_t budget = F4SDIO_TX_SETTLE_CYCLES;
			bool timed_out = false;

			while ((SDIO->STA & SDIO_FLAG_TXACT) != 0U) {
				if ((uint32_t)(f4_cyccnt() - t0) > budget) {
					timed_out = true;
					break;
				}
			}
			if (timed_out) {
				volatile uint32_t d;

				s_f4dbg.tx_settle_timeout++;
				for (d = 0U; d < F4SDIO_TX_SETTLE_TAIL_CYCLES; d++)
					;
			}
		}
#endif
		SDIO->DCTRL &= ~SDIO_DCTRL_DTEN;
		SDIO->ICR = SDIO_FLAG_DATAEND | SDIO_FLAG_DBCKEND;
		return kStatus_Success;
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

	/*
	 * An SDIO multibyte read has no block boundary to end on, so the
	 * peripheral raises neither DBCKEND nor DATAEND for it; what says the
	 * read is complete is the byte count.  Reporting the drained transfer
	 * as a failure here is what kept the driver's RX FIFO reads from ever
	 * delivering a frame - the bytes were all there, only the verdict was
	 * wrong.
	 */
	if (stream_rx && (data->rxData != NULL)) {
		/*
		 * The counter is the verdict, but the data path is still
		 * running (RXACT up): a multibyte read has no block boundary,
		 * so nothing stops the DPSM when the count runs out.  Dropping
		 * DTEN returns it to Idle, which is the state the next command
		 * expects to find the bus in.
		 */
		SDIO->DCTRL &= ~SDIO_DCTRL_DTEN;
		SDIO->ICR = SDIO_FLAG_DATAEND | SDIO_FLAG_DBCKEND;
		return kStatus_Success;
	}

	return f4sdio_wait_data_end();
}

/*
 * A data transfer masks the card interrupt for its whole length.  The device
 * raises it on DAT1 whenever it wants attention, and serving it means issuing
 * CMD52 - a command, in the middle of a running CMD53.  The peripheral then
 * abandons the data path, which is exactly the short stop counts the log shows.
 * The line is level-based, so it is still there when the bus is free.
 */
static bool s_f4sdio_irq_masked;

static void f4sdio_irq_mask(void)
{
	uint32_t primask;

	primask = f4sdio_crit_enter();
	if ((SDIO->MASK & SDIO_IT_SDIOIT) != 0U) {
		s_f4sdio_irq_masked = true;
		SDIO->MASK &= ~SDIO_IT_SDIOIT;
		s_f4dbg.irq_masked_xfer++;
	}
	f4sdio_crit_exit(primask);
}

static void f4sdio_irq_unmask(void)
{
	uint32_t primask;

	if (!s_f4sdio_irq_masked)
		return;

	s_f4sdio_irq_masked = false;
	primask = f4sdio_crit_enter();
	/*
	 * Restore the request but leave STA.SDIOIT alone.  The flag may be a card
	 * event that arrived while the line was down, and the manual clears it
	 * only after the card's own source has been served - which is the pump's
	 * re-arm step, not a transfer's tail.
	 */
	SDIO->MASK |= SDIO_IT_SDIOIT;
	f4sdio_crit_exit(primask);

	if (f4sdio_dat1_low() && (SDIO->STA & SDIO_FLAG_SDIOIT) == 0U)
		f4sdio_soft_poke();
	f4sdio_sample_state();
}

/* CMD53 argument fields (SDIO simplified spec 5.3): the function number is the
 * top nibble, the block bit is bit 27, the increment-address bit 26, and bits
 * 25:9 hold the register address.  The vendor driver addresses its four data
 * ports by the address's own top three bits (device 7 is the RX FIFO port), not
 * by the function nibble - every transfer here is function 1.
 */
#define F4SDIO_ARG_FUNC(arg)	((unsigned)((arg) >> 28))
#define F4SDIO_ARG_BLOCK(arg)	(((arg) >> 27) & 1UL)
#define F4SDIO_ARG_ADDR(arg)	(((arg) >> 9) & 0x1FFFFUL)
#define F4SDIO_ARG_DEV(arg)	((unsigned)(F4SDIO_ARG_ADDR(arg) >> 13))

/* The RX-FIFO port: a read there is the card handing over received frames, and
 * its bytes are the only direct witness of "did the card ever receive
 * anything". */
#define F4SDIO_RX_FIFO_DEV	7U

static void f4sdio_fifo_record(const sdmmchost_data_t *data, status_t status)
{
	const uint8_t *p = (const uint8_t *)data->rxData;
	struct f4sdio_fifo_rec *rec;
	uint32_t req = (uint32_t)(data->blockSize * data->blockCount);
	uint32_t i;
	uint32_t first = req;
	uint32_t copied = 0U;

	if ((s_f4sdio_rx_total == req) && (s_f4sdio_rx_left <= req))
		copied = req - s_f4sdio_rx_left;

	rec = &s_f4dbg.fifo[s_f4dbg.fifo_rec_head % F4SDIO_FIFO_REC_MAX];
	s_f4dbg.fifo_rec_head++;
	rec->req = req;
	rec->copied = copied;
	rec->end = s_f4sdio_rx_end;
	rec->status = (uint32_t)status;
	rec->got = copied;
	rec->zero = 1U;
	for (i = 0U; i < req; i++) {
		if (p[i] != 0U) {
			rec->zero = 0U;
			first = i;
			break;
		}
	}
	rec->first = first;
	for (i = 0U; i < F4SDIO_FIFO_HEAD; i++)
		rec->head[i] = (i < req) ? p[i] : 0U;
	s_f4dbg.fifo_reads++;
	if (status == kStatus_Success)
		s_f4dbg.fifo_bytes += req;
	if (rec->zero != 0U)
		s_f4dbg.fifo_zero++;
	/*
	 * An RX-FIFO read is the best in-band sample of the DAT1 line: the card
	 * has just handed over frames, so "it is asserting" and "it is not" are
	 * both meaningful here, and no extra bus access is involved.
	 */
	f4sdio_sample_state();
}

/* A write to one of the three transmit FIFO ports is a frame the driver handed
 * the card; the other writes are register traffic.  The record keeps the
 * vendor's transmit descriptor and the frame's first bytes, which is what a
 * later "the card never transmitted" finding is read from. */
#define F4SDIO_TX_HIQ_DEV	4U
#define F4SDIO_TX_MIQ_DEV	5U
#define F4SDIO_TX_LOQ_DEV	6U

static void f4sdio_txq_record(const sdmmchost_data_t *data, status_t status,
			      unsigned int dev)
{
	struct f4sdio_tx_rec *rec;
	const uint8_t *p = (const uint8_t *)data->txData;
	uint32_t len = (uint32_t)(data->blockSize * data->blockCount);
	uint32_t i;

	rec = &s_f4dbg.txq[s_f4dbg.txq_rec_head % F4SDIO_TX_REC_MAX];
	s_f4dbg.txq_rec_head++;
	rec->dev = dev;
	rec->len = len;
	rec->status = (uint32_t)status;
	rec->sta_drop = s_f4sdio_tx_sta_drop;
	rec->dcnt_drop = s_f4sdio_tx_dcnt_drop;
	rec->txact_drop = s_f4sdio_tx_txact_drop;
	rec->drop_end = s_f4sdio_tx_drop_end;
	rec->sta_arm = s_f4sdio_tx_sta_arm;
	rec->dctrl_arm = s_f4sdio_tx_dctrl_arm;
	rec->sta_burst = s_f4sdio_tx_sta_burst;
	rec->dcnt_burst = s_f4sdio_tx_dcnt_burst;
	rec->burst_cycles = s_f4sdio_tx_burst_cycles;
	for (i = 0U; i < F4SDIO_TX_HEAD; i++)
		rec->head[i] = (i < len) ? p[i] : 0U;
}

static status_t f4sdio_transfer(sdmmchost_t *host, sdmmchost_transfer_t *content)
{
	status_t status;
	bool byte_mode;
	bool byte_read;

	if ((content == NULL) || (content->command == NULL))
		return kStatus_InvalidArgument;

	byte_mode = f4sdio_byte_mode(content->command);
	byte_read = (content->data != NULL) && f4sdio_byte_read(content->data);

	/*
	 * Clear the command/data status this transfer may inherit, but never the
	 * card-interrupt flag while the card line is armed: a card event that
	 * arrived between the ISR's stand-down and this transfer would otherwise
	 * be erased without the card's source being served.
	 */
	SDIO->ICR = s_f4sdio_irq_armed ?
		    (0xFFFFFFFFUL & ~(uint32_t)SDIO_FLAG_SDIOIT) : 0xFFFFFFFFUL;

	if (content->data != NULL) {
		status = f4sdio_data_config(content->data, byte_mode, byte_read);
		if (status != kStatus_Success)
			return status;
	}

	f4sdio_irq_mask();

	status = f4sdio_send_command(host, content->command);
	if (status != kStatus_Success) {
		/*
		 * A command that never completed is the one failure this path used to
		 * swallow: the data-phase failure has its own line below, but a
		 * CMD53 that the card never answered (or answered with a CRC error)
		 * returns here with nothing on the console - and a transmit write
		 * then simply never happens.
		 */
		SDMMC_LOGE("f4sdio",
			   "cmd%u arg=0x%08x failed sta=0x%08x dir=%s len=%u",
			   (unsigned)content->command->index,
			   (unsigned)content->command->argument,
			   (unsigned)SDIO->STA,
			   (content->data != NULL) ?
			   ((content->data->txData != NULL) ? "tx" : "rx") : "-",
			   (content->data != NULL) ?
			   (unsigned)(content->data->blockSize *
				      content->data->blockCount) : 0u);
		f4sdio_irq_unmask();
		return status;
	}

	if (s_f4sdio_tx_deferred) {
		s_f4sdio_tx_deferred = false;
		SDIO->DCTRL = s_f4sdio_tx_dctrl;
		/* The data path one bus write after arming: a TXUNDERR already set
		 * here says the DPSM started with an empty FIFO, before the frame
		 * was written. */
		s_f4sdio_tx_sta_arm = SDIO->STA;
	}

	if (content->data != NULL) {
		status = f4sdio_transfer_data(content->data, byte_mode,
					      byte_read);
		if ((content->data->rxData != NULL) &&
		    (content->command->index == 53U) &&
		    (F4SDIO_ARG_DEV(content->command->argument) ==
		     F4SDIO_RX_FIFO_DEV))
			f4sdio_fifo_record(content->data, status);
		if (content->data->txData != NULL) {
			unsigned int dev = F4SDIO_ARG_DEV(content->command->argument);

			s_f4dbg.tx_xfers++;
			if (status == kStatus_Success)
				s_f4dbg.tx_ok++;
			else
				s_f4dbg.tx_err++;
			if ((content->command->index == 53U) &&
			    (dev == F4SDIO_TX_HIQ_DEV || dev == F4SDIO_TX_MIQ_DEV ||
			     dev == F4SDIO_TX_LOQ_DEV))
				f4sdio_txq_record(content->data, status, dev);
		}
		if (byte_read && !s_f4sdio_irq_armed)
			SDIO->DCTRL &= ~SDIO_DCTRL_SDIOEN;
		f4sdio_irq_unmask();
		if (status != kStatus_Success) {
			SDMMC_LOGE("f4sdio",
				   "data cmd=%u len=%u dir=%s sta=0x%08x "
				   "left=%u tsta=0x%08x dcnt=%u",
				   (unsigned)content->command->index,
				   (unsigned)(content->data->blockSize *
					      content->data->blockCount),
				   (content->data->rxData != NULL) ? "rx" : "tx",
				   (unsigned)SDIO->STA,
				   (unsigned)s_f4sdio_tx_left,
				   (unsigned)s_f4sdio_tx_sta,
				   (unsigned)s_f4sdio_tx_dcount);
			return status;
		}
		if (byte_read && !s_f4sdio_irq_armed)
			SDIO->DCTRL &= ~SDIO_DCTRL_SDIOEN;
	}

	f4sdio_irq_unmask();
	return kStatus_Success;
}

/*
 * The SD stack's wide registers (SCR via ACMD51, SD status via ACMD13) are
 * sent MSB first, while this controller's FIFO hands the first byte back in
 * the low byte of the first word.  The stack asks the host to put the packet
 * into its memory order; only the MSB-first form needs the swap.  CMD6's
 * switch status is usual LSB-first data and does not call this.
 */
static void f4sdio_convert_data_to_little_endian(sdmmchost_t *host,
						 uint32_t *data,
						 uint32_t wordSize,
						 uint32_t format)
{
	uint32_t i;

	(void)host;

	if ((data == NULL) ||
	    (format != (uint32_t)kSDMMC_DataPacketFormatMSBFirst)) {
		return;
	}

	for (i = 0U; i < wordSize; i++) {
		data[i] = ((data[i] & 0x000000FFUL) << 24) |
			  ((data[i] & 0x0000FF00UL) << 8) |
			  ((data[i] & 0x00FF0000UL) >> 8) |
			  ((data[i] & 0xFF000000UL) >> 24);
	}
}

/*
 * DAT0 is the card's busy line after a write or erase.  The SD stack polls it
 * through SDMMCHOST_IsCardBusy, and only a host that answers lets
 * SD_PollingCardStatusBusy run its timeout instead of issuing CMD13 without a
 * bound.  PC8 is D0 on this board (sdio_board.c).
 */
static bool f4sdio_card_is_busy(sdmmchost_t *host)
{
	(void)host;
	return (GPIOC->IDR & (1UL << 8U)) == 0U;
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
	.convertDataToLittleEndian = f4sdio_convert_data_to_little_endian,

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
	.cardIsBusy = f4sdio_card_is_busy,

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

#if defined(CONFIG_FSL_SDMMC_ENABLE_SDIO)
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
#endif /* CONFIG_FSL_SDMMC_ENABLE_SDIO */

#if defined(CONFIG_FSL_SDMMC_ENABLE_SD)
status_t FDWMMCHOST_SDConfig(sdmmc_sd_t *sdmmc, sdmmchost_config_t *config)
{
	sdmmchost_t *host = &sdmmc->host;
	sd_card_t *card = &sdmmc->card;
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
	card->noInteralAlign = false;

	host->capability = (uint32_t)kSDMMCHOST_SupportVoltage3v3 |
			   (uint32_t)kSDMMCHOST_Support4BitDataWidth;
	host->maxBlockCount = (uint32_t)(config->maxTransSize / config->defBlockSize);
	host->maxBlockSize = SDMMCHOST_SUPPORT_MAX_BLOCK_LENGTH;
	host->sourceClock_Hz = sdio_board_source_hz();

	return kStatus_Success;
}
#endif /* CONFIG_FSL_SDMMC_ENABLE_SD */

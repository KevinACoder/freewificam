/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * sdio_host.c - the STM32F429 SDIO host for the AP6181 (BCM43362).
 *
 * Bus shape: 400 kHz / 1-bit for enumeration, then 24 MHz / 4-bit.  Commands
 * are polled (CMD0/3/5/7/52/53); the data phase of a CMD53 is carried by
 * DMA2 stream 3 channel 4, armed from the controller's CMDREND interrupt and
 * completed by the stream's transfer-complete interrupt.  The module's card
 * interrupt (DAT1) stays interrupt-driven too: SDIOIT wakes the WWD thread,
 * which then issues its own reads.
 *
 * Every read is staged and every unaligned or non-block-multiple write is
 * staged, so a caller's pointer never reaches the DMA engine directly.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "RTOS/wwd_rtos_interface.h"
#include "platform/wwd_bus_interface.h"
#include "platform/wwd_platform_interface.h"
#include "platform/wwd_sdio_interface.h"
#include "wwd_constants.h"
#include "wwd_debug.h"

#include "internal/wwd_thread.h"

#include "sdio_board.h"
#include "uart.h"
#include "wiced_port.h"
#include "wiced_port_config.h"

/* --- SDIO controller ------------------------------------------------------ */

#define SDIO_BASE		0x40012C00UL
#define SDIO_POWER		(*(volatile uint32_t *)(SDIO_BASE + 0x00UL))
#define SDIO_CLKCR		(*(volatile uint32_t *)(SDIO_BASE + 0x04UL))
#define SDIO_ARG		(*(volatile uint32_t *)(SDIO_BASE + 0x08UL))
#define SDIO_CMD		(*(volatile uint32_t *)(SDIO_BASE + 0x0CUL))
#define SDIO_RESP1		(*(volatile uint32_t *)(SDIO_BASE + 0x14UL))
#define SDIO_DTIMER		(*(volatile uint32_t *)(SDIO_BASE + 0x24UL))
#define SDIO_DLEN		(*(volatile uint32_t *)(SDIO_BASE + 0x28UL))
#define SDIO_DCTRL		(*(volatile uint32_t *)(SDIO_BASE + 0x2CUL))
#define SDIO_DCOUNT		(*(volatile uint32_t *)(SDIO_BASE + 0x30UL))
#define SDIO_STA		(*(volatile uint32_t *)(SDIO_BASE + 0x34UL))
#define SDIO_ICR		(*(volatile uint32_t *)(SDIO_BASE + 0x38UL))
#define SDIO_MASK		(*(volatile uint32_t *)(SDIO_BASE + 0x3CUL))
#define SDIO_FIFOCNT		(*(volatile uint32_t *)(SDIO_BASE + 0x48UL))
#define SDIO_FIFO		(*(volatile uint32_t *)(SDIO_BASE + 0x80UL))

#define SDIO_POWER_ON		3UL
#define SDIO_CLKCR_CLKEN	(1UL << 8)
#define SDIO_CLKCR_PWRSAV	(1UL << 9)
#define SDIO_CLKCR_WIDBUS_4	(1UL << 11)

#define SDIO_CMD_WAITRESP_SHORT	(1UL << 6)
#define SDIO_CMD_CPSMEN		(1UL << 10)

#define SDIO_DCTRL_DTEN		(1UL << 0)
#define SDIO_DCTRL_DTDIR_CARD	(1UL << 1)	/* card -> host (read) */
#define SDIO_DCTRL_DTMODE_BLOCK	(1UL << 2)
#define SDIO_DCTRL_DMAEN	(1UL << 3)
#define SDIO_DCTRL_SDIOEN	(1UL << 11)

#define STA_CCRCFAIL		(1UL << 0)
#define STA_DCRCFAIL		(1UL << 1)
#define STA_CTIMEOUT		(1UL << 2)
#define STA_DTIMEOUT		(1UL << 3)
#define STA_TXUNDERR		(1UL << 4)
#define STA_RXOVERR		(1UL << 5)
#define STA_CMDREND		(1UL << 6)
#define STA_CMDSENT		(1UL << 7)
#define STA_DATAEND		(1UL << 8)
#define STA_STBITERR		(1UL << 9)
#define STA_TXACT		(1UL << 12)
#define STA_RXACT		(1UL << 13)
#define STA_TXFIFOHE		(1UL << 14)
#define STA_RXFIFOHF		(1UL << 15)
#define STA_TXFIFOF		(1UL << 16)
#define STA_RXFIFOF		(1UL << 17)
#define STA_TXDA		(1UL << 20)
#define STA_RXDA		(1UL << 21)
#define STA_SDIOIT		(1UL << 22)

#define STA_DATA_ERRORS		(STA_CCRCFAIL | STA_DCRCFAIL | STA_TXUNDERR | \
				 STA_RXOVERR | STA_STBITERR | STA_DTIMEOUT)
#define STA_CMD_ERRORS		(STA_CCRCFAIL | STA_CTIMEOUT)
#define STA_ALL			(0xFFFFFFFFUL)

#define MASK_CCRCFAIL		(1UL << 0)
#define MASK_DTIMEOUT		(1UL << 3)
#define MASK_TXUNDERR		(1UL << 4)
#define MASK_RXOVERR		(1UL << 5)
#define MASK_CMDREND		(1UL << 6)
#define MASK_CMDSENT		(1UL << 7)
#define MASK_STBITERR		(1UL << 9)
#define MASK_SDIOIT		(1UL << 22)
#define MASK_DATA_ERRORS	(MASK_CCRCFAIL | MASK_DTIMEOUT | MASK_TXUNDERR | \
				 MASK_RXOVERR | MASK_STBITERR)

#define BUS_RETRIES		5U
#define CMD_SPIN		1000000U	/* polled command, several ms */
#define FIFO_SPIN		200000U		/* one FIFO word handshake */
#define DATA_IDLE_SPIN		2000000U	/* TXACT/RXACT release */

#define SDIO_STAGE_SIZE		2048U

/* --- RCC ------------------------------------------------------------------ */

#define RCC_BASE		0x40023800UL
#define RCC_APB2RSTR		(*(volatile uint32_t *)(RCC_BASE + 0x0CUL))
#define RCC_APB2ENR		(*(volatile uint32_t *)(RCC_BASE + 0x44UL))
#define RCC_APB2_SDIO		(1UL << 11)

/* A full controller reset, the way the vendor host's SDIO_DeInit/SDIO_Init
 * pair does it.  Without it the data path carries state from the 400 kHz
 * phase into the first 24 MHz transfer: the FIFO's own flags look idle while
 * the block sequence never completes. */
static void
sdio_controller_reset(void)
{
	RCC_APB2ENR |= RCC_APB2_SDIO;
	RCC_APB2RSTR |= RCC_APB2_SDIO;
	RCC_APB2RSTR &= ~RCC_APB2_SDIO;
	(void)RCC_APB2RSTR;
}

/* --- DMA2 stream 3 (SDIO is channel 4) ----------------------------------- */

#define DMA2_BASE		0x40026400UL
#define DMA2_LISR		(*(volatile uint32_t *)(DMA2_BASE + 0x00UL))
#define DMA2_LIFCR		(*(volatile uint32_t *)(DMA2_BASE + 0x08UL))
#define DMA2_S3CR		(*(volatile uint32_t *)(DMA2_BASE + 0x58UL))
#define DMA2_S3NDTR		(*(volatile uint32_t *)(DMA2_BASE + 0x5CUL))
#define DMA2_S3PAR		(*(volatile uint32_t *)(DMA2_BASE + 0x60UL))
#define DMA2_S3M0AR		(*(volatile uint32_t *)(DMA2_BASE + 0x64UL))
#define DMA2_S3FCR		(*(volatile uint32_t *)(DMA2_BASE + 0x6CUL))

#define DMA2_STREAM3_FLAGS	(0x3FUL << 22)	/* LIFCR, stream 3, all five */
#define DMA_LISR_TCIF3		(1UL << 27)
#define DMA_LISR_TEIF3		(1UL << 25)
#define DMA_LISR_DMEIF3		(1UL << 23)
#define DMA_LISR_FEIF3		(1UL << 21)

#define DMA_SCR_EN		(1UL << 0)
#define DMA_SCR_DMEIE		(1UL << 1)
#define DMA_SCR_TEIE		(1UL << 2)
#define DMA_SCR_TCIE		(1UL << 4)
#define DMA_SCR_PFCTRL		(1UL << 5)
#define DMA_SCR_DIR_M2P		(1UL << 6)
#define DMA_SCR_MINC		(1UL << 10)
#define DMA_SCR_PSIZE_WORD	(2UL << 11)
#define DMA_SCR_MSIZE_WORD	(2UL << 13)
#define DMA_SCR_PL_VERY_HIGH	(3UL << 16)
#define DMA_SCR_PBURST_INC4	(2UL << 21)
#define DMA_SCR_MBURST_INC4	(2UL << 23)
#define DMA_SCR_CHSEL_4		(4UL << 25)

#define DMA_S3_COMMON		(DMA_SCR_MINC | \
				 DMA_SCR_PSIZE_WORD | DMA_SCR_MSIZE_WORD | \
				 DMA_SCR_PL_VERY_HIGH | DMA_SCR_CHSEL_4)
/* Single-beat DMA flow control in direct mode: the vendor's PFCTRL + INC4
 * burst + FIFO shape raises FEIF on this controller at the moment the stream
 * is enabled (EN is cleared by hardware before a byte moves). */
#define DMA_S3_FCR_VALUE	(0UL)

#define DMA_DATA_SPIN		2000000U

static host_semaphore_type_t s_done;
static volatile uint32_t s_current_command;
static volatile wwd_bus_transfer_direction_t s_direction;
static volatile bool s_failed;
static bool s_bus_ready;

static uint8_t s_stage[SDIO_STAGE_SIZE] __attribute__((aligned(4)));

/* The console's own line surface is the log component; this driver's trace
 * goes through the same early UART directly because it must be readable even
 * when the transfer it describes left the bus wedged.  It is a failure
 * diagnostic, not a log: one line per failed attempt, bounded by the retry
 * count. */
static void
ap6181_trace(const char *fmt, ...)
{
	char buf[112];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n > 0)
		uart_early_puts(buf);
}

static uint32_t
clock_divider(uint32_t hz)
{
	uint32_t source = sdio_board_source_hz();
	uint32_t div;

	if (hz == 0U || source <= hz)
		return 0U;
	div = (source + hz - 1U) / hz;
	return (div >= 2U) ? (div - 2U) : 0U;
}

static uint32_t
block_bits(uint32_t bytes)
{
	switch (bytes) {
	case 1: return 0U;
	case 2: return 1U;
	case 4: return 2U;
	case 8: return 3U;
	case 16: return 4U;
	case 32: return 5U;
	case 64: return 6U;
	case 128: return 7U;
	case 256: return 8U;
	case 512: return 9U;
	case 1024: return 10U;
	case 2048: return 11U;
	default: return 9U;
	}
}

static uint32_t
optimal_block(uint32_t bytes)
{
	if (bytes > 256U)
		return 512U;
	if (bytes > 128U)
		return 256U;
	if (bytes > 64U)
		return 128U;
	if (bytes > 32U)
		return 64U;
	if (bytes > 16U)
		return 32U;
	if (bytes > 8U)
		return 16U;
	if (bytes > 4U)
		return 8U;
	return 4U;
}

static void
clock_set(uint32_t hz, bool wide4, bool powersave)
{
	uint32_t clkcr = clock_divider(hz) & 0xFFUL;

	SDIO_POWER = SDIO_POWER_ON;
	if (wide4)
		clkcr |= SDIO_CLKCR_WIDBUS_4;
	if (powersave)
		clkcr |= SDIO_CLKCR_PWRSAV;
	SDIO_CLKCR = clkcr | SDIO_CLKCR_CLKEN;
}

/* One polled command.  Returns the controller's status once the response (or
 * the send) is done, or 0 when the fixed budget expired with nothing. */
static uint32_t
command_send(uint32_t command, uint32_t argument, bool response_needed,
	     bool settle_cmdact)
{
	uint32_t spin = CMD_SPIN;
	uint32_t wanted = response_needed ? STA_CMDREND : STA_CMDSENT;

	SDIO_ICR = STA_ALL;
	SDIO_ARG = argument;
	SDIO_CMD = command | (response_needed ? SDIO_CMD_WAITRESP_SHORT : 0U) |
		   SDIO_CMD_CPSMEN;
	for (;;) {
		uint32_t sta = SDIO_STA;

		if ((sta & wanted) != 0U) {
			uint32_t settle = CMD_SPIN;

			/* The command line is not idle until CMDACT drops;
			 * sending the next command before that puts the new
			 * command's response window on top of the old one.  A
			 * CMD53 does not pay this wait: its data phase starts
			 * on the response and every microsecond before the FIFO
			 * is fed is an underrun risk. */
			if (settle_cmdact)
				while ((SDIO_STA & (1UL << 11)) != 0U &&
				       --settle != 0U)
					;
			SDIO_ICR = STA_CMDREND | STA_CMDSENT;
			return sta;
		}
		if ((sta & STA_CMD_ERRORS) != 0U)
			return 0U;
		if (--spin == 0U)
			return 0U;
	}
}

/* Serve the data phase of a CMD53 from the CPU.  `total` is the block-rounded
 * byte count the command's argument asked the card for.
 *
 * A transmit primes the FIFO before the data path is opened: DTEN with an
 * empty FIFO is an underrun the controller reports mid-block, and the card
 * then sees a short block sequence (measured: 1000 of 1024 bytes, TXUNDERR). */
/* Arm the stream.  Called from the CMDREND interrupt, which is where the
 * vendor host arms its own stream: the card starts its data phase on the
 * response, and any later the FIFO is already too late. */
static void
dma_arm(wwd_bus_transfer_direction_t direction)
{
	uint32_t cr = DMA_S3_COMMON | DMA_SCR_EN | DMA_SCR_TCIE;

	if (direction == BUS_WRITE)
		cr |= DMA_SCR_DIR_M2P;
	DMA2_S3CR = cr;
	ap6181_trace("ap6181: armed cr=0x%08lx ndtr=%lu lisr=0x%08lx\n",
		     (unsigned long)DMA2_S3CR, (unsigned long)DMA2_S3NDTR,
		     (unsigned long)DMA2_LISR);
}

/* Wait for the stream's completion interrupt and for the data path to go
 * idle.  DCOUNT alone is not completion: returning while TXACT/RXACT is still
 * asserted leaves the card mid-transfer and the next command is answered with
 * an R5 error. */
static bool
data_wait(uint32_t total)
{
	uint32_t spin;
	uint32_t sta;

	/* Short wait: a healthy transfer completes in microseconds, and the
	 * four-word stall is known, so a long wait only stretches the gap
	 * between chunks (the card's backplane goes to sleep in long gaps and
	 * then NAKs the bring-up accesses). */
	if (host_rtos_get_semaphore(&s_done, 5U, WICED_TRUE) != WWD_SUCCESS) {
		ap6181_stats.cmd_timeouts++;
		return false;
	}
	if (s_failed)
		return false;

	spin = DMA_DATA_SPIN;
	for (;;) {
		sta = SDIO_STA;
		if ((sta & STA_DATA_ERRORS) != 0U)
			return false;
		/* DCOUNT counts the bytes already moved, so completion is the
		 * count reaching the transfer size, not zero. */
		if ((sta & (STA_TXACT | STA_RXACT)) == 0U &&
		    SDIO_DCOUNT >= total)
			return true;
		if (--spin == 0U)
			return false;
	}
	(void)total;
}

/* The stream on this controller stops four words before the end of a block
 * write (measured: NDTR=4, DCOUNT=16, no error flag, DPSM idle).  Finish the
 * last words from the CPU: the card is still clocking and takes them, which
 * turns the stall into a completed transfer. */
static bool
pio_tail(uint32_t total)
{
	uint32_t words = total / 4U;
	uint32_t left = DMA2_S3NDTR & 0xFFFFUL;
	uint32_t done = (left <= words) ? (words - left) : 0U;
	uint32_t i;
	uint32_t spin;

	for (i = done; i < words; i++) {
		spin = FIFO_SPIN;
		while ((SDIO_STA & STA_TXFIFOF) != 0U && --spin != 0U)
			;
		if (spin == 0U)
			return false;
		SDIO_FIFO = ((const uint32_t *)s_stage)[i];
	}

	spin = DATA_IDLE_SPIN;
	while (spin-- != 0U) {
		uint32_t sta = SDIO_STA;

		if ((sta & STA_DATA_ERRORS) != 0U)
			return false;
		if ((sta & (STA_TXACT | STA_RXACT)) == 0U)
			return true;
	}
	return false;
}

/* --- platform bus interface ---------------------------------------------- */

wwd_result_t
host_platform_bus_init(void)
{
	if (host_rtos_init_semaphore(&s_done) != WWD_SUCCESS)
		return WWD_SEMAPHORE_ERROR;

	sdio_board_init();
	sdio_controller_reset();
	clock_set(AP6181_SDIO_INIT_HZ, false, true);

	SDIO_ICR = STA_ALL;
	SDIO_MASK = 0U;

	sdio_board_irq_enable(true);
	sdio_board_dma_irq_enable(true);
	s_bus_ready = true;
	return WWD_SUCCESS;
}

wwd_result_t
host_platform_bus_deinit(void)
{
	sdio_board_irq_enable(false);
	sdio_board_dma_irq_enable(false);
	SDIO_MASK = 0U;
	SDIO_CLKCR = 0U;
	SDIO_POWER = 0U;
	(void)host_rtos_deinit_semaphore(&s_done);
	s_bus_ready = false;
	return WWD_SUCCESS;
}

wwd_result_t
host_platform_sdio_enumerate(void)
{
	uint32_t loop;
	uint32_t response = 0U;

	if (!s_bus_ready)
		return WWD_SDIO_RETRIES_EXCEEDED;

	for (loop = 0U; loop < AP6181_SDIO_ENUMERATE_MS; loop++) {
		ap6181_stats.enum_attempts++;
		(void)command_send((uint32_t)SDIO_CMD_0, 0U, false, true);
		/* CMD5 has a response (R4): the controller must sample it, or
		 * the card's pending response collides with CMD3's window.  Its
		 * bit 31 is the card's "I/O ready" flag; CMD3 before that flag
		 * is a command the card is allowed to ignore, and the ignore
		 * shows up as a response that lands after the next attempt's
		 * window. */
		/* CMD5's result is not a gate: this module often returns a
		 * response the controller scores as a command CRC error, and it
		 * still answers CMD3.  The vendor host ignores the CMD5 result
		 * for the same reason; gating on it left the loop spinning
		 * without ever sending CMD3. */
		(void)command_send((uint32_t)SDIO_CMD_5, 0U, true, true);
		if (command_send((uint32_t)SDIO_CMD_3, 0U, true, true) != 0U) {
			response = SDIO_RESP1;
			break;
		}
next:
		ap6181_stats.cmd_timeouts++;
		(void)host_rtos_delay_milliseconds(1U);
	}
	if (loop == AP6181_SDIO_ENUMERATE_MS) {
		ap6181_trace("ap6181: enum timeout after %lu loops sta=0x%08lx\n",
			     (unsigned long)ap6181_stats.enum_attempts,
			     (unsigned long)SDIO_STA);
		return WWD_TIMEOUT;
	}

	ap6181_stats.rca = (uint16_t)(response >> 16);
	(void)command_send((uint32_t)SDIO_CMD_7, response & 0xFFFF0000UL, true, true);
	(void)host_rtos_delay_milliseconds(1U);
	ap6181_stats.enumerated = 1U;
	ap6181_stats.bus_width = 1U;
	ap6181_stats.clock_hz = AP6181_SDIO_INIT_HZ;
	return WWD_SUCCESS;
}

wwd_result_t
host_platform_sdio_transfer(wwd_bus_transfer_direction_t direction,
			    wwd_sdio_command_t command,
			    sdio_transfer_mode_t mode,
			    sdio_block_size_t block_size, uint32_t argument,
			    uint32_t *data, uint16_t data_size,
			    sdio_response_needed_t response_expected,
			    uint32_t *response)
{
	uint32_t attempts = 0U;
	uint32_t block = (uint32_t)block_size;

	if (response != NULL)
		*response = 0U;
	if (command == SDIO_CMD_53 && (data == NULL || data_size == 0U))
		return WWD_BADARG;

restart:
	SDIO_ICR = STA_ALL;
	s_current_command = (uint32_t)command;
	attempts++;
	if (attempts > BUS_RETRIES) {
		ap6181_trace("ap6181: xfer give-up cmd=%lu dir=%d arg=0x%08lx "
			     "len=%lu sta=0x%08lx\n", (unsigned long)command,
			     (int)direction, (unsigned long)argument,
			     (unsigned long)data_size, (unsigned long)SDIO_STA);
		ap6181_stats.cmd_timeouts++;
		return WWD_SDIO_RETRIES_EXCEEDED;
	}

	if (command == SDIO_CMD_53) {
		uint32_t total;

		/* The 43362's byte-mode CMD53 carries its length in the block
		 * field, so the argument's size is the block the host will use;
		 * block mode uses the caller's size and block count. */
		if (mode == SDIO_BYTE_MODE) {
			block = optimal_block(data_size);
			argument = (block < 512U) ?
				((argument & ~0x1FFUL) | block) :
				(argument & ~0x1FFUL);
			total = block;
		} else {
			total = ((data_size + block - 1U) / block) * block;
		}
		if (total > SDIO_STAGE_SIZE)
			return WWD_BUFFER_SIZE_SET_ERROR;

		/* Every transfer is staged: the DMA engine needs a word count and
		 * a caller's buffer may be unaligned, shorter than a block, or
		 * both. */
		if (direction == BUS_WRITE) {
			memcpy(s_stage, data, data_size);
			if (total > data_size)
				memset(s_stage + data_size, 0, total - data_size);
		}

		ap6181_stats.last_dlen = total;
		ap6181_stats.last_arg = argument;
		ap6181_stats.last_cmd = (uint32_t)command;

		SDIO_DTIMER = 0xFFFFFFFFUL;
		SDIO_DLEN = total;
		SDIO_DCTRL = (block_bits(block) << 4) | SDIO_DCTRL_DTMODE_BLOCK |
			     SDIO_DCTRL_DMAEN | SDIO_DCTRL_DTEN |
			     ((direction == BUS_READ) ? SDIO_DCTRL_DTDIR_CARD : 0U);

		/* Prepare the stream, then send the command; the CMDREND
		 * interrupt arms it.  This is the vendor host's order. */
		DMA2_S3CR = 0U;
		DMA2_LIFCR = DMA2_STREAM3_FLAGS;
		DMA2_S3FCR = DMA_S3_FCR_VALUE;
		DMA2_S3PAR = (uint32_t)(uintptr_t)&SDIO_FIFO;
		DMA2_S3M0AR = (uint32_t)(uintptr_t)s_stage;
		DMA2_S3NDTR = total / 4U;

		s_direction = direction;
		s_failed = false;
		(void)host_rtos_get_semaphore(&s_done, 0U, WICED_TRUE);

		SDIO_MASK = MASK_SDIOIT | MASK_CMDREND | MASK_CMDSENT |
			    MASK_DATA_ERRORS | MASK_DTIMEOUT | MASK_TXUNDERR |
			    MASK_RXOVERR | MASK_STBITERR;
		SDIO_ICR = STA_ALL;
		SDIO_ARG = argument;
		SDIO_CMD = (uint32_t)command | SDIO_CMD_WAITRESP_SHORT |
			   SDIO_CMD_CPSMEN;

		if (!data_wait(total) &&
		    !(direction == BUS_WRITE && pio_tail(total))) {
			ap6181_trace("ap6181: dma fail dir=%d len=%lu att=%lu "
				     "sta=0x%08lx dc=0x%08lx ndtr=%lu lisr=0x%08lx "
				     "dma=0x%08lx\n",
				     (int)direction, (unsigned long)total,
				     (unsigned long)attempts,
				     (unsigned long)SDIO_STA,
				     (unsigned long)SDIO_DCOUNT,
				     (unsigned long)DMA2_S3NDTR,
				     (unsigned long)DMA2_LISR,
				     (unsigned long)ap6181_stats.dma_intrs);
			(void)command_send((uint32_t)SDIO_CMD_52,
					   0x110UL << 9, true, true);
			ap6181_trace("ap6181: cccr110=0x%02lx dcount=%lu\n",
				     (unsigned long)(SDIO_RESP1 & 0xFFUL),
				     (unsigned long)SDIO_DCOUNT);
			ap6181_trace("ap6181: isr sta=0x%08lx n=%lu cmd53 isr=%lu "
				     "cmd=%lu\n",
				     (unsigned long)ap6181_stats.last_isr_sta,
				     (unsigned long)ap6181_stats.sdio_intrs,
				     (unsigned long)ap6181_stats.cmd53_isr,
				     (unsigned long)s_current_command);
			ap6181_stats.dma_errors++;
			goto retry;
		}
		SDIO_MASK = MASK_SDIOIT;
		if (direction == BUS_READ)
			memcpy(data, s_stage, data_size);
	} else {
		if (command_send((uint32_t)command, argument,
				 response_expected == RESPONSE_NEEDED,
				 true) == 0U) {
			ap6181_stats.cmd_timeouts++;
			goto retry;
		}
		if (command == SDIO_CMD_52) {
			uint32_t a52 = (argument >> 9) & 0x1FFFFUL;

			ap6181_stats.cmd52_count++;
			ap6181_stats.last52_arg = argument;
			ap6181_stats.last52_resp = SDIO_RESP1;
			if ((a52 >= 0x03400UL && a52 <= 0x038FFUL) ||
			    (a52 >= 0x1000AUL && a52 <= 0x1000FUL) ||
			    a52 == 0x00004UL || a52 == 0x00007UL ||
			    a52 == 0x00110UL || a52 == 0x00210UL ||
			    a52 == 0x00211UL)
				ap6181_trace("ap6181: cmd52 %s a=0x%05lx d=0x%02lx "
					     "resp=0x%08lx\n",
					     ((argument & 0x80000000UL) != 0UL) ?
					     "wr" : "rd", (unsigned long)a52,
					     (unsigned long)(argument & 0xFFUL),
					     (unsigned long)SDIO_RESP1);
		}
	}

	if (response != NULL)
		*response = SDIO_RESP1;

	return WWD_SUCCESS;

retry:
	ap6181_stats.cmd_retries++;
	ap6181_stats.last_sta = SDIO_STA;
	ap6181_stats.last_dctrl = SDIO_DCTRL;
	ap6181_stats.last_dlen = SDIO_DLEN;
	SDIO_MASK = MASK_SDIOIT;
	SDIO_DCTRL = 0U;
	SDIO_ICR = STA_ALL;
	goto restart;
}

wwd_result_t
host_platform_enable_high_speed_sdio(void)
{
	sdio_controller_reset();
	clock_set(AP6181_SDIO_TRANSFER_HZ, AP6181_SDIO_4BIT != 0, false);
	SDIO_MASK = MASK_SDIOIT;
	ap6181_stats.bus_width = AP6181_SDIO_4BIT ? 4U : 1U;
	ap6181_stats.clock_hz = AP6181_SDIO_TRANSFER_HZ;
	return WWD_SUCCESS;
}

wwd_result_t
host_platform_bus_enable_interrupt(void)
{
	SDIO_MASK = MASK_SDIOIT;
	return WWD_SUCCESS;
}

wwd_result_t
host_platform_bus_disable_interrupt(void)
{
	SDIO_MASK = 0U;
	return WWD_SUCCESS;
}

wwd_result_t
host_platform_unmask_sdio_interrupt(void)
{
	SDIO_MASK = MASK_SDIOIT;
	return WWD_SUCCESS;
}

/* --- interrupt ------------------------------------------------------------ */

void
SDIO_IRQHandler(void)
{
	uint32_t sta = SDIO_STA;

	ap6181_stats.sdio_intrs++;
	ap6181_stats.last_isr_sta = sta;

	if ((sta & (STA_CMDREND | STA_CMDSENT)) != 0U) {
		SDIO_ICR = STA_CMDREND | STA_CMDSENT;
		if (s_current_command == (uint32_t)SDIO_CMD_53) {
			ap6181_stats.cmd53_isr++;
			dma_arm(s_direction);
		}
	}

	if ((sta & (STA_DATA_ERRORS | STA_CTIMEOUT)) != 0U) {
		ap6181_stats.last_err_sta = sta;
		s_failed = true;
		SDIO_ICR = STA_ALL;
		(void)host_rtos_set_semaphore(&s_done, WICED_TRUE);
		return;
	}

	if ((sta & STA_SDIOIT) != 0U) {
		SDIO_ICR = STA_SDIOIT;
		wwd_thread_notify_irq();
	}
}

void
DMA2_Stream3_IRQHandler(void)
{
	uint32_t lisr = DMA2_LISR;

	ap6181_stats.dma_intrs++;
	if ((lisr & (DMA_LISR_TEIF3 | DMA_LISR_DMEIF3 | DMA_LISR_FEIF3)) != 0U) {
		ap6181_stats.last_err_sta = lisr;
		s_failed = true;
	}
	DMA2_LIFCR = DMA2_STREAM3_FLAGS;
	(void)host_rtos_set_semaphore(&s_done, WICED_TRUE);
}
/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   uart.c
 * @brief  Console UART early polled path (ARM PL011, firefly family).
 *
 * The console is polled end to end - no interrupt mode, no CMSIS driver
 * shell. The polled RX primitive (uart_early_try_getc) is the console
 * shell's input path.
 *
 * Family-generic by construction: every firefly console is an ARM PL011
 * accessed 32-bit at the standard byte offsets - the access pattern the
 * vendor SDK proves on this silicon (the DTS reg-shift=<2> quirk is not how
 * the part decodes; the SDK's FtIn32 at base+offset works on real boards).
 * The base address and the uart clock arrive from the version's
 * board_conf.h (board/firefly/v1/ selects the firefly v1 map); this
 * file never names a register base itself.
 *
 * Divisor: firefly v1's UART clock is 48 MHz and 48e6 / (16 * 115200) =
 * 26.0417, so IBRD = 26 and FBRD = 3 (0.0417 * 64 = 2.67, rounded per the
 * PrimeCell specification's +0.5 rule) - computed from the board macros, so
 * another clock needs no edit here.
 *
 * @design exemplar Linux drivers/tty/serial/amba-pl011.c (the vendor-neutral
 * PL011 sequence: disable, clear, divisors, line, control) reduced to the
 * polled subset; the seam (uart_early_*) is the RK3568 board's
 * (board/rk3568-itx/drivers/uart.c), so every consumer above this file is
 * board-agnostic.
 */

#include <stdbool.h>
#include <stdint.h>

#include "board_conf.h"
#include "regs.h"
#include "uart.h"

#define UART_CONSOLE_BASE	BOARD_UART_BASE

/* Standard PL011 byte offsets. */
#define REG_DR		0x00	/* data (read RX / write TX) */
#define REG_FR		0x18	/* flag */
#define REG_IBRD	0x24	/* integer baud rate divisor */
#define REG_FBRD	0x28	/* fractional baud rate divisor */
#define REG_LCR_H	0x2c	/* line control */
#define REG_CR		0x30	/* control */
#define REG_IMSC	0x38	/* interrupt mask set/clear */
#define REG_ICR		0x44	/* interrupt clear */

#define FR_BUSY		(1u << 3)
#define FR_RXFE		(1u << 4)	/* RX FIFO empty */
#define FR_TXFF		(1u << 5)	/* TX FIFO full */

#define LCR_H_WLEN_8	(3u << 5)
#define LCR_H_FEN	(1u << 4)

#define CR_UARTEN	(1u << 0)
#define CR_TXE		(1u << 8)
#define CR_RXE		(1u << 9)

#define ICR_CLEAR_ALL	0x7ffu

/* --- register access ------------------------------------------------------ */

static inline uint32_t reg_read(uint32_t offset)
{
	return reg_rd32(UART_CONSOLE_BASE + offset);
}

static inline void reg_write(uint32_t offset, uint32_t value)
{
	reg_wr32(UART_CONSOLE_BASE + offset, value);
}

/* --- early path ------------------------------------------------------------ */

static bool early_ready;

/* Program the UART for polled 8N1 with the FIFO on. Shared by
 * uart_early_init and uart_early_panic, so both paths cannot drift apart.
 * The sequence is the PrimeCell one: disable before touching the divisor
 * latches, clear every pending interrupt, then line, then control. It is
 * idempotent. */
static void program_uart(void)
{
	uint32_t bauddiv = BOARD_UART_CLOCK_HZ / (16u * BOARD_UART_BAUD);
	uint32_t frac = ((BOARD_UART_CLOCK_HZ % (16u * BOARD_UART_BAUD)) * 4u +
			 BOARD_UART_BAUD / 2u) / BOARD_UART_BAUD;

	/* Interrupts stay off: this console is polled, and IMSC=0 guarantees a
	 * stray interrupt cannot fire before the GIC is programmed. */
	reg_write(REG_CR, 0x0u);
	reg_write(REG_ICR, ICR_CLEAR_ALL);
	reg_write(REG_IMSC, 0x0u);

	/* Integer and fractional divisor halves: frac is bauddiv's fractional
	 * part in 1/64 units (the +0.5 rounding folded into the numerator
	 * above). 48 MHz / 115200 8N1 gives 26 + 3/64. */
	reg_write(REG_IBRD, bauddiv);
	reg_write(REG_FBRD, frac);

	reg_write(REG_LCR_H, LCR_H_WLEN_8 | LCR_H_FEN);

	reg_write(REG_CR, CR_UARTEN | CR_TXE | CR_RXE);

	early_ready = true;
}

void uart_early_init(void)
{
	program_uart();
}

/* Wait for TX FIFO room. Unbounded spin, deliberately: the normal case
 * exits as soon as TXFF clears, and a bounded version that merely writes
 * anyway still costs a full timeout per byte on uncached Device reads - the
 * RK3568 line measured that as a console that appears to stop mid-message
 * (the same discipline applies to this part's Device-mapped FR). */
static void polled_putc(char c)
{
	if (!early_ready) {
		return;
	}

	if (c == '\n') {
		while ((reg_read(REG_FR) & FR_TXFF) != 0u) {
		}
		reg_write(REG_DR, '\r');
	}

	while ((reg_read(REG_FR) & FR_TXFF) != 0u) {
	}
	reg_write(REG_DR, (uint32_t)(uint8_t)c);
}

void uart_early_putc(char c)
{
	polled_putc(c);
}

void uart_early_puts(const char *s)
{
	if (s == 0) {
		return;
	}
	while (*s != '\0') {
		polled_putc(*s++);
	}
}

void uart_early_put_hex32(uint32_t value)
{
	static const char digits[] = "0123456789abcdef";
	char buf[9];
	int i;

	for (i = 7; i >= 0; i--) {
		buf[i] = digits[value & 0xfu];
		value >>= 4;
	}
	buf[8] = '\0';
	uart_early_puts(buf);
}

/* Polled RX primitive: one byte if the part has one waiting. This is the
 * console shell's input path (the shell port parks its task between polls
 * rather than waiting here). */
bool uart_early_try_getc(int *out)
{
	if (!early_ready || out == 0) {
		return false;
	}
	if ((reg_read(REG_FR) & FR_RXFE) != 0u) {
		return false;
	}
	*out = (int)(reg_read(REG_DR) & 0xffu);
	return true;
}

void uart_early_panic(const char *message)
{
	/* Re-program unconditionally: a fault may fire before any init, and a
	 * fault handler that cannot speak is indistinguishable from a dead
	 * board. */
	program_uart();
	uart_early_puts(message);
}

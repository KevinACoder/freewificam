/*
 * @file   uart.c
 * @brief  Console UART2 early polled path (DW-APB 16550-compatible).
 *
 * The RK3568 console UART is a DesignWare APB UART behind a 32-bit bus.
 * Register access is therefore 32-bit with a byte-offset stride of 4 - it is
 * NOT a byte-wide 16550, and byte accesses read/write garbage here.
 *
 * Divisor: the UART clock is 24 MHz and 24e6 / (16 * 115200) = 13, so DLL
 * holds 13. The latch is selected through LCR.DLAB and both halves must be
 * written with that bit set, then LCR restored.
 *
 * Polling, not interrupts: the console must work from the first instructions
 * of the boot path (before the GIC exists). This is the early path only -
 * no RX, no interrupt mode, no CMSIS driver shell (SPEC v3 §3.0 reductions).
 */

#include <stdbool.h>
#include <stdint.h>

#include "board_conf.h"
#include "regs.h"
#include "uart.h"

#define UART_CONSOLE_BASE	BOARD_UART_BASE

/* Byte offsets - already byte offsets, NOT register indices: the part has
 * reg-shift=2 / reg-io-width=4, so 16550 register index N sits at byte
 * offset 4*N (LSR, index 5, is at 0x14). No further shift belongs here.
 *
 * An earlier revision shifted again (base + (offset << 2)), which pointed
 * every access 4x too high. THR is at offset 0 - the one value where
 * shifting changes nothing - and that is the *only* reason the console
 * appeared to work: it could transmit, because the part had already been
 * programmed by the loader and nobody noticed that our own LCR/IER/MCR
 * writes were landing on the wrong registers.
 *
 * The visible symptom was a console that printed and then stopped dead. The
 * UART FIFO is 64 bytes deep, so short bursts (a banner, a dozen characters)
 * went out without ever needing to poll; the first write that had to wait
 * for room read LSR from the wrong address, never saw THR_EMPTY, and spun
 * forever. */
#define REG_RBR		0x00	/* receive buffer (read) */
#define REG_THR		0x00	/* transmit holding (write) */
#define REG_DLL		0x00	/* divisor latch low, DLAB=1 */
#define REG_IER		0x04	/* interrupt enable */
#define REG_DLH		0x04	/* divisor latch high, DLAB=1 */
#define REG_FCR		0x08	/* FIFO control (write) */
#define REG_LCR		0x0c	/* line control */
#define REG_MCR		0x10	/* modem control */
#define REG_LSR		0x14	/* line status */

#define LSR_THR_EMPTY	(1u << 5)

#define LCR_8N1		0x03	/* WLS = 8 data bits, no parity, 1 stop bit */
#define LCR_DLAB	(1u << 7)

#define FCR_ENABLE	(1u << 0)
#define FCR_CLEAR_RX	(1u << 1)
#define FCR_CLEAR_TX	(1u << 2)
#define FCR_TRIGGER_14	(3u << 6)

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

/* Program the UART for polled 8N1. Shared by uart_early_init and
 * uart_early_panic, so both paths cannot drift apart.
 *
 * No scratch-register self-test: the scratch register is optional in
 * 16550-compatible parts and is not implemented on this DW-APB UART - a
 * self-test refused to transmit on a perfectly working console and declared
 * it dead. There is no cheap way to prove a UART works other than
 * transmitting.
 *
 * No shadow-skip of repeated programming either (the prior image's driver
 * skips when the shadow matches): that mechanism exists for a CMSIS
 * Initialize->PowerControl pair whose divisor-latch window re-arming caused
 * an interrupt storm there; this image programs exactly once at boot plus
 * the fault path, and the sequence itself is idempotent. */
static void program_uart(void)
{
	uint32_t divisor = BOARD_UART_CLOCK_HZ / (16u * BOARD_UART_BAUD);
	const uint8_t dll = divisor & 0xffu;
	const uint8_t dlh = (divisor >> 8) & 0xffu;

	/* Interrupts stay off: this console is polled, and IER=0 guarantees a
	 * stray interrupt cannot fire before the GIC is programmed. */
	reg_write(REG_IER, 0x00u);

	reg_write(REG_LCR, LCR_DLAB);
	reg_write(REG_DLL, dll);
	reg_write(REG_DLH, dlh);
	reg_write(REG_LCR, LCR_8N1);

	reg_write(REG_FCR, FCR_ENABLE | FCR_CLEAR_RX | FCR_CLEAR_TX |
			   FCR_TRIGGER_14);

	/* DTR and RTS, what the console expects. */
	reg_write(REG_MCR, 0x03u);

	early_ready = true;
}

void uart_early_init(void)
{
	program_uart();
}

/* Wait for transmit room.
 *
 * Note this is an unbounded spin, deliberately: on this part THR_EMPTY
 * behaves as expected, and a bounded version was actively harmful - a
 * timeout that merely *writes anyway* still costs a full timeout per byte,
 * which on uncached Device reads is slow enough that the boot log appeared
 * to stop mid-message (reproduced on this board, 2026-09-30). The original
 * spin has no such cost because it exits as soon as the bit is set, which is
 * the normal case.
 *
 * The earlier "silent hang" that motivated a bound was misdiagnosed: the
 * real cause was a bad GIC register offset elsewhere in bring-up, not a
 * stuck transmit-ready bit.
 *
 * Only THR_EMPTY (bit 5) is polled here. Whether the transmitter has fully
 * drained (TEMT, bit 6) is deliberately NOT used: an attempt to drain on it
 * never succeeded on this part, which latched a permanent "stalled" state
 * and silenced the console from the first message onward - indistinguishable
 * from the board resetting. */
static void polled_putc(char c)
{
	if (!early_ready) {
		return;
	}

	if (c == '\n') {
		while ((reg_read(REG_LSR) & LSR_THR_EMPTY) == 0u) {
		}
		reg_write(REG_THR, '\r');
	}

	while ((reg_read(REG_LSR) & LSR_THR_EMPTY) == 0u) {
	}
	reg_write(REG_THR, (uint32_t)(uint8_t)c);
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

void uart_early_panic(const char *message)
{
	/* Re-program unconditionally: a fault may fire before any init, and a
	 * fault handler that cannot speak is indistinguishable from a dead
	 * board. */
	program_uart();
	uart_early_puts(message);
}

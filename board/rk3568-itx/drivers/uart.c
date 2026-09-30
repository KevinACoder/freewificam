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

#define LSR_DATA_READY	(1u << 0)
#define LSR_THR_EMPTY	(1u << 5)

#define LCR_8N1		0x03	/* WLS = 8 data bits, no parity, 1 stop bit */
#define LCR_DLAB	(1u << 7)

#define FCR_ENABLE	(1u << 0)
#define FCR_CLEAR_RX	(1u << 1)
#define FCR_CLEAR_TX	(1u << 2)
#define FCR_TRIGGER_14	(3u << 6)
#define FCR_TRIGGER_1	(0u << 6)

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

bool uart_early_try_getc(int *out)
{
	if (!early_ready || out == 0) {
		return false;
	}
	if ((reg_read(REG_LSR) & LSR_DATA_READY) == 0u) {
		return false;
	}
	*out = (int)(reg_read(REG_RBR) & 0xffu);
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

/* --- interrupt reception (S4-a, SPEC cherrysh-adapt §3.4) ------------------- */

#ifdef CONFIG_UART_RX_IRQ

#include "gicv3.h"

/* The armed reception: buffer, its length, how far the current drain got,
 * and the consumer's completion callback. Touched by the ISR and by the
 * arming consumer - the arming call happens once, from task context,
 * before the interrupt is enabled, so there is no race window. */
static uint8_t *rx_buf;
static uint32_t rx_len;
static uart_rx_event_cb_t rx_cb;
static bool rx_armed;
static bool rx_gic_ready;

/* IER bit 0 (ERBFI) enables both received-data-available and the receive
 * timeout - the two shapes a FIFO fill arrives in (a single keystroke
 * comes in via the 4-character timeout, a burst via the trigger level).
 * Line-status and modem interrupts stay OFF: they are the storm-class
 * causes and the shell has no use for them. */
#define IER_RX_ENABLE	0x01u

/* RX-side forensics (bring-up evidence; read by uart_console_rx_diag). */
volatile unsigned uart_console_rx_isr_calls;
volatile unsigned uart_console_rx_isr_bytes;

#define REG_IIR		0x08	/* interrupt identification (read) */

static void uart_console_rx_isr(void *arg)
{
	uint32_t n = 0U;

	(void)arg;
	uart_console_rx_isr_calls++;

	/* Drain until the FIFO is empty or the armed buffer is full - one
	 * completion per FIFO fill, not one per byte. The buffer is the
	 * FIFO's depth (64), so "full with bytes still queued" cannot
	 * happen. */
	while ((reg_read(REG_LSR) & LSR_DATA_READY) != 0u) {
		if (rx_buf == 0 || n >= rx_len) {
			break;
		}
		rx_buf[n++] = (uint8_t)(reg_read(REG_RBR) & 0xffu);
	}
	uart_console_rx_isr_bytes += n;

	if (rx_cb != 0) {
		/* The count is what actually arrived - a consumer that trusts
		 * a fixed size instead reads garbage; a consumer that gets a
		 * constant 0 is silently losing every byte (both are
		 * board-proven failure shapes from the prior project's UART
		 * line). */
		rx_cb(UART_RX_EVENT_COMPLETE, n);
	}
	/* Re-arming is the callback's business (it swaps in a fresh buffer
	 * first) - arming here would let a byte arriving during the copy
	 * land in a buffer the consumer is still reading. */
}

int uart_console_rx_arm(uint8_t *buf, uint32_t len, uart_rx_event_cb_t cb)
{
	if (buf == 0 || len == 0u || cb == 0) {
		return -1;
	}

	/* GIC side first - registering and enabling must not open the door
	 * before the reception is armed. The IER write below is the actual
	 * "interrupt on" switch; everything before it is setup. */
	if (!rx_gic_ready) {
		gicv3_init();
		if (irq_register(BOARD_CONSOLE_INTID, uart_console_rx_isr,
				 0) != 0 ||
		    irq_set_mode_level(BOARD_CONSOLE_INTID) != 0 ||
		    irq_set_priority(BOARD_CONSOLE_INTID,
				     BOARD_SPI_DEFAULT_PRIORITY_RAW) != 0) {
			return -1;
		}
		rx_gic_ready = true;
	}
	(void)irq_disable(BOARD_CONSOLE_INTID);
	(void)irq_clear_pending(BOARD_CONSOLE_INTID);

	/* Buffer in BEFORE the interrupt is enabled - the load-bearing
	 * order (uart.h). Boot-time keystrokes land in the client buffer,
	 * not in scratch nobody reads. */
	rx_buf = buf;
	rx_len = len;
	rx_cb = cb;
	rx_armed = true;

	(void)irq_enable(BOARD_CONSOLE_INTID);
	reg_write(REG_IER, IER_RX_ENABLE);
	/* Trigger level 1: every byte in the FIFO asserts RXRDY. The boot
	 * program_uart() leaves trigger 14 - fine for polled TX, fatal for
	 * interactive RX: a short line never reaches it, and this part's
	 * receive-timeout interrupt does not deliver (board-measured
	 * 2026-10-01: a 24-byte paste interrupted, an 8-byte "version" sat
	 * in the FIFO forever with IER=1 and nothing pending). Batching is
	 * the drain loop's job (it empties the FIFO per completion), not
	 * the trigger's. Trigger bits are FCR[7:6]; 00 = 1 character. */
	reg_write(REG_FCR, FCR_ENABLE | FCR_TRIGGER_1);
	return 0;
}

int uart_console_rx_armed(void)
{
	return rx_armed ? 1 : 0;
}

void uart_console_rx_gate(int stub_owns)
{
	if (stub_owns) {
		/* The stub owns the CPU and reads the FIFO by polling; the
		 * shell's RX interrupt must not fire under it. The byte in
		 * flight stays in the FIFO - the stub's polled reader sees
		 * it. */
		(void)irq_disable(BOARD_CONSOLE_INTID);
	} else {
		/* Hand the line back: everything the session left in the
		 * FIFO is session traffic (gdb's acks, half-typed input) -
		 * none of it may resurface as shell input, so flush rather
		 * than let it drain into the ring. */
		reg_write(REG_FCR, FCR_ENABLE | FCR_CLEAR_RX | FCR_CLEAR_TX |
					   FCR_TRIGGER_14);
		(void)irq_clear_pending(BOARD_CONSOLE_INTID);
		if (rx_armed) {
			(void)irq_enable(BOARD_CONSOLE_INTID);
		}
	}
}

/* One-line RX-path health check for bring-up: the UART's own interrupt
 * state (IER/IIR/LSR) and the GIC's enable/pending bits for the console
 * INTID, plus the ISR counters. Everything readback-silent on this
 * silicon is absent on purpose (see gicv3.c file header); ISENABLER/
 * ISPENDR are the registers this board DOES read back. */
void uart_console_rx_diag(void)
{
	uint32_t word = (BOARD_CONSOLE_INTID / 32u) * 4u;
	uint32_t bit = 1u << (BOARD_CONSOLE_INTID % 32u);
	uint32_t isen = reg_rd32(BOARD_GICD_BASE + 0x0100u + word);
	uint32_t ispend = reg_rd32(BOARD_GICD_BASE + 0x0200u + word);

	board_log("uart: rx diag intid=%u ier=%x iir=%x lsr=%x"
		  " isen=%x ispend=%x isr=%u bytes=%u\n",
		  (unsigned)BOARD_CONSOLE_INTID,
		  reg_read(REG_IER), reg_read(REG_IIR), reg_read(REG_LSR),
		  (unsigned)(isen & bit), (unsigned)(ispend & bit),
		  uart_console_rx_isr_calls, uart_console_rx_isr_bytes);
}

#endif /* CONFIG_UART_RX_IRQ */

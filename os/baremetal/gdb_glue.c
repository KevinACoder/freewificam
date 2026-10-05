/*
 * @file   gdb_glue.c
 * @brief  Bare-metal integration of the serial GDB stub.
 *
 * Shape carried over from the prior image's adapter glue (tx_gdb_glue.c),
 * minus everything that belonged to the shell/ThreadX world:
 *
 *  - Console ownership: the baremetal image has no shell and no UART RX
 *    interrupt - the stub is the ONLY consumer of console RX, so the
 *    session-boundary intr_ctrl is a no-op unless CONFIG_UART_RX_IRQ is
 *    built in, in which case it is the board driver's rx_gate (the stub
 *    masks the shell's RX interrupt for the length of the session and
 *    hands the line back - flushed - at exit; the S4 spec's "session
 *    boundary = ownership handover").
 *  - Session mute: the log layer's stamped sink asks board_console_muted()
 *    (weak in log.c); the strong answer here is gdb_session_active. The
 *    raw outlet is NOT gated - the stub's own stop log must reach the
 *    wire mid-session.
 *  - Break-in: a 0x03 seen by gdb_poll raises gdb_break() - a BRK
 *    exception that freezes the interrupted context for real (unlike the
 *    synthetic poll-site stop used for a plain host attach). With an
 *    interrupt-driven console the shell's RX path asks the same question
 *    through board_console_break_hook (weak in log.c, strong in the
 *    threadx glue) and hands '$' frames over through gdb_isr_attach().
 */

#include <stdint.h>
#include <stddef.h>

#include "gdb_glue.h"
#include "gdb/gdb.h"
#include "gdb/gdb_arch.h"
#include "log.h"
#include "arch_asm.h"
#include "generic_timer.h"
#include "minilibc.h"
#include "uart.h"

/* --- dbgport: polled console UART ------------------------------------------ */

static unsigned long cntvct(void)
{
	unsigned long v;

	v = gtimer_count_get();
	return v;
}

static void dbg_putc(char c)
{
	uart_early_putc(c);
}

/* One-byte pushback: gdb_poll may consume the '$' that opens a frame
 * before the session starts; the packet layer reads it back first. */
static int unget_char = -1;

/* Software push-back for the interrupt-driven handover (gdb_isr_attach):
 * the shell's RX interrupt has already pulled the attach frame's leading
 * bytes out of the FIFO when it saw the '$' - they ride here, and
 * dbg_getc drains this before polling the hardware. Written once by the
 * ISR before the session starts, consumed by the session that same
 * execution context: no concurrency. */
static uint8_t isr_prebuf[64];
static unsigned int isr_prebuf_len;

static int dbg_getc(void)
{
	unsigned long deadline;
	int c;

	if (isr_prebuf_len > 0U) {
		c = isr_prebuf[0];
		isr_prebuf_len--;
		for (unsigned int i = 0U; i < isr_prebuf_len; i++) {
			isr_prebuf[i] = isr_prebuf[i + 1U];
		}
		return c;
	}

	if (unget_char >= 0) {
		c = unget_char;
		unget_char = -1;
		return c;
	}

	/* Block until a byte is there. The bridge trickles bytes in with
	 * millisecond gaps; returning -1 on an empty FIFO would NAK a frame
	 * mid-read and tear the session down (board-proven). 5 s (at 24 MHz)
	 * keeps a stalled host from wedging the target forever. */
	deadline = cntvct() + 5UL * 24000UL;
	while (!uart_early_try_getc(&c)) {
		if (cntvct() > deadline) {
			return -1;
		}
	}
	return c;
}

static void dbg_intr_ctrl(int on)
{
#ifdef CONFIG_UART_RX_IRQ
	/* The interrupt-driven console's ownership handover: stub in = the
	 * shell's RX interrupt goes silent at the GIC; stub out = the FIFO
	 * (session residue: gdb's acks, half-typed input) is flushed so it
	 * cannot resurface as shell input, and the line is handed back. */
	uart_console_rx_gate(on);
#else
	(void)on;	/* no console RX interrupt in this image - see header */
#endif
}

static const struct gdb_dbgport dbgport = {
	.putc = dbg_putc,
	.getc = dbg_getc,
	.intr_ctrl = dbg_intr_ctrl,
};

/* Strong answer to log.c's weak gate: inside a session the stamped sinks
 * drop their output so board_log lines cannot land in the middle of the
 * RSP stream. The raw outlet does not ask. */
int board_console_muted(void)
{
	return gdb_session_active;
}

void gdb_break(void)
{
	__asm__ __volatile__("dsb sy");
	__asm__ __volatile__("brk %0" :: "i"(GDB_BRK_IMM));
}

/* Attach a RUNNING target, from any context that sees the first byte of a
 * host's RSP frame ('$'): gdb_poll's plain-attach branch and, with an
 * interrupt-driven console, the shell's RX interrupt. The stop is the
 * synthetic poll-site shape (registers = the attach site's snapshot, ESR
 * presented as the gate-BRK class so the PC-skip path treats it as a
 * compiled-in entry); `rest` carries bytes already pulled from the FIFO
 * behind the '$' - the rest of the host's first frame. Runs the session
 * to completion; returns after detach/continue. */
void gdb_isr_attach(int first_byte, const uint8_t *rest, unsigned int rest_len)
{
	unsigned long sp_snap;
	unsigned long lr_snap;
	unsigned long daif_snap;
	static struct gdb_trapframe tf;

	if (rest_len > sizeof(isr_prebuf)) {
		rest_len = sizeof(isr_prebuf);
	}
	for (unsigned int i = 0U; i < rest_len; i++) {
		isr_prebuf[i] = rest[i];
	}
	isr_prebuf_len = rest_len;
	unget_char = first_byte;

	__asm__ __volatile__("mov %0, sp" : "=r"(sp_snap));
	__asm__ __volatile__("mov %0, x30" : "=r"(lr_snap));
	ARCH_MRS(daif, daif_snap);

	tf.sp = sp_snap;
	tf.lr = lr_snap;
	tf.elr = lr_snap;
	tf.spsr = daif_snap;
	tf.esr = (0x3cUL << 26) | (unsigned long)GDB_BRK_IMM;
	tf.far = 0UL;
	for (unsigned int i = 0U; i < 30U; i++) {
		tf.x[i] = 0UL;
	}

	/* silent attach: any print here lands in gdb's RSP stream ahead of
	 * the handshake (board-measured) */
	gdb_sync_entry(&tf);

	/* Session over: anything the host left unconsumed is stale. */
	isr_prebuf_len = 0U;
}

void gdb_glue_init(void)
{
	gdb_dbgport_register(&dbgport);
	gdb_arch_setup();
	board_log("gdb: stub ready (rsp on uart2)\n");
}

/* --- main-loop watcher ------------------------------------------------------ */

void gdb_poll(void)
{
	int c;

	while (uart_early_try_getc(&c)) {
		if (c == 0x03) {
			/* Ctrl-C: raise the BRK on the spot - the trap
			 * freezes the interrupted world and the whole
			 * session runs from its sync entry. */
			uart_early_puts("gdb: Ctrl-C - entering stub\n");
			gdb_break();
			return;
		}
		if (c == '$') {
			/* Host attached to a RUNNING target: the frame that
			 * starts with this '$' carries the session's first
			 * command. Enter a session with a synthetic stop -
			 * the registers are the poll-site snapshot (this is
			 * a break-in, not a trap); a real frame comes from
			 * the 0x03/breakpoint paths. */
			gdb_isr_attach('$', 0, 0U);
			return;
		}
		/* Any other byte while running: no console reader exists in
		 * this image - drop it (the host's line chatter, etc.). */
	}
}

void gdb_sync_entry(struct gdb_trapframe *tf)
{
	gdb_debug_interrupt(tf);
}

/*
 * @file   gdb_glue.c
 * @brief  Bare-metal integration of the serial GDB stub.
 *
 * Shape carried over from the prior image's adapter glue (tx_gdb_glue.c),
 * minus everything that belonged to the shell/ThreadX world:
 *
 *  - Console ownership: there is no shell and no UART RX interrupt in
 *    this image - the stub is the ONLY consumer of console RX, so the
 *    session-boundary intr_ctrl is a no-op (the hook stays in the dbgport
 *    contract for the rounds that add an interrupt-driven console).
 *  - Session mute: the log layer's stamped sink asks board_console_muted()
 *    (weak in log.c); the strong answer here is gdb_session_active. The
 *    raw outlet is NOT gated - the stub's own stop log must reach the
 *    wire mid-session.
 *  - Break-in: a 0x03 seen by gdb_poll raises gdb_break() - a BRK
 *    exception that freezes the interrupted context for real (unlike the
 *    synthetic poll-site stop used for a plain host attach).
 */

#include <stdint.h>
#include <stddef.h>

#include "gdb_glue.h"
#include "gdb/gdb.h"
#include "gdb/gdb_arch.h"
#include "log.h"
#include "minilibc.h"
#include "uart.h"

/* --- dbgport: polled console UART ------------------------------------------ */

static unsigned long cntvct(void)
{
	unsigned long v;

	__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(v));
	return v;
}

static void dbg_putc(char c)
{
	uart_early_putc(c);
}

/* One-byte pushback: gdb_poll may consume the '$' that opens a frame
 * before the session starts; the packet layer reads it back first. */
static int unget_char = -1;

static int dbg_getc(void)
{
	unsigned long deadline;
	int c;

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
	(void)on;	/* no console RX interrupt in this image - see header */
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
			unsigned long sp_snap;
			unsigned long lr_snap;
			unsigned long daif_snap;
			static struct gdb_trapframe tf;

			__asm__ __volatile__("mov %0, sp" : "=r"(sp_snap));
			__asm__ __volatile__("mov %0, x30" : "=r"(lr_snap));
			__asm__ __volatile__("mrs %0, daif"
					     : "=r"(daif_snap));

			tf.sp = sp_snap;
			tf.lr = lr_snap;
			tf.elr = lr_snap;
			tf.spsr = daif_snap;
			/* Present the synthetic stop as the gate-BRK class
			 * (EC 0x3c / ISS 0x401): SIGTRAP + swbreak, and the
			 * PC-skip path treats it as the compiled-in entry
			 * (no Z0 patch lives at the poll site). */
			tf.esr = (0x3cUL << 26) | (unsigned long)GDB_BRK_IMM;
			tf.far = 0UL;
			for (unsigned int i = 0U; i < 30U; i++) {
				tf.x[i] = 0UL;
			}

			unget_char = '$';
			/* silent attach: any print here lands in gdb's RSP
			 * stream ahead of the handshake (board-measured) */
			gdb_sync_entry(&tf);
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

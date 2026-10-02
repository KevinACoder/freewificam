/*
 * @file   log.h
 * @brief  Console print sinks (os interface).
 *
 * Two outlets, per the print discipline carried over from the prior project
 * (SPEC v3 §3.4.2 - the only standard; a third outlet is a rework):
 *
 *  - board_log: kernel/driver/diagnostic lines. Stamped ("[   s.mmm] "),
 *    assembled into whole lines by the sink (module name goes in fmt, the
 *    stamp is the sink's business, a fragment without '\n' stays pending and
 *    concatenates with the next piece of the same line).
 *  - board_console_write_raw: interactive byte stream (prompts, echo), no
 *    stamp, same lock. A '\r' directly before a '\n' is dropped - the polled
 *    writer expands '\n' to CR LF itself.
 */

#ifndef FREEWEBCAM_LOG_H
#define FREEWEBCAM_LOG_H

#include <stdarg.h>

/* Formatted line output. Callers write '\n' only (never "\r\n"); a call
 * without the trailing '\n' leaves a pending fragment that the next write
 * joins - one line, one stamp, one CR LF on the wire. */
void board_log(const char *fmt, ...);

/* Raw bytes, unstamped, byte-exact (ANSI escapes survive). */
void board_console_write_raw(const char *data, unsigned int len);

/* Fault-path printer (N7-stab, 2026-10-02 sustained-TX freeze round): the
 * park/assert landers print through THIS, not board_log - no print lock
 * (a fault that interrupts a print would deadlock on the lock its own
 * interrupted context holds, and the freeze would be silent), no stamp,
 * a static buffer (a stack-overflow victim must not push another frame).
 * Interleaving with a live print is acceptable on a death line; silence
 * is not. Thread and ISR safe by construction (polled UART only). */
void board_log_emergency(const char *fmt, ...);

/* Switch the stamp source from the generic counter (cntvct) to the OS tick
 * count (SPEC v3 §3.4.3). One-way: called once, from main, after the tick
 * came up; the switch itself is announced by a caller log line. */
void board_log_stamp_use_tick(void);

/* Session gate: 1 while a gdb stub session owns the console - the stamped
 * sink drops its output, the raw outlet does not ask. Weak here (default
 * "not muted"), strong in the gdb glue. */
int board_console_muted(void);

/* 1 while some context holds the one-line print lock (N7-stab forensics:
 * a freeze with the lock held turns every later board_log into a spin, so
 * the fast probe checks this first and switches to the emergency outlet
 * instead of wedging itself on a dead holder). */
int board_console_lock_held(void);

/* Break-in seam for an interrupt-driven console (S4 §3.6.2): a 0x03 seen
 * by the shell's RX path asks the gdb stub first. Returns 1 = the byte was
 * consumed by the stub (break-in raised); 0 = no stub / not interested,
 * the byte is the shell's (readline's cancel-line). Weak here (no stub),
 * strong in the threadx gdb glue. */
int board_console_break_hook(void);

/* Boot-relative wall clock, the stamp's own source: seconds and leftover
 * milliseconds since the stamp epoch (first stamped line). One caller:
 * the shell's uptime command, which pins this against the kernel tick -
 * divergence between the two numbers is direct evidence of a dead or
 * racing tick. */
void board_uptime_parts(uint32_t *sec, uint32_t *ms);

#endif /* FREEWEBCAM_LOG_H */

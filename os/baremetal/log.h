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

/* Switch the stamp source from the generic counter (cntvct) to the OS tick
 * count (SPEC v3 §3.4.3). One-way: called once, from main, after the tick
 * came up; the switch itself is announced by a caller log line. */
void board_log_stamp_use_tick(void);

/* Session gate: 1 while a gdb stub session owns the console - the stamped
 * sink drops its output, the raw outlet does not ask. Weak here (default
 * "not muted"), strong in the gdb glue. */
int board_console_muted(void);

#endif /* FREEWEBCAM_LOG_H */

/*
 * @file   log.h
 * @brief  Console print sinks (os interface).
 *
 * Two outlets, one discipline: one line, one stamp, one CR LF on the wire.
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

/* Fault-path printer: the park/assert landers print through THIS, not
 * board_log - no print lock (a fault that interrupts a print would deadlock
 * on the lock its own interrupted context holds, and the freeze would be
 * silent), no stamp, a static buffer (a stack-overflow victim must not push
 * another frame). Interleaving with a live print is acceptable on a death
 * line; silence is not. Thread and ISR safe by construction (polled UART
 * only). */
void board_log_emergency(const char *fmt, ...);

/* Switch the stamp source from the generic counter (cntvct) to the OS tick
 * count. One-way: called once, after the tick came up; the switch itself is
 * announced by a caller log line. */
void board_log_stamp_use_tick(void);

#endif /* FREEWEBCAM_LOG_H */

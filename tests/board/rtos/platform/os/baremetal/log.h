/*
 * @file   log.h
 * @brief  Console print sinks (os interface).
 *
 * Two outlets, one discipline: one line, one stamp, one CR LF on the wire.
 *
 *  - LOG_* / log_emit: kernel/driver/diagnostic lines, one level per call. The
 *    level vocabulary is the vendored engine's (components/ulog): TRACE ..
 *    CRITICAL plus ALWAYS, with a single runtime threshold. The outlet still
 *    assembles whole lines (a fragment without '\n' stays pending and joins the
 *    next piece of the same line); the stamp is still the sink's business.
 *    LOG_ANCHOR is the never-filtered level: judged lines, ledger lines,
 *    protocol lines and fault lines are emitted at ALWAYS, so no runtime
 *    threshold can silence them.
 *  - board_console_write_raw: interactive byte stream (prompts, echo), no
 *    stamp, same lock. A '\r' directly before a '\n' is dropped - the polled
 *    writer expands '\n' to CR LF itself.
 *
 * The engine keeps one static message buffer and is not re-entrant, so the
 * print lock is taken at the entry point (log_emit) and the outlet subscriber
 * does not lock again.
 */

#ifndef FREEWIFICAM_LOG_H
#define FREEWIFICAM_LOG_H

#include <stdarg.h>

#include "ulog.h"

/* Default runtime level for the console outlet. Value-typed so a build rule can
 * override it; one place reads it (log.c). */
#ifndef LOG_DEFAULT_LEVEL
#define LOG_DEFAULT_LEVEL ULOG_INFO_LEVEL
#endif

/* Emit one line at a level. Formats the line, promotes it to ALWAYS when it
 * matches a never-filtered family, then hands it to the engine under the print
 * lock. The line discipline (whole-line assembly, CR/LF, repeat folding) is the
 * outlet's and is unchanged. */
void log_emit(ulog_level_t severity, const char *fmt, ...);

/* Current runtime threshold of the console outlet. */
ulog_level_t log_level_get(void);

/* Change the runtime threshold (the command face). Re-subscribes the outlet;
 * ALWAYS lines pass at any threshold. */
void log_level_set(ulog_level_t severity);

/* Entry macros. With ULOG_ENABLED undefined the whole family compiles to
 * nothing, arguments included - the compile-time master gate. */
#ifdef ULOG_ENABLED
#define LOG_TRACE(...)    log_emit(ULOG_TRACE_LEVEL,    __VA_ARGS__)
#define LOG_DEBUG(...)    log_emit(ULOG_DEBUG_LEVEL,    __VA_ARGS__)
#define LOG_INFO(...)     log_emit(ULOG_INFO_LEVEL,     __VA_ARGS__)
#define LOG_WARNING(...)  log_emit(ULOG_WARNING_LEVEL,  __VA_ARGS__)
#define LOG_ERROR(...)    log_emit(ULOG_ERROR_LEVEL,    __VA_ARGS__)
#define LOG_CRITICAL(...) log_emit(ULOG_CRITICAL_LEVEL, __VA_ARGS__)
/* Never-filtered: judged lines, one-boot ledger lines, protocol lines, fault
 * lines. */
#define LOG_ANCHOR(...)   log_emit(ULOG_ALWAYS_LEVEL,   __VA_ARGS__)
#else
#define LOG_TRACE(...)    do {} while (0)
#define LOG_DEBUG(...)    do {} while (0)
#define LOG_INFO(...)     do {} while (0)
#define LOG_WARNING(...)  do {} while (0)
#define LOG_ERROR(...)    do {} while (0)
#define LOG_CRITICAL(...) do {} while (0)
#define LOG_ANCHOR(...)   do {} while (0)
#endif

/* Compatibility alias for the pre-level call sites: emits at INFO. Retained so
 * the retrofit can proceed file by file; the level migration gate keeps the
 * remaining call count at zero. */
void board_log(const char *fmt, ...);

/* Raw bytes, unstamped, byte-exact (ANSI escapes survive). */
void board_console_write_raw(const char *data, unsigned int len);

/* Fault-path printer: the park/assert landers print through THIS, not
 * log_emit - no print lock (a fault that interrupts a print would deadlock
 * on the lock its own interrupted context holds, and the freeze would be
 * silent), no stamp, a static buffer (a stack-overflow victim must not push
 * another frame). Interleaving with a live print is acceptable on a death
 * line; silence is not. Thread and ISR safe by construction (polled UART
 * only). Outside the level plane on purpose: a fault line is never filtered. */
void board_log_emergency(const char *fmt, ...);

/* Switch the stamp source from the generic counter (cntvct) to the OS tick
 * count. One-way: called once, after the tick came up; the switch itself is
 * announced by a caller log line. */
void board_log_stamp_use_tick(void);

#endif /* FREEWIFICAM_LOG_H */

/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    fw_log.h
 * @brief   Console logging: one line, one level, one sink.
 *
 * This header forwards the image's real log interface
 * (`tests/board/rtos/platform/os/baremetal/log.h`) and the level vocabulary of
 * the vendored engine (`third-party/ulog/src/ulog.h`).  Both are shipped under
 * `freewificam/deps/` by the sdk target so the declarations cannot drift from
 * the objects in the archives.
 *
 * The stable surface is deliberately small:
 *
 *   LOG_TRACE / LOG_DEBUG / LOG_INFO / LOG_WARNING / LOG_ERROR / LOG_CRITICAL
 *       One formatted line at a level.  The runtime threshold is per image.
 *   LOG_ANCHOR
 *       The never-filtered level.  Judged lines, protocol lines and fault
 *       lines use it so no threshold can silence them.
 *   log_emit(level, fmt, ...)
 *       The function behind the macros; for a caller that carries the level
 *       in a variable.
 *   log_level_get() / log_level_set(level)
 *       Read and change the current threshold.
 *   board_console_write_raw(data, len)
 *       Byte stream without a stamp (interactive prompts and echo).
 *   board_log_emergency(fmt, ...)
 *       Fault-path printer: no lock, no stamp, static buffer; safe when a
 *       fault interrupts a print.
 *   board_log_stamp_use_tick()
 *       Switch the stamp source from the generic counter to the OS tick.
 *
 * Contract details a consumer must know:
 *
 *   - The sink assembles whole lines.  A call without '\n' stays pending and
 *     joins the next piece; a call with '\n' emits one line.  '\r' is dropped:
 *     the sink owns CR LF.  Callers write '\n' only.
 *   - The engine keeps one static message buffer and is not re-entrant; the
 *     entry point takes the print lock.  `board_console_write_raw` shares that
 *     lock.  Do not call the log from inside the sink callback or from a fault
 *     context: use `board_log_emergency` for the latter.
 *   - `LOG_*` compile to nothing when the image has `ULOG_ENABLED` undefined;
 *     the arguments are not evaluated.  Do not put side effects in a log call.
 *   - The link needs the log object plus its support closure:
 *     libfw_osal_cmsis2 (log, tick, printf bindings), libfw_support (uLog and
 *     the printf component), libfw_board_<board> (UART), libfw_arch (generic
 *     timer) and libc.  `pkg-config --static --libs freewificam` names the
 *     whole closure; see docs/PORTING.md.
 *
 * Nothing above is new implementation: the header is the component's, the
 * objects are the ones the image already links.
 */

#ifndef FREEWIFICAM_FW_LOG_H
#define FREEWIFICAM_FW_LOG_H

/* The level enum and names (ulog.h), then the image entry points (log.h). */
#include "deps/ulog.h"
#include "deps/log.h"

#endif /* FREEWIFICAM_FW_LOG_H */

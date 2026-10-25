/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    trace/define_trace.h
 * @brief   The second pass a trace-event header includes to generate its
 *          tracepoint definitions.
 *
 * @design declaration-only compatibility face. The interface (include/trace/
 *         define_trace.h) re-includes the trace-event header with
 *         TRACE_HEADER_MULTI_READ defined so the same text generates the
 *         tracepoint records and the trace_* entry points - but only in the one
 *         translation unit that defined CREATE_TRACE_POINTS. This port builds no
 *         tracepoint instrumentation, and that unit is not part of the compiled
 *         set, so the pass is empty here: the header exists because a trace-event
 *         header includes it unconditionally at its end, and the definitions it
 *         would generate are not reached. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_TRACE_DEFINE_TRACE_H_
#define _LINUXKPI_TRACE_DEFINE_TRACE_H_

#include "../linuxkpi_core.h"

#endif /* _LINUXKPI_TRACE_DEFINE_TRACE_H_ */

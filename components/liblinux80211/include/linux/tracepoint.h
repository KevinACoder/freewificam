/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/tracepoint.h
 * @brief   The trace-event declaration vocabulary a driver's trace headers are
 *          written in, for a build that carries no tracepoint instrumentation.
 *
 * @design declaration-only compatibility face. The interface's header
 *         (include/linux/tracepoint.h) owns the whole tracepoint machinery; a
 *         trace-event header names it and then either defines real tracepoints
 *         (the one translation unit with CREATE_TRACE_POINTS) or declares the
 *         trace_* entry points as empty. This port builds no instrumentation and
 *         that translation unit is not part of the compiled set, so the reachable
 *         path is the empty one: the parameter-list spellings expand to the
 *         interface's own text and the event-definition spellings are the empty
 *         stubs the interface's own build uses when tracing is off (the vendored
 *         trace header redefines them itself, which is why the names must exist
 *         for the #undef to be well formed). The instrumentation spellings
 *         (__field, __string, the dynamic-array family, __entry) appear only in
 *         the discarded argument positions of that empty path, so they are not
 *         stated. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_TRACEPOINT_H_
#define _LINUXKPI_LINUX_TRACEPOINT_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/tracepoint.h: the parameter-list spellings, which the empty
 * event-definition path still expands because the generated stub takes the
 * parameter list. */
#define	TP_PROTO(args...)	args
#define	TP_ARGS(args...)	args

/* [LNX] include/linux/tracepoint.h: the event-definition spellings at their
 * tracing-off shape (an empty trace_* stub). */
#define	TRACE_EVENT(name, proto, args...)				\
	static inline void trace_##name(proto) {}
#define	DECLARE_EVENT_CLASS(name, proto, args...)
#define	DEFINE_EVENT(evt_class, name, proto, args...)			\
	static inline void trace_##name(proto) {}

#endif /* _LINUXKPI_LINUX_TRACEPOINT_H_ */

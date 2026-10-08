/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linuxkpi_impl.h
 * @brief   The implementation side of the linuxkpi contract: the fixed-size shells the
 *          contract's opaque storage slots must hold, the component's start-up call and
 *          its log sink.
 *
 * @design adapter with fixed-size shells - the contract header
 *         (components/linuxkpi/linuxkpi_core.h) declares its objects as opaque byte
 *         arrays whose slot sizes are pinned by comments there, so the implementation
 *         must fit exactly and say so at compile time. The shells keep their CMSIS-RTOS2
 *         object plus the bookkeeping the Linux semantics need; nothing is allocated for
 *         a lock, a condition or a timer.
 *         Two seams are declared here rather than reached for directly, because the same
 *         body serves a board and a host build:
 *           - `lkpi_core_init()` is called by whoever starts the image, after the kernel
 *             is up (the system workqueue's thread is created there);
 *           - `lkpi_log_sink_register()` installs the line sink: the board layer
 *             registers its stamped console sink, a host build registers stdio, and a
 *             build that registers nothing drops the lines. Reference for the shape:
 *             components/linuxkpi/usb/lkpi_usb_backend.h:1-45 (the component's other
 *             narrow seam).
 * @date    08.10.2026
 */

#ifndef _LINUXKPI_IMPL_H_
#define _LINUXKPI_IMPL_H_

#include <stdint.h>
#include <stddef.h>

#include "os/cmsis2/cmsis2.h"

/*
 * Shells. The sizes are the contract's: mutex 64 bytes (struct mutex._storage),
 * condition 96 (struct completion._cv, struct wait_queue_head._cv), callout 112
 * (struct timer_list._callout, struct delayed_work._callout).
 */
struct lkpi_mutex_shell {
	osMutexId_t	id;
	uint32_t	inited;
	uint32_t	locked;		/* bookkeeping for mutex_is_locked() */
	uint64_t	_reserved[6];
};

struct lkpi_cv_shell {
	osSemaphoreId_t	sem;
	uint32_t	waiters;
	uint32_t	inited;
	uint64_t	_reserved[10];
};

struct lkpi_callout_shell {
	osTimerId_t	timer;
	void		*owner;		/* the object the callback belongs to */
	uint32_t	inited;
	uint32_t	pending;
	uint64_t	_reserved[11];
};

_Static_assert(sizeof(struct lkpi_mutex_shell) == 8 * sizeof(void *),
	       "the mutex shell must fill struct mutex._storage exactly");
_Static_assert(sizeof(struct lkpi_cv_shell) == 12 * sizeof(void *),
	       "the condition shell must fill the _cv slot exactly");
_Static_assert(sizeof(struct lkpi_callout_shell) == 14 * sizeof(void *),
	       "the callout shell must fill the _callout slot exactly");

/*
 * Start the component's services: creates the system workqueue and its worker thread.
 * Call it once, from a task, after the kernel is running and before any driver work.
 */
void lkpi_core_init(void);

/*
 * Install the line sink. The component formats a complete line (with its trailing
 * newline) and hands it over; the sink owns stamping and the wire discipline. Passing
 * NULL removes the sink and silences the component.
 */
void lkpi_log_sink_register(void (*sink)(const char *line));

#endif /* _LINUXKPI_IMPL_H_ */
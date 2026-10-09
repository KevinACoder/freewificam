/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/interrupt.h
 * @brief   The deferred-callback object a driver's transmit path arms, as the Linux
 *          interrupt header names it.
 *
 * @design declaration-only compatibility face, grown per consumer. The interface
 *         (include/linux/interrupt.h) carries the whole IRQ and softirq surface; the
 *         only part the vendored bytes reach is the tasklet, which is the interface's
 *         small "run this later in softirq context" object. The object's shape is
 *         transcribed field for field (name, type and order) so a driver that embeds it
 *         by value lays out what upstream lays out. The operations are declarations
 *         only: deferred execution on this port is the port's mechanism (the component's
 *         deferred-work seam is the workqueue in the contract header), and none of the
 *         tasklet call sites in the compiled set actually need a tasklet to fire - so
 *         tasklet_init/tasklet_kill/tasklet_hi_schedule are left undefined rather than
 *         given an inert body that would silently swallow a scheduled callback. The
 *         contract header already states the identifier `tasklet_schedule` as a no-op
 *         macro (its own earlier decision for that one call site); a function of the
 *         same name here would be rewritten by that macro, so this header does not
 *         restate it. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_INTERRUPT_H_
#define _LINUXKPI_LINUX_INTERRUPT_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/interrupt.h:19 includes <linux/atomic.h> (the tasklet record
 * carries an atomic count); the face keeps the interface's own include. */
#include <linux/atomic.h>

/* [LNX] include/linux/interrupt.h: the two state bits tasklet_trylock/unlock maintain
 * on tasklet_struct.state. */
enum {
	TASKLET_STATE_SCHED,	/* Tasklet is scheduled for execution */
	TASKLET_STATE_RUN	/* Tasklet is running (SMP only) */
};

/* [LNX] include/linux/interrupt.h:691-702, transcribed field for field. `count` is the
 * atomic_t the contract header defines; the func/callback pair is the interface's union
 * (the modern .callback form and the legacy .func form share the slot). */
struct tasklet_struct {
	struct tasklet_struct	*next;
	unsigned long		state;
	atomic_t		count;
	bool			use_callback;
	union {
		void (*func)(unsigned long data);
		void (*callback)(struct tasklet_struct *t);
	};
	unsigned long		data;
};

/* [LNX] include/linux/interrupt.h:802-805: the operations, with the interface's exact
 * signatures. No bodies: arming, killing and running a deferred callback is the port's
 * scheduler work, and a caller that reaches these must be given it. */
extern void tasklet_init(struct tasklet_struct *t,
			 void (*func)(unsigned long), unsigned long data);
extern void tasklet_kill(struct tasklet_struct *t);
extern void tasklet_hi_schedule(struct tasklet_struct *t);

#endif /* _LINUXKPI_LINUX_INTERRUPT_H_ */

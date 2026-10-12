/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/interrupt.h
 * @brief   The deferred-callback object a driver arms from an interrupt context, as
 *          the Linux interrupt header names it.
 *
 * @design the object of a declaration-only compatibility face, over the contract
 *         header's deferred-work seam. The interface (include/linux/interrupt.h)
 *         carries the whole IRQ and softirq surface; the only part the vendored
 *         bytes reach is the tasklet, the interface's small "run this later in
 *         softirq context" object. Its shape is transcribed field for field (name,
 *         type and order) so a driver that embeds it by value lays out what
 *         upstream lays out - plus a first-party tail behind those fields (the
 *         work item that carries the callback to the seam), which no upstream
 *         field's offset depends on.
 *
 *         What the mechanism is: the contract header's deferred-work seam
 *         (lkpi_tasklet_schedule in linuxkpi_core.c, "deferred callbacks"), where a
 *         tasklet is a coalescing work item and the worker thread is the softirq
 *         context. The earlier reading of this face - "none of the tasklet call
 *         sites in the compiled set actually need a tasklet to fire", which left
 *         the operations undefined and the arm a no-op macro - stopped holding when
 *         the SDIO vendor driver landed: its receive path arms the tasklet from the
 *         bus interrupt handler and drains the pending receive buffers nowhere else
 *         (hal/rtl8188f/sdio/sdio_ops.c:1466-1468 arms it, rtl8189fs_recv.c:54-58
 *         is the only drain), so a no-op arm stops the drain and the pool of 8
 *         receive buffers empties permanently. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
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
 * (the modern .callback form and the legacy .func form share the slot). The trailing
 * member is this port's: the work item the arm queues (see the @design note).
 * container_of walks back over it, so it must stay last. */
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
	struct work_struct	work;	/* the port's slot on the seam */
};

/* [LNX] include/linux/interrupt.h:802-805: the operations, with the interface's exact
 * signatures. The bodies are the deferred-work seam's (linuxkpi_core.c); the library
 * header states the arm as a macro over the same body, so both spellings reach one
 * mechanism. */
extern void tasklet_init(struct tasklet_struct *t,
			 void (*func)(unsigned long), unsigned long data);
extern void tasklet_kill(struct tasklet_struct *t);
extern void tasklet_hi_schedule(struct tasklet_struct *t);

#endif /* _LINUXKPI_LINUX_INTERRUPT_H_ */

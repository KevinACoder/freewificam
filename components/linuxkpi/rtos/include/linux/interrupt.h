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

/* ------------------------------------------------------------------ */
/* the interrupt-request surface                                       */
/* ------------------------------------------------------------------ */

/* [LNX] include/linux/irqreturn.h:8-16: what a handler returns. The interface
 * spells this an enum; the contract header already carries IRQ_NONE and
 * IRQ_HANDLED as the value macros every existing consumer compares against, so
 * this face keeps those and states the third value beside them. */
#ifndef IRQ_WAKE_THREAD
#define	IRQ_WAKE_THREAD		2
#endif
typedef int irqreturn_t;

/* [LNX] include/linux/interrupt.h:30: the handler signature. */
typedef irqreturn_t (*irq_handler_t)(int irq, void *dev_id);

/* [LNX] include/linux/interrupt.h:41-56: the flags this face names. A driver that
 * registers a shared or a one-shot line passes one of these. */
#define	IRQF_SHARED		0x00000080
#define	IRQF_ONESHOT		0x00002000
#define	IRQF_NO_AUTOEN		0x00004000

/* [LNX] include/linux/interrupt.h:154-160: the request/release pair. `irq` is the
 * interrupt the engine allocated (a pci_dev's .irq); the engine routes the
 * handler to the backend's registered vector. free_irq's dev_id must be the same
 * pointer the request named. */
int	request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags,
	    const char *name, void *dev);
void	free_irq(unsigned int irq, void *dev_id);

/*
 * [LNX] include/linux/hardirq.h:13: `extern void synchronize_irq(unsigned int
 * irq);` - wait until no handler is running on that line. The interface's home for
 * it is linux/hardirq.h (which linux/interrupt.h includes); this face keeps it
 * beside the request/release pair. The body is in linuxkpi_compat.c: this port's
 * handler runs to completion before the engine returns from the interrupt, so
 * there is no in-flight handler to wait for and the wait is empty.
 */
void	synchronize_irq(unsigned int irq);

/* [LNX] include/linux/interrupt.h: the affinity-hint setter. The interface selects
 * between a CONFIG_SMP arm and the !CONFIG_SMP arm at include/linux/interrupt.h:407,
 * which is `return -EINVAL` - this port runs one CPU and has no IRQ affinity to
 * program, so that is the reachable arm and its body is carried verbatim. The
 * parameter type is the interface's (struct cpumask, forward-declared here; the
 * caller passes NULL to clear the hint). */
struct cpumask;

static __always_inline __unused int
irq_set_affinity_hint(unsigned int irq, const struct cpumask *m)
{
	(void)irq;
	(void)m;
	return -EINVAL;
}

#endif /* _LINUXKPI_LINUX_INTERRUPT_H_ */

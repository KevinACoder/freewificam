/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/irq_work.h - the deferred irq-work record, declaration-only face.
 *
 * The pinned header defines the per-CPU irq_work queue and its entry points.
 * This port has no such queue: the compiled set only names the record (an
 * embedded struct) and irq_work_queue(), whose body belongs to the port's
 * deferred-work seam (S1c).  A caller that reaches it before then gets an
 * unresolved symbol rather than a silent inline no-op.
 */
#ifndef _LINUXKPI_LINUX_IRQ_WORK_H_
#define _LINUXKPI_LINUX_IRQ_WORK_H_

#include "../../linuxkpi_core.h"

struct irq_work {
	void	(*func)(struct irq_work *work);
};

static __always_inline __unused void
init_irq_work(struct irq_work *work, void (*func)(struct irq_work *))
{
	work->func = func;
}

void irq_work_queue(struct irq_work *work);
void irq_work_sync(struct irq_work *work);

#endif /* _LINUXKPI_LINUX_IRQ_WORK_H_ */

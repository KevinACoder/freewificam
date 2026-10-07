/*
 * @file
 * @brief workqueue(9) shell: one worker thread per workqueue, fixed
 * callback pair (the driver callback takes the enqueued work item and
 * the workqueue context).
 */

#ifndef _COMPAT_SYS_WORKQUEUE_H_
#define _COMPAT_SYS_WORKQUEUE_H_

#include <sys/cdefs.h>
#include <sys/intr.h>

#ifndef PRI_NONE
#define PRI_NONE 0
#endif

struct work {
	struct work *w_qnext; /* chain inside its workqueue */
};

struct workqueue;

typedef void (*workqueue_func_t)(struct work *, void *);

int workqueue_create(struct workqueue **wqp, const char *name,
	workqueue_func_t func, void *arg, int pri, int ipl, int flags);
void workqueue_enqueue(struct workqueue *wq, struct work *wk, void *cpu);
void workqueue_destroy(struct workqueue *wq);

#endif /* _COMPAT_SYS_WORKQUEUE_H_ */

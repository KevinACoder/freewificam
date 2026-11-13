/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtos_stats.h
 * @brief  Per-task run-time snapshot for the freeze watchdog.
 *
 * The SDIO engine's state says where a transfer stopped; it cannot say who was
 * on the core instead.  This is that reading: every thread the kernel knows,
 * with its cumulative run time on the board's architectural counter.  Two
 * snapshots a second apart turn into "which thread owned the core while the
 * pump sat READY".
 *
 * The face is plain C on purpose: the board cases compile in the wireless
 * stack's flag world and must not pull the kernel's headers in.  The backend
 * that owns the kernel answers; a backend without run-time accounting returns
 * zero, which is an honest "no instrument" rather than a fake idle.
 */

#ifndef RTOS_STATS_H
#define RTOS_STATS_H

#include <stdint.h>

/* eTaskState's own order; a reader maps them to words. */
#define RTOS_TASK_RUNNING	0u
#define RTOS_TASK_READY		1u
#define RTOS_TASK_BLOCKED	2u
#define RTOS_TASK_SUSPENDED	3u
#define RTOS_TASK_DELETED	4u
#define RTOS_TASK_INVALID	5u

struct rtos_task_stat {
	const char	*name;		/* the task's own name, or "?" */
	uint32_t	number;		/* unique task number (names may repeat) */
	uint32_t	runtime;	/* cumulative counter, 24 MHz ticks */
	uint32_t	state;		/* RTOS_TASK_* */
	uint32_t	prio;		/* the kernel's current priority */
	uint32_t	stack_free;	/* high-water free, bytes */
	void		*handle;	/* the kernel handle (for the PC sampler) */
};

/*
 * The tick sampler's answer: where in the image a task was when the tick
 * interrupted it.  A task that spins shows one or two PCs with almost every
 * sample; a task that blocks shows few or none.  The addresses are image
 * addresses (symbolise with the ELF the boot ran).
 */
struct rtos_pc_hit {
	uint64_t	pc;
	uint32_t	count;
};

/* Top sampled PCs for one task, highest count first; returns the number
 * written (0 when the sampler is off or the task was never sampled). */
unsigned int rtos_pc_top(void *task, struct rtos_pc_hit *out, unsigned int max);

/* The PC a task was switched out at: the ELR saved at the top of its context
 * frame.  Deterministic where the tick sampler is statistical - use it to name
 * the instruction a spinning task left.  0 when the handle is unknown. */
uint64_t rtos_task_saved_pc(void *task);

/* The newest PC sample for a task: where it was the last time the tick saw it.
 * The histogram answers "where did it spend the second"; this answers "where
 * was it when the freeze was declared". */
uint64_t rtos_pc_last(void *task, uint32_t *ago_ms);

/* Scan a task's saved context frame for words that look like text addresses.
 * The AArch64 port saves the ELR and x30 in that frame; a frame-pointer walk is
 * not available for every build, but the return addresses are still there. */
unsigned int rtos_task_frame_scan(void *task, uint64_t *vals, uint32_t *offs,
				  unsigned int max);

/* Raw words from the saved frame at a byte offset: the scan filter says "no
 * text address in the first 0x300", so the next step is to look at the words
 * themselves and find where the port put the ELR. */
unsigned int rtos_task_frame_raw(void *task, uint32_t off, uint64_t *out,
				 unsigned int n);

/* Fills up to `max` entries, returns how many were written (0 when this backend
 * has no run-time accounting).  Callable from a thread; the values are read
 * racily by design - the running task's own total is only complete after its
 * next switch. */
unsigned int rtos_task_stats_get(struct rtos_task_stat *out, unsigned int max);

#endif /* RTOS_STATS_H */

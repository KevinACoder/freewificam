/* SPDX-License-Identifier: GPL-2.0 */
/*
 * metal_kernel.h - cooperative kernel core for the baremetal CMSIS2 backend.
 *
 * There is no tick and no preemption: a thread runs until it blocks, yields or
 * terminates, and the scheduler loop only runs between those points. Time is
 * the generic counter (CNTVCT, 24 MHz on RK3568) exposed as microseconds; the
 * one-shot virtual timer is armed for the next deadline only while the CPU
 * would otherwise idle.
 *
 * The wait lists are intrusive singly linked lists of thread control blocks,
 * headed by a "void *" slot owned by the object (semaphore, queue, flags...).
 * Everything that touches a list or a thread state must hold IRQs masked; the
 * object files do that through metal_irq_save/metal_irq_restore, which also
 * serve osCriticalEnter/osCriticalExit.
 */

#ifndef METAL_KERNEL_H
#define METAL_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#if !defined(CMSIS2_MAX_THREADS)
#define CMSIS2_MAX_THREADS 24
#endif

#if !defined(CMSIS2_MAX_TIMERS)
#define CMSIS2_MAX_TIMERS 128
#endif

/* wake status codes, deliberately the CMSIS-RTOS2 osStatus_t values */
#define METAL_OK 0
#define METAL_ERROR 1
#define METAL_TIMEOUT 2
#define METAL_RESOURCE 3

enum metal_thread_state {
	METAL_INACTIVE = 0,
	METAL_READY,
	METAL_RUNNING,
	METAL_BLOCKED,
	METAL_TERMINATED,
};

struct metal_thread {
	uint64_t *sp;		/* saved callee-saved frame */
	void *stack;		/* allocation base (for the heap) */
	void *stack_top;
	uint32_t stack_size;
	const char *name;
	uint8_t prio;		/* current (boosted) priority */
	uint8_t base_prio;
	uint8_t state;
	uint8_t used;
	uint32_t flags;		/* thread flags */
	uint32_t flags_wait;
	uint32_t flags_opt;
	uint32_t flag_result;
	void *wait_head;	/* list we are queued on, NULL when not queued */
	struct metal_thread *wait_next;
	uint64_t timeout_us;	/* absolute deadline, 0 = none */
	int wake_status;
};

typedef void (*metal_fn_t)(void *);

struct metal_timer {
	metal_fn_t fn;
	void *arg;
	uint32_t period_ms;
	uint64_t due_us;
	uint8_t periodic;
	uint8_t active;
	uint8_t used;
};

extern struct metal_thread *metal_current;

/* core */
void metal_kernel_init(void);
void metal_kernel_start(void) __attribute__((noreturn));
void metal_kernel_heartbeat(void);

uint64_t metal_now_us(void);
uint32_t metal_now_ms(void);

void metal_irq_enable(void);
void metal_irq_disable(void);
uint32_t metal_irq_save(void);
void metal_irq_restore(uint32_t state);

/* scheduling */
void metal_yield(void);
int metal_block_on(void *wait_head, uint64_t timeout_us);
int metal_block_until(void *wait_head, uint64_t deadline_us);
int metal_block_when(void *wait_head, uint64_t deadline_us,
		     int (*cond)(void *), void *arg);
void metal_wake_one(void *wait_head, int status);
void metal_wake_all(void *wait_head, int status);
void metal_unlink(struct metal_thread *t);

struct metal_thread *metal_thread_create(metal_fn_t fn, void *arg,
					 const char *name, int prio,
					 size_t stack_size);
int metal_thread_terminate_id(struct metal_thread *t);
void metal_thread_terminate_self(void) __attribute__((noreturn));
struct metal_thread *metal_self(void);
struct metal_thread *metal_thread_find(const char *name);
uint32_t metal_thread_count(void);

/* software timers, run from the scheduler loop (never from an IRQ) */
struct metal_timer *metal_timer_alloc(void);
int metal_timer_start(struct metal_timer *t, uint32_t ms);
int metal_timer_stop(struct metal_timer *t);
void metal_timer_release(struct metal_timer *t);

/* heap: one TLSF arena in .bss (metal_memory.c) */
void metal_heap_init(void);
void *metal_heap_alloc(size_t size);
void metal_heap_free(void *p);
void metal_heap_stats(size_t *free_bytes, size_t *min_free, size_t *total,
		      uint32_t *fails);

#endif /* METAL_KERNEL_H */
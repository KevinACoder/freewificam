/**
 * @file    cmsis_os2_ext.h
 * @brief   Critical-section extension of the CMSIS-RTOS2 facade.
 * @author  zhugengyu
 * @date    07.10.2026
 *
 * @design adapter - ref linux:kernel/locking/spinlock.c:88 (the irqsave take/release
 *         pair whose state is the caller's saved flag word)
 *
 * CMSIS-RTOS2 has no interrupt-masking primitive, but components that move data
 * between a submitter and a completion context need one: a short section that
 * excludes every other context from an object - the done-list hand-off and the
 * endpoint state updates of the linuxkpi USB engine are the first users.
 *
 * Contract:
 *   - osCriticalEnter() returns an opaque saved state; the matching
 *     osCriticalExit() restores exactly that state. Nesting is legal and
 *     per-context (a nested take restores the outer state).
 *   - The section is short and may not wait on anything: no CMSIS blocking
 *     call, no sleep, no allocation. It may be entered from a completion
 *     context, which the backend must accept.
 *   - What "critical" means is the backend's business, and the two backends
 *     differ by construction:
 *       - ThreadX: interrupts masked (tx_interrupt_control), the exact
 *         single-core semantics the engine was ported against;
 *       - FreeRTOS host (POSIX port): threads masking signals cannot exclude
 *         other pthreads, so the section is a process-wide recursive lock
 *         (thread-local nesting depth around one pthread mutex). Same
 *         exclusion, no interrupt latency claim - a recorded host divergence.
 */

#ifndef CMSIS_OS2_EXT_H
#define CMSIS_OS2_EXT_H

#include <stdint.h>
#include <stddef.h>

typedef uint32_t os_critical_state_t;

os_critical_state_t osCriticalEnter( void );
void osCriticalExit( os_critical_state_t state );

/**
 * @brief   The heap, behind the facade like every other kernel service.
 *
 * CMSIS-RTOS2 does not define an allocator, but components above the port layer need
 * one (a driver's descriptor blocks, buffers and objects all come from the heap), and
 * the charter keeps kernel APIs inside their own port layer: this pair is the one
 * allocator entry the components see, implemented per backend over the heap that
 * backend actually has.
 *
 * Contract: osMemoryAlloc() returns NULL when the heap is exhausted (it never waits),
 * the memory is at least word-aligned, and osMemoryFree() accepts exactly the pointers
 * osMemoryAlloc() returned. Neither call may be made before the kernel is up on a
 * backend whose heap lives inside the kernel (the FreeRTOS one does).
 */
void * osMemoryAlloc( size_t size );
void   osMemoryFree( void * ptr );

/**
 * @brief   Per-thread execution-time profile (measurement builds only).
 *
 * The wired-throughput rounds need "which thread eats the single core" as a
 * first-class reading, not an inference. CMSIS-RTOS2 has no such query; the
 * ThreadX backend answers it from the upstream Execution Profile Kit when the
 * build enables it (TX_EXECUTION_PROFILE_ENABLE, the EPK=1 lever), and every
 * other backend answers with an empty profile so the caller needs no
 * per-kernel switch.
 *
 * Contract: fills up to `max` entries with the created threads' names and
 * accumulated execution time in the time source's units (the ThreadX board
 * source is CNTVCT: 24 MHz ticks; the shipped build carries it because WFI
 * stops PMCCNTR and this board's idle genuinely busy-polls, so the PMU route
 * is closed). `*isr_ticks` / `*idle_ticks` receive the ISR and idle
 * accumulators (0 when unknown). Returns the number of entries written. The
 * sums are monotonic since kernel entry and are only meaningful when the
 * profile kit is compiled in; an empty result plus zero totals means the
 * build has no profile, not that nothing ran. Callable from a thread; the
 * EPK's per-thread totals are read racily by design (the running thread's
 * own total is only complete after its next switch). */
#define OS_EXEC_PROFILE_MAX_ENTRIES	16

typedef struct {
	const char *name;	/* NULL when the slot is unused */
	uint64_t    ticks;	/* accumulated execution time, source units */
} os_exec_profile_entry_t;

size_t osExecProfileGet(os_exec_profile_entry_t *entries, size_t max,
			uint64_t *isr_ticks, uint64_t *idle_ticks);

/**
 * @brief   The name a thread was created with, or NULL when unknown.
 *
 * A hang report is only useful if it can name the thread that did not come
 * back. CMSIS-RTOS2 has no name query (the attribute is write-only), so the
 * backend answers from the record it already keeps. `thread_id` is the value
 * osThreadGetId()/osThreadNew() returned; any other value yields NULL. The
 * returned pointer is the backend's own storage and stays valid for the
 * thread's lifetime. Backends that keep no names may answer NULL - callers
 * must treat the name as optional.
 */
const char * osThreadGetName( void * thread_id );

#endif /* CMSIS_OS2_EXT_H */

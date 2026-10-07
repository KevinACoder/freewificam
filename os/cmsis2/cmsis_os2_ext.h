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

typedef uint32_t os_critical_state_t;

os_critical_state_t osCriticalEnter( void );
void osCriticalExit( os_critical_state_t state );

#endif /* CMSIS_OS2_EXT_H */

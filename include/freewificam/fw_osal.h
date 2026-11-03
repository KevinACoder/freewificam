/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    fw_osal.h
 * @brief   The operating-system ABI the libraries are built against.
 *
 * The core does not name an RTOS.  Everything it needs from the host is the
 * CMSIS-RTOS2 API plus the small project extension in `cmsis_os2_ext.h`
 * (critical section, heap, thread names, execution profile).  The board image
 * satisfies this ABI with the vendored FreeRTOS or ThreadX adapter; an
 * external RTOS supplies its own CMSIS-RTOS2 implementation and the shim for
 * the extension.  None of these calls is implemented by the freewificam
 * libraries in this checkpoint, so a consumer that calls them links them from
 * its own OS, not from the SDK:
 *
 *   FreeRTOS   third-party/CMSIS-FreeRTOS  (the board image's backend)
 *   ThreadX    tests/board/rtos/platform/os/threadx/cmsis2_threadx.c
 *   Zephyr     subsys/portability/cmsis_rtos_v2 (planned, L2)
 *   RT-Thread  a hand-written shim            (planned, L4)
 *
 * What is in this header, verbatim from the two component headers:
 *
 *   - threads, delays, ticks, mutexes/semaphores/message queues, timers
 *     (the CMSIS-RTOS2 surface; `cmsis_os2.h`),
 *   - `osCriticalEnter`/`osCriticalExit`: a short, non-blocking exclusion
 *     section that also excludes completion contexts; the saved state is
 *     restored exactly, nesting is legal,
 *   - `osMemoryAlloc`/`osMemoryFree`: the one allocator the components above
 *     the port see; NULL means exhausted, never a wait,
 *   - the observation extensions (heap gauges, thread names, execution
 *     profile) that measurement builds use.
 *
 * What is NOT here: an interrupt API, a bus API, timers built on the cycle
 * counter, or a board clock API.  Those are platform/board concerns and stay
 * outside the OS-neutral contract.
 *
 * The stable names are the CMSIS-RTOS2 ones and the four extensions.  The
 * rest of `cmsis_os2_ext.h` is observation-only and may answer zeros on a
 * backend that has no such facility (documented in that header); a consumer
 * must treat those readings as optional.
 */

#ifndef FREEWIFICAM_FW_OSAL_H
#define FREEWIFICAM_FW_OSAL_H

/* Forwarded component headers, installed under freewificam/deps/ by the sdk
 * target.  They are the real declarations; this wrapper does not copy or
 * re-spell them, so the prototypes cannot drift from the implementations. */
#include "deps/cmsis_os2.h"
#include "deps/cmsis_os2_ext.h"

#endif /* FREEWIFICAM_FW_OSAL_H */

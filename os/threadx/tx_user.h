/*
 * @file   tx_user.h
 * @brief  Adapter-owned shadow header for the ThreadX kernel (SPEC S3
 *         threadx-up-boot §3.5.3).
 *
 * The kernel force-includes this file into every translation unit via
 * -DTX_INCLUDE_USER_DEFINE_FILE (tx_port.h). There is no generated config.h
 * in this repo: the knobs arrive as -D from the Makefile (configs/*.config
 * fragments are the source of truth) and are re-exported here under their
 * kernel names; a missing key is a build error, not a silent default.
 *
 * Anything not listed here stays undefined and takes the port default
 * (cortex_a55/gnu inc/tx_port.h).
 */

#ifndef OS_THREADX_TX_USER_H
#define OS_THREADX_TX_USER_H

/* --- keys the Makefile must inject ---------------------------------------- */

#ifndef CONFIG_TX_TICKS_PER_SECOND
#error CONFIG_TX_TICKS_PER_SECOND not injected (configs/threadx-*.config)
#endif
#ifndef CONFIG_TX_TIMER_STACK
#error CONFIG_TX_TIMER_STACK not injected (configs/threadx-*.config)
#endif

/* --- kernel knobs ---------------------------------------------------------- */

/* Tick rate. 1000 Hz by user adjudication 2026-09-30 (spec open question 1):
 * the CherryUSB osal hard-assumes it and osDelay ticks read as milliseconds.
 * The baremetal config keeps its own 100 Hz - the two never mix. */
#define TX_TIMER_TICKS_PER_SECOND	CONFIG_TX_TICKS_PER_SECOND

/* Kernel timer-thread stack. The port default happens to equal the key
 * default; carrying the key keeps the growth path one flag away (the prior
 * project's 16384 was a net80211-callout lesson, not a bare-bones need). */
#define TX_TIMER_THREAD_STACK_SIZE	CONFIG_TX_TIMER_STACK

/* Stack overflow becomes a named culprit instead of a silent hang. The
 * notify handler lives in tx_glue.c and parks after printing - it must
 * exist whenever this is on. */
#define TX_ENABLE_STACK_CHECKING

/* CMSIS slot backlink (S4 cherrysh 前置, spec §3.5.3): one extra pointer per
 * thread, letting the ISR-side wakeup path find its CMSIS slot without a
 * table walk. Consumed from T3 (cmsis2_threadx.c) on. */
#define TX_THREAD_USER_EXTENSION	VOID *tx_thread_cmsis_slot;

/* Deliberately undefined this round (each with its spec anchor):
 *   TX_MAX_PRIORITIES       - port default 32, the CMSIS band map assumes it
 *   TX_ENABLE_WFI           - upstream default off (open question 2); idle is
 *                             a busy poll until re-adjudicated
 *   ENABLE_ARM_FP           - -mgeneral-regs-only: there is no FP state
 *   TX_ENABLE_IRQ/FIQ_NESTING, TX_ENABLE_FIQ_SUPPORT, TX_DISABLE_ERROR_CHECKING,
 *   TX_ENABLE_EVENT_TRACE, TX_ENABLE_EXECUTION_CHANGE_NOTIFY,
 *   TX_MISRA_ENABLE         - all stay off (spec §3.5.3 table) */

#endif /* OS_THREADX_TX_USER_H */

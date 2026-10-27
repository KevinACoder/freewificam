/*
 * @file    tx_user.h
 * @brief   Board fork of the ThreadX kernel configuration: the CMSIS-RTOS2
 *          flavour (one key more than the bring-up flavour).
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  proxy - the same kernel interface resolves to a different
 *          configuration header by include path; ref the bring-up flavour
 *          os/threadx/board/firefly/tx_user.h and the host header
 *          os/threadx/tx_user.h. The build puts THIS directory first on the
 *          tx include path (tests/board/cmsis2-firefly-v1-pcie/Makefile,
 *          TX_CFLAGS) and never puts the bring-up directory on it.
 *
 * The kernel force-includes this file into every translation unit via
 * -DTX_INCLUDE_USER_DEFINE_FILE (tx_port.h). The knobs arrive as -D from the
 * build and are re-exported here under their kernel names; a missing key is
 * a build error, not a silent default.
 *
 * Diff against the bring-up flavour: exactly one key - TX_THREAD_USER_EXTENSION
 * is DEFINED here because its consumer (os/threadx/cmsis2_threadx.c) is in this
 * image; the bring-up flavour leaves it out on purpose. Every other key and the
 * deliberately-undefined list are identical.
 *
 * Anything not listed here stays undefined and takes the port default
 * (third-party/threadx/ports/cortex_a55/gnu/inc/tx_port.h).
 */

#ifndef OS_THREADX_BOARD_FIREFLY_CMSIS2_TX_USER_H
#define OS_THREADX_BOARD_FIREFLY_CMSIS2_TX_USER_H

/* --- keys the build must inject ------------------------------------------- */

#ifndef CONFIG_TX_TICKS_PER_SECOND
#error CONFIG_TX_TICKS_PER_SECOND not injected (the board build carries it)
#endif
#ifndef CONFIG_TX_TIMER_STACK
#error CONFIG_TX_TIMER_STACK not injected (the board build carries it)
#endif

/* --- kernel knobs ---------------------------------------------------------- */

/* Tick rate. 1000 Hz: osDelay ticks read as milliseconds. */
#define TX_TIMER_TICKS_PER_SECOND	CONFIG_TX_TICKS_PER_SECOND

/* Kernel timer-thread stack. */
#define TX_TIMER_THREAD_STACK_SIZE	CONFIG_TX_TIMER_STACK

/* Stack overflow becomes a named culprit instead of a silent hang. The
 * notify handler lives in the board glue and parks after printing. */
#define TX_ENABLE_STACK_CHECKING

/* CMSIS slot backlink: one extra pointer per thread, so the CMSIS-RTOS2
 * backend finds its slot from a kernel thread without a table walk
 * (osThreadGetId is one dereference). Same shape as the host header. */
#define TX_THREAD_USER_EXTENSION	VOID *tx_thread_cmsis_slot;

/* Deliberately undefined (each with its reason):
 *   TX_MAX_PRIORITIES       - port default 32; the CMSIS band map assumes it
 *   TX_ENABLE_WFI           - upstream default off; idle is a busy poll
 *   ENABLE_ARM_FP           - -mgeneral-regs-only: there is no FP state
 *   TX_ENABLE_IRQ/FIQ_NESTING, TX_ENABLE_FIQ_SUPPORT, TX_DISABLE_ERROR_CHECKING,
 *   TX_ENABLE_EVENT_TRACE, TX_ENABLE_EXECUTION_CHANGE_NOTIFY
 *                             - all stay off (single core; no FIQ owner in
 *                             this image; error checking wanted on) */

/* --- measurement-only profile kit (EPK=1 build lever) ----------------------
 *
 * TX_EXECUTION_PROFILE_ENABLE (injected by the build) rebuilds the kernel and
 * the port so every thread switch and ISR boundary accumulates per-thread
 * execution time from the time source below - the per-thread answer to "who
 * eats the single core". The source is CNTVCT (24 MHz, always counting, and
 * WFI-independent: the PMU route is closed because WFI stops PMCCNTR while
 * this board's idle genuinely busy-polls). EXECUTION_TIME_SOURCE_TYPE is
 * unsigned long = 64-bit here, so no wrap handling ever triggers; the max
 * constant is set to the full width to match. Off by default: the switch
 * changes the TX_THREAD layout and adds a counter read per context switch,
 * so the shipped image stays byte-for-byte without it (the same lever
 * discipline as the throughput arms). */
#if defined(TX_EXECUTION_PROFILE_ENABLE)
#ifndef __ASSEMBLER__
extern unsigned long long gtimer_count_get(void);
#endif
#define TX_EXECUTION_TIME_SOURCE	((EXECUTION_TIME_SOURCE_TYPE)gtimer_count_get())
#define TX_EXECUTION_MAX_TIME_SOURCE	0xFFFFFFFFFFFFFFFFULL
#endif

#endif /* OS_THREADX_BOARD_FIREFLY_CMSIS2_TX_USER_H */

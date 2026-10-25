/*
 * @file    tx_user.h
 * @brief   Board fork of the ThreadX kernel configuration (the board image's
 *          key set).
 * @author  zhugengyu
 * @date    07.10.2026
 * @design  proxy - the same kernel interface resolves to a different
 *          configuration header by include path; ref the host header
 *          os/threadx/tx_user.h and the include-path selection in
 *          tests/board/threadx-rk3568-smoke/Makefile (TX_CFLAGS).
 *
 * The kernel force-includes this file into every translation unit via
 * -DTX_INCLUDE_USER_DEFINE_FILE (tx_port.h). The knobs arrive as -D from the
 * build and are re-exported here under their kernel names; a missing key is
 * a build error, not a silent default.
 *
 * This file and the host fork (os/threadx/tx_user.h) share a name on
 * purpose: the flavour is selected by the compile set, not by conditionals -
 * the board build never compiles the host tree and puts
 * os/threadx/board/rk3568-itx first on the include path, so tx_port.h's
 * #include "tx_user.h" resolves here.
 *
 * Anything not listed here stays undefined and takes the port default
 * (third-party/threadx/ports/cortex_a55/gnu/inc/tx_port.h).
 */

#ifndef OS_THREADX_BOARD_RK3568_TX_USER_H
#define OS_THREADX_BOARD_RK3568_TX_USER_H

/* --- keys the build must inject ------------------------------------------- */

#ifndef CONFIG_TX_TICKS_PER_SECOND
#error CONFIG_TX_TICKS_PER_SECOND not injected (the board build carries it)
#endif
#ifndef CONFIG_TX_TIMER_STACK
#error CONFIG_TX_TIMER_STACK not injected (the board build carries it)
#endif

/* --- kernel knobs ---------------------------------------------------------- */

/* Tick rate. 1000 Hz: osDelay ticks read as milliseconds and the anchor
 * ladder's "[ N.000] tick: t=N000" stamps line up with seconds. */
#define TX_TIMER_TICKS_PER_SECOND	CONFIG_TX_TICKS_PER_SECOND

/* Kernel timer-thread stack. The port default happens to equal the key
 * default; carrying the key keeps the growth path one flag away. */
#define TX_TIMER_THREAD_STACK_SIZE	CONFIG_TX_TIMER_STACK

/* Stack overflow becomes a named culprit instead of a silent hang. The
 * notify handler lives in tx_glue.c and parks after printing - it must
 * exist whenever this is on. */
#define TX_ENABLE_STACK_CHECKING

/* Deliberately undefined (each with its reason):
 *   TX_THREAD_USER_EXTENSION - the CMSIS slot backlink; its consumer
 *     (os/threadx/cmsis2_threadx.c) is not in this image - the first
 *     CMSIS-RTOS2 board round adds the flavour that defines it
 *   TX_MAX_PRIORITIES       - port default 32; the CMSIS band map assumes it
 *   TX_ENABLE_WFI           - upstream default off; idle is a busy poll
 *   ENABLE_ARM_FP           - -mgeneral-regs-only: there is no FP state
 *   TX_ENABLE_IRQ/FIQ_NESTING, TX_ENABLE_FIQ_SUPPORT, TX_DISABLE_ERROR_CHECKING,
 *   TX_ENABLE_EVENT_TRACE, TX_ENABLE_EXECUTION_CHANGE_NOTIFY
 *                             - all stay off (single core; no FIQ owner in
 *                             this image; error checking wanted on) */

#endif /* OS_THREADX_BOARD_RK3568_TX_USER_H */

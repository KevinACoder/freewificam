/**
 * @file    tx_user.h
 * @brief   ThreadX kernel configuration for the host build.
 * @author  zhugengyu
 * @date    06.10.2026

 * @design adapter - ref threadx:ports/linux/gnu/inc/tx_port.h:86
 *
 * The kernel force-includes this file into every translation unit: the build passes
 * -DTX_INCLUDE_USER_DEFINE_FILE, and the port's tx_port.h reaches this header by that
 * define. There is no generated configuration file - the values below are the whole
 * configuration, and anything not listed takes the port default.
 */

#ifndef OS_THREADX_TX_USER_H
#define OS_THREADX_TX_USER_H

/* Tick rate: 1000 Hz, so one CMSIS tick is one millisecond. The Linux port turns this
 * into the period of its simulated timer interrupt. */
#define TX_TIMER_TICKS_PER_SECOND    1000

/* Per-thread backlink to the CMSIS slot that owns this control block, so that
 * osThreadGetId() is one pointer dereference instead of a walk over the slot table. */
#define TX_THREAD_USER_EXTENSION    VOID *tx_thread_cmsis_slot;

/*
 * Deliberately undefined, each for a reason:
 *
 * TX_ENABLE_STACK_CHECKING   - the Linux port points tx_thread_stack_ptr at the recorded
 *                              stack top at thread build time and never updates it (the
 *                              real stack is the pthread's), so the kernel's stack checks
 *                              can never fire here. A green light that cannot turn red is
 *                              worse than no light, so the knob stays off; it returns with
 *                              a port whose threads run on the recorded stack.
 * TX_MAX_PRIORITIES          - port default 32. The CMSIS priority map below is written
 *                              against it, so it is not a free knob.
 * TX_TIMER_THREAD_STACK_SIZE - unused by this port (the timer thread is a pthread).
 * TX_LINUX_MEMORY_SIZE       - port default 64000 bytes is enough for the kernel objects
 *                              this build creates.
 * TX_DISABLE_ERROR_CHECKING  - stays off: API errors must stay visible.
 * TX_LINUX_DEBUG_ENABLE, TX_ENABLE_EVENT_TRACE - off: they add logging and trace noise,
 *                              not behaviour.
 */

#endif /* OS_THREADX_TX_USER_H */

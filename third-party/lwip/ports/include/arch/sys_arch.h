/*
 * @file    sys_arch.h
 * @brief   lwIP per-port OS types for the single CMSIS-RTOS2 sys_arch.
 *
 * The types lwIP expects from a port: one opaque wrapper struct per object,
 * each holding exactly one handle, plus the validity probes the stack calls
 * on them. The kernel appears only in sys_arch.c - this header names no
 * kernel type, so it compiles wherever CMSIS-RTOS2 reaches.
 *
 * @design ports & adapters: the adapter boundary is CMSIS-RTOS2, so there is
 * one sys_arch serving both kernels. Type and macro shape re-derived from the
 * upstream FreeRTOS port of the same stack face:
 * third-party/lwip/contrib/ports/freertos/include/arch/sys_arch.h:32-95
 * (same struct set, same valid/set_invalid macro pair, same ERR_NEED_SCHED
 * contract); the single-adapter rationale follows
 * ports/net80211/osal/cmsis2/osal_cmsis2.c:1-26. Header bytes are ours; the
 * names and shapes are the contract lwIP's sys.h declares.
 */

#ifndef LWIP_ARCH_SYS_ARCH_H
#define LWIP_ARCH_SYS_ARCH_H

#include "lwip/opt.h"
#include "lwip/arch.h"

/* Returned by the from-ISR post to tell the outermost function that a
 * context switch is wanted. Both CMSIS backends reschedule from their
 * interrupt exit path, so the port answers ERR_OK there; the macro exists
 * because the stack's contract names it. */
#define ERR_NEED_SCHED 123

void sys_arch_msleep(u32_t delay_ms);
#define sys_msleep(ms) sys_arch_msleep(ms)

#if SYS_LIGHTWEIGHT_PROT
typedef u32_t sys_prot_t;
#endif /* SYS_LIGHTWEIGHT_PROT */

#if !LWIP_COMPAT_MUTEX
struct _sys_mut {
	void *mut;
};
typedef struct _sys_mut sys_mutex_t;
#define sys_mutex_valid_val(mutex)   ((mutex).mut != NULL)
#define sys_mutex_valid(mutex)       (((mutex) != NULL) && sys_mutex_valid_val(*(mutex)))
#define sys_mutex_set_invalid(mutex) ((mutex)->mut = NULL)

/* Bounded acquire and stack liveness, for the measurement paths that must turn
 * a hang into a line: a plain LOCK_TCPIP_CORE() that never returns prints
 * nothing at all. Both are port extensions, not part of lwIP's sys.h. */
int sys_mutex_lock_timeout(sys_mutex_t *mutex, u32_t timeout_ms);
unsigned long sys_arch_mbox_fetch_count(void);
const char *sys_arch_core_lock_owner(void);
unsigned long sys_arch_core_lock_held_ms(void);
#endif /* !LWIP_COMPAT_MUTEX */

struct _sys_sem {
	void *sem;
};
typedef struct _sys_sem sys_sem_t;
#define sys_sem_valid_val(sema)      ((sema).sem != NULL)
#define sys_sem_valid(sema)          (((sema) != NULL) && sys_sem_valid_val(*(sema)))
#define sys_sem_set_invalid(sema)    ((sema)->sem = NULL)

struct _sys_mbox {
	void *mbx;
};
typedef struct _sys_mbox sys_mbox_t;
#define sys_mbox_valid_val(mbox)     ((mbox).mbx != NULL)
#define sys_mbox_valid(mbox)         (((mbox) != NULL) && sys_mbox_valid_val(*(mbox)))
#define sys_mbox_set_invalid(mbox)   ((mbox)->mbx = NULL)

struct _sys_thread {
	void *thread_handle;
};
typedef struct _sys_thread sys_thread_t;

#endif /* LWIP_ARCH_SYS_ARCH_H */

/*
 * @file   sys_arch.h
 * @brief  lwIP's per-port types for the CMSIS-RTOS2 sys_arch
 *         (sys_arch.c sits next to this header's directory).
 *
 * The FreeRTOS build uses the contrib port's header of the same shape
 * (third-party/lwip/contrib/ports/freertos/include/arch/sys_arch.h). The
 * types are opaque to the stack - wrapper structs around a void handle are
 * all lwIP ever sees - and the kernel only ever appears in sys_arch.c.
 * Ported from the prior workspace's lwip line (specs/lwip-adapt §3.2.2).
 *
 * @author zhugengyu
 * @date   22.09.2026 (prior-work) / 02.10.2026 (port)
 */

#ifndef LWIP_ARCH_SYS_ARCH_H
#define LWIP_ARCH_SYS_ARCH_H

#include "lwip/opt.h"
#include "lwip/arch.h"

/* The contrib FreeRTOS port returns this from the fromisr post to make the
 * caller ask for a context switch. The CMSIS/ThreadX image reschedules
 * through the IRQ-exit path with no extra help, so posts from ISRs simply
 * succeed - the macro exists for the contract, not because we return it. */
#define ERR_NEED_SCHED	123

void sys_arch_msleep(u32_t delay_ms);
#define sys_msleep(ms)	sys_arch_msleep(ms)

#if SYS_LIGHTWEIGHT_PROT
typedef u32_t sys_prot_t;
#endif /* SYS_LIGHTWEIGHT_PROT */

#if !LWIP_COMPAT_MUTEX
struct _sys_mut {
	void *mut;
};
typedef struct _sys_mut sys_mutex_t;
#define sys_mutex_valid_val(mutex)	((mutex).mut != NULL)
#define sys_mutex_valid(mutex)		(((mutex) != NULL) && sys_mutex_valid_val(*(mutex)))
#define sys_mutex_set_invalid(mutex)	((mutex)->mut = NULL)
#endif /* !LWIP_COMPAT_MUTEX */

struct _sys_sem {
	void *sem;
};
typedef struct _sys_sem sys_sem_t;
#define sys_sem_valid_val(sema)		((sema).sem != NULL)
#define sys_sem_valid(sema)		(((sema) != NULL) && sys_sem_valid_val(*(sema)))
#define sys_sem_set_invalid(sema)	((sema)->sem = NULL)

struct _sys_mbox {
	void *mbx;
};
typedef struct _sys_mbox sys_mbox_t;
#define sys_mbox_valid_val(mbox)	((mbox).mbx != NULL)
#define sys_mbox_valid(mbox)		(((mbox) != NULL) && sys_mbox_valid_val(*(mbox)))
#define sys_mbox_set_invalid(mbox)	((mbox)->mbx = NULL)

struct _sys_thread {
	void *thread_handle;
};
typedef struct _sys_thread sys_thread_t;

/* N7 diagnostics: live depth of the tcpip input mailbox (the first one
 * sys_mbox_new created). See sys_arch.c. */
unsigned int sys_arch_tcpip_mbox_count(void);

#endif /* LWIP_ARCH_SYS_ARCH_H */

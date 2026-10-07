/*
 * @file
 * @brief process-context shell: a single context.
 *
 * net80211 runs its state machine in one port worker; the imported code only
 * asks whether it is in "process context" through curproc, and the answer is a
 * non-NULL constant so the checks take the process path. The current thread can
 * be asked from the CMSIS-RTOS2 callers directly (osThreadGetId) where a driver
 * round needs an identity.
 *
 * @design kernel-services shim: same declaration face as the component's compat
 * header; no process model behind it.
 */

#ifndef _WLAN_SYS_PROC_H_
#define _WLAN_SYS_PROC_H_

struct proc;

extern struct proc proc0_holder;
#define curproc (&proc0_holder)

#endif /* _WLAN_SYS_PROC_H_ */

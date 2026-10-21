/*
 * @file
 * @brief once(9) shell.
 *
 * The imported stack runs net80211 on a single RTOS scheduling domain, so
 * "exactly once" reduces to a flag: there is no second context that could race
 * the initialisation. If a future round brings a second domain, this is the
 * place to switch to an atomic CAS (recorded in the port DIVERGENCES ledger).
 *
 * @design kernel-services shim: same declaration face as the component's compat
 * header, with the same single-context semantics spelled out.
 */

#ifndef _WLAN_SYS_ONCE_H_
#define _WLAN_SYS_ONCE_H_

/* ONCE_DECL declares the run-once control; RUN_ONCE runs fn exactly once. The
 * control is zero-initialised because the stack declares it at file scope and
 * relies on the first RUN_ONCE doing the work. */
#define ONCE_DECL(n) int n = 0
#define RUN_ONCE(ctl, fn) ((*(ctl)) ? 0 : ((*(ctl)) = 1, (fn)(), 0))

#endif /* _WLAN_SYS_ONCE_H_ */

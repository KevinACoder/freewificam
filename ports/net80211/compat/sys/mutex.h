/*
 * @file
 * @brief Sleeping mutex and condition-variable shells for the imported stack.
 *
 * The imported net80211 sources (and the kept vocabulary header
 * net/if_media.h, whose ifmedia_lock macros call mutex_enter/exit/owned) embed
 * these shells by value and expect the NetBSD names. The implementation is the
 * single CMSIS-RTOS2 osal of this port (osal/cmsis2/).
 *
 * @design kernel-services shim: same declaration face as the component's compat
 * header it replaces (ports/net80211/compat replaces the machinery headers of
 * the imported component; the vocabulary headers keep the upstream bytes).
 */

#ifndef _WLAN_SYS_MUTEX_H_
#define _WLAN_SYS_MUTEX_H_

#include <sys/types.h>

/* Fixed-size shells: net80211 embeds these by value. The osal keeps the RTOS
 * object id plus owner/depth bookkeeping inside, so the size must hold an
 * osMutexId_t and two machine words. */
struct host_mutex {
	void *hm_priv[8];
};
struct host_cond {
	void *hc_priv[8];
};

typedef struct host_mutex kmutex_t;
typedef struct host_cond kcondvar_t;

#define MUTEX_DEFAULT 0
#define MUTEX_SPIN 1
#define IPL_NONE 0
#ifndef IPL_VM
#define IPL_VM 0
#endif
#ifndef IPL_NET
#define IPL_NET 2
#endif
#ifndef MTX_DUPOK
#define MTX_DUPOK 0
#endif
#ifndef CV_DEFAULT
#define CV_DEFAULT 0
#endif

int wlan_mutex_init(kmutex_t *m, int type, int ipl);
int wlan_mutex_enter(kmutex_t *m);
int wlan_mutex_exit(kmutex_t *m);
int wlan_mutex_owned(kmutex_t *m);
void wlan_mutex_destroy(kmutex_t *m);

#define mutex_init(m, type, ipl) wlan_mutex_init((m), (type), (ipl))
#define mutex_enter(m) wlan_mutex_enter(m)
#define mutex_exit(m) wlan_mutex_exit(m)
#define mutex_owned(m) wlan_mutex_owned(m)
#define mutex_destroy(m) wlan_mutex_destroy(m)
/* the imported drivers build sleep/wakeup handshakes on the spin variant: it
 * must really lock */
#define mutex_spin_enter(x) wlan_mutex_enter(x)
#define mutex_spin_exit(x) wlan_mutex_exit(x)

int wlan_cv_init(kcondvar_t *cv, int flags);
int wlan_cv_wait(kcondvar_t *cv, kmutex_t *m);
int wlan_cv_timedwait(kcondvar_t *cv, kmutex_t *m, int ticks);
int wlan_cv_broadcast(kcondvar_t *cv);
void wlan_cv_destroy(kcondvar_t *cv);

#define cv_init(cv, ...) wlan_cv_init((cv), 0)
#define cv_wait(cv, m) wlan_cv_wait((cv), (m))
#define cv_timedwait(cv, m, t) wlan_cv_timedwait((cv), (m), (t))
#define cv_broadcast(cv) wlan_cv_broadcast(cv)
#define cv_destroy(cv) wlan_cv_destroy(cv)

/* Condition variables are not used by the net80211 state machine; they serve
 * the imported drivers' command handshakes. The semantics match the component
 * header this replaces: cv_wait releases the mutex for the wait and reacquires
 * it (recursion depth included) before returning. */

#endif /* _WLAN_SYS_MUTEX_H_ */

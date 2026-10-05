/*
 * @file
 * @brief Sleeping mutex shell over the host mutex primitive.
 */

#ifndef _COMPAT_SYS_MUTEX_H_
#define _COMPAT_SYS_MUTEX_H_

#include <sys/cdefs.h>
#include "types.h"

/* Fixed-size shells: net80211 embeds these by value. The port
 * verifies the sizes against its primitives at compile time. */
struct host_mutex {
	void *hm_priv[8];
};
struct host_cond {
	void *hc_priv[8];
};

typedef struct host_mutex kmutex_t;

#ifndef MUTEX_DEFAULT
#define MUTEX_DEFAULT 0
#endif
#define MUTEX_SPIN 1
#ifndef IPL_NET
#define IPL_NET 0
#endif
#ifndef IPL_VM
#define IPL_VM 0
#endif
#ifndef IPL_NONE
#define IPL_NONE 0
#endif
#ifndef MTX_DUPOK
#define MTX_DUPOK 0
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
/* Real locking: urtwn_wait_async() builds its sleep/wakeup handshake
 * with mutex_spin_enter + cv_wait; a no-op here loses wakeups and
 * hangs if_init forever. */
#define mutex_spin_enter(x) wlan_mutex_enter(x)
#define mutex_spin_exit(x) wlan_mutex_exit(x)

/* cv: the imported code keeps one condition variable in the urtwn
 * task; net80211 core does not use cv. */
typedef struct host_cond kcondvar_t;
#define CV_DEFAULT 0

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

#endif /* _COMPAT_SYS_MUTEX_H_ */

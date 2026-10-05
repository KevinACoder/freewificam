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

/* Condition-variable API removed with the urtwn vendored driver
 * (feat/refactor-layout-r1 S4): it was the only cv user (urtwn
 * wait_async sleep/wakeup handshake); net80211 core never used cv and
 * the wlan_cv_* functions had no implementation in the tree. */

#endif /* _COMPAT_SYS_MUTEX_H_ */

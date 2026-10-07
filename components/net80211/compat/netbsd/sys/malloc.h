/*
 * @file
 * @brief malloc(9) shell: types are decoration, storage is the port's.
 */

#ifndef _COMPAT_SYS_MALLOC_H_
#define _COMPAT_SYS_MALLOC_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <stdlib.h>   /* real malloc/free declarations first */

#define M_DEVBUF 1
#define M_TEMP 2
#define M_80211_NODE 3
#define M_80211_ACL 4
#define M_USB 5
#define M_USBDEV 6
#define M_NOFIT 7

#define M_NOWAIT 1
#define M_WAITOK 2
#define M_ZERO 4
#define M_CANFAIL 8

void *wlan_kmalloc(size_t size, int flags, int type);
void wlan_kfree(void *p, int type);

#define malloc(size, type, flags) wlan_kmalloc((size), (flags), (type))
#define free(ptr, type) wlan_kfree((ptr), (type))

#define MALLOC_DECLARE(type)
#define MALLOC_DEFINE(type, name, descr)
#define malloc_type_attach(t) ((void)0)
#define malloc_type_detach(t) ((void)0)

#endif /* _COMPAT_SYS_MALLOC_H_ */

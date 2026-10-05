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

/* upstream's kern_malloc(9) family (sys/sys/malloc.h): the audio(4)
 * middle layer allocates its ring buffers with these, and upstream
 * aliases malloc/free onto them.  Both spellings have to exist, so
 * kern_* forwards to the same port allocator. */
void *kern_malloc(unsigned long size, int flags);
void *kern_realloc(void *p, unsigned long size, int flags);
void kern_free(void *p);

#define MALLOC_DECLARE(type)
#define MALLOC_DEFINE(type, name, descr)
#define malloc_type_attach(t) ((void)0)
#define malloc_type_detach(t) ((void)0)

/* uaudio.c gets its kmem_alloc/KM_SLEEP vocabulary through this header
 * (upstream malloc.h chains into the kmem world); the include sits at
 * the end so M_WAITOK is already defined when kmem.h names it, and the
 * guards keep either entry order legal */
#include <sys/kmem.h>

#endif /* _COMPAT_SYS_MALLOC_H_ */

/*
 * @file
 * @brief kmem(9) shell over the port allocator.
 */

#ifndef _COMPAT_SYS_KMEM_H_
#define _COMPAT_SYS_KMEM_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/malloc.h>

#define KM_SLEEP M_WAITOK
#define KM_NOSLEEP M_NOWAIT
#define KM_ZERO M_ZERO

/* kmem(9) reaches uaudio.c through <sys/malloc.h>, which upstream chains
 * here; this header must therefore provide the whole vocabulary that
 * file's kmem_alloc/kmem_zalloc calls name, not just the flags above */
void *kmem_intr_alloc(size_t size, int flags);
void kmem_intr_free(void *p, size_t size);
void *kmem_intr_zalloc(size_t size, int flags);
void *kmem_zalloc(size_t size, int flags);
void *kmem_alloc(size_t size, int flags);
void kmem_free(void *p, size_t size);

#endif /* _COMPAT_SYS_KMEM_H_ */

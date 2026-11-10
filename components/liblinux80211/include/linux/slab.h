/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/slab.h
 * @brief   The include a driver names for the kmalloc family; the allocator is the
 *          component's.
 *
 * @design declaration-only compatibility face. The interface (include/linux/slab.h)
 *         owns the kmalloc/kzalloc/kcalloc/kfree family and the devm variants; the
 *         allocator this component serves - including the size-header heap contract and
 *         the gfp flags reducing to wait-or-fail - is already stated in the contract
 *         header (components/linuxkpi/linuxkpi_core.h, its allocation section). This
 *         header therefore only satisfies the include by name rather than restating a
 *         second allocator API. It adds the named-cache vocabulary a compiled consumer
 *         reaches through it: the cache record is carried as an opaque handle, the
 *         allocation and free calls are declared with the interface's signatures, and
 *         their bodies (linuxkpi_compat.c) are the port's one heap under those names -
 *         a cache is a heap record holding a name and an object size, and an object
 *         from it is a heap block of that size. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_SLAB_H_
#define _LINUXKPI_LINUX_SLAB_H_

#include "../../linuxkpi_core.h"
/* [LNX] include/linux/slab.h: includes <linux/overflow.h>; the on-stack
 * flexible-array helper a driver uses arrives through this header. */
#include <linux/overflow.h>

/* [LNX] include/linux/slab.h: the named-cache record, carried as an opaque handle:
 * no compiled unit reads a field of it, it is only stored and passed back. */
struct kmem_cache;

/* [LNX] include/linux/types.h: the cache-flag word type. */
typedef unsigned int __bitwise	slab_flags_t;

/* [LNX] include/linux/slab.h:72,69: the one cache flag the compiled set names
 * (_SLAB_HWCACHE_ALIGN is bit 4 of the interface's flag enumeration). */
#define	__SLAB_FLAG_BIT(nr)	((slab_flags_t __force)(1U << (nr)))
#define	SLAB_HWCACHE_ALIGN	__SLAB_FLAG_BIT(4)

/* [LNX] include/linux/slab.h:394-396,485,891,1308 */
struct kmem_cache *kmem_cache_create(const char *name, unsigned int size,
				     unsigned int align, slab_flags_t flags,
				     void (*ctor)(void *));
void kmem_cache_destroy(struct kmem_cache *s);
void *kmem_cache_alloc(struct kmem_cache *s, gfp_t flags);
void kmem_cache_free(struct kmem_cache *s, void *objp);
#define	kmem_cache_zalloc(_k, _flags)	kmem_cache_alloc((_k), (_flags) | __GFP_ZERO)

#endif /* _LINUXKPI_LINUX_SLAB_H_ */

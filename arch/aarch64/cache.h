/*
 * @file   cache.h
 * @brief  AArch64 cache maintenance API (arch interface).
 *
 * Virtual-address-based operations only. The by-VA set is the whole
 * interface this milestone needs (boot hygiene + the future DMA rounds' and
 * gdb Z0 breakpoint's prerequisite); set/way entire operations are
 * deliberately absent - the boot-path flush-all they supported measured as
 * boot-breaking and was retired (SPEC v3 §3.3.3).
 */

#ifndef FREEWEBCAM_CACHE_H
#define FREEWEBCAM_CACHE_H

#include <stdint.h>

/* Clean (write back) the dirty lines covering [addr, addr+size). */
void cache_dcache_flush(uintptr_t addr, unsigned long size);

/* Invalidate the lines covering [addr, addr+size) WITHOUT writing back -
 * correct only when the CPU holds no dirty data in the range (typically
 * before the CPU reads what a device wrote). */
void cache_dcache_invalidate(uintptr_t addr, unsigned long size);

/* Clean + invalidate the lines covering [addr, addr+size). */
void cache_dcache_flush_invalidate(uintptr_t addr, unsigned long size);

/* Boot-time hygiene: drain the loader's dirty lines over the whole image
 * (_start..__heap_end), then invalidate the instruction cache. Runs with
 * caches off, from mmu_enable() - the last moment the range is plain
 * memory. */
void cache_init(void);

/* Make stores to [addr, addr+size) visible to instruction fetch (software
 * breakpoint patching, self-modifying code). */
void cache_icache_sync_range(uintptr_t addr, unsigned long size);

#endif /* FREEWEBCAM_CACHE_H */

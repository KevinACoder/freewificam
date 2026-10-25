/*
 * @file   cache.c
 * @brief  AArch64 data cache maintenance.
 *
 * Cache line is 64 bytes on Cortex-A55. Ranges are rounded outward: cleaning
 * or invalidating a few extra lines is harmless, whereas missing one is the
 * silent-failure mode this exists to prevent. That asymmetry is why the
 * rounding direction is always outward and never "aligned down and hope".
 *
 * This discipline has already been the difference between working and
 * silently failing in five places on this board (IRQ, ITS, GMAC, NVMe,
 * EHCI): shared tables are reached through non-coherent ports, so every CPU
 * write needs a clean before the device reads it, and every device write
 * needs an invalidate before the CPU reads it.
 */

#include <stdint.h>

#include "cache.h"

#define DCACHE_LINE	64u

/* op: 'c' clean (write back), 'i' invalidate (discard), 'b' clean+invalidate */
static void dcache_op(uintptr_t addr, unsigned long size, char op)
{
	uintptr_t start = addr & ~(uintptr_t)(DCACHE_LINE - 1u);
	uintptr_t end = (addr + size + DCACHE_LINE - 1u) &
			~(uintptr_t)(DCACHE_LINE - 1u);
	uintptr_t p;

	for (p = start; p < end; p += DCACHE_LINE) {
		switch (op) {
		case 'c':
			__asm__ __volatile__("dc cvac, %0" ::"r"(p) : "memory");
			break;
		case 'i':
			/* ivac discards without writing back - correct only
			 * when the CPU has no dirty data in the range. */
			__asm__ __volatile__("dc ivac, %0" ::"r"(p) : "memory");
			break;
		default:
			__asm__ __volatile__("dc civac, %0" ::"r"(p) : "memory");
			break;
		}
	}
	__asm__ __volatile__("dsb sy" ::: "memory");
}

void cache_dcache_flush(uintptr_t addr, unsigned long size)
{
	if (size != 0u) {
		dcache_op(addr, size, 'c');
	}
}

void cache_dcache_invalidate(uintptr_t addr, unsigned long size)
{
	if (size != 0u) {
		dcache_op(addr, size, 'i');
	}
}

void cache_dcache_flush_invalidate(uintptr_t addr, unsigned long size)
{
	if (size != 0u) {
		dcache_op(addr, size, 'b');
	}
}

/* --- boot-time cache hygiene --------------------------------------------- */

/* Linker-provided extent of everything this image owns. */
extern char _start[];
extern char __heap_end[];

/* Drain whatever the loader left dirty over our own memory, then invalidate
 * the instruction cache.
 *
 * The loader (U-Boot here) runs with its MMU and caches on, and it is what
 * wrote our image into RAM. Some of those writes can still be sitting dirty
 * in its data cache when it jumps to us. Those lines are a trap: our boot
 * code clears .bss and writes page tables straight to memory with caching
 * off, and later - once we turn our own caches on - the stale dirty lines
 * get evicted and land on top of the code, the tables and the stack.
 *
 * The symptom is the worst kind: an image that boots, prints, and then dies
 * somewhere arbitrary, differently each time, so it looks like a peripheral
 * problem rather than a memory problem. Draining up front costs a few
 * microseconds and removes the whole class.
 *
 * Called from mmu_enable() before caches are enabled, so this is the last
 * moment at which the range is guaranteed to be plain memory. */
void cache_init(void)
{
	uintptr_t start = (uintptr_t)_start;
	uintptr_t end = (uintptr_t)__heap_end;

	if (end > start) {
		cache_dcache_flush_invalidate(start,
					      (unsigned long)(end - start));
	}

	/* Anything the loader left in the instruction cache is stale for the
	 * same reason, and unlike the data cache it does not write back. */
	__asm__ __volatile__("ic iallu" ::: "memory");
	__asm__ __volatile__("dsb sy" ::: "memory");
	__asm__ __volatile__("isb" ::: "memory");
}

/* Store-then-fetch consistency, board-proven recipe: drain the store, push
 * the dirty lines out by VA, invalidate the whole instruction cache, order,
 * and only then let fetch resume. The gdb round's Z0 software-breakpoint
 * patch reuses exactly this. */
void cache_icache_sync_range(uintptr_t addr, unsigned long size)
{
	uintptr_t start = addr & ~(uintptr_t)(DCACHE_LINE - 1u);
	uintptr_t end = (addr + size + DCACHE_LINE - 1u) &
			~(uintptr_t)(DCACHE_LINE - 1u);
	uintptr_t p;

	__asm__ __volatile__("dsb ish" ::: "memory");

	for (p = start; p < end; p += DCACHE_LINE) {
		__asm__ __volatile__("dc cvau, %0" ::"r"(p) : "memory");
	}
	__asm__ __volatile__("ic ialluis" ::: "memory");
	__asm__ __volatile__("dsb ish" ::: "memory");
	__asm__ __volatile__("isb" ::: "memory");
}

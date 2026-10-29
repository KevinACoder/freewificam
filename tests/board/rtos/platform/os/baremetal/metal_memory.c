/* SPDX-License-Identifier: GPL-2.0 */
/*
 * metal_memory.c - the single TLSF arena behind osMemoryAlloc/osMemoryFree.
 *
 * The arena lives in .bss (16 MiB by default). TLSF gives O(1) worst-case
 * allocation with bounded fragmentation, which matters because the backend
 * hands out thread stacks, queue buffers and linuxkpi/USB objects at runtime
 * and never reclaims pages.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "metal_kernel.h"
#include "tlsf.h"

#if !defined(CONFIG_METAL_HEAP_SIZE)
#define CONFIG_METAL_HEAP_SIZE (16u * 1024u * 1024u)
#endif

static uint8_t s_arena[CONFIG_METAL_HEAP_SIZE] __attribute__((aligned(16)));
static tlsf_t s_tlsf;
static size_t s_allocated;
static size_t s_min_free = CONFIG_METAL_HEAP_SIZE;
static uint32_t s_fails;

void metal_heap_init(void)
{
	s_tlsf = tlsf_create_with_pool(s_arena, sizeof(s_arena));
	s_allocated = tlsf_size();
	s_min_free = sizeof(s_arena) - s_allocated;
}

static size_t free_bytes(void)
{
	size_t total = sizeof(s_arena);

	return (s_allocated < total) ? total - s_allocated : 0u;
}

void *metal_heap_alloc(size_t size)
{
	void *p;
	size_t f;

	if (s_tlsf == NULL || size == 0u)
		return NULL;
	p = tlsf_malloc(s_tlsf, size);
	if (p == NULL) {
		s_fails++;
		return NULL;
	}
	s_allocated += tlsf_block_size(p);
	f = free_bytes();
	if (f < s_min_free)
		s_min_free = f;
	return p;
}

void metal_heap_free(void *p)
{
	if (p == NULL || s_tlsf == NULL)
		return;
	s_allocated -= tlsf_block_size(p);
	tlsf_free(s_tlsf, p);
}

void metal_heap_stats(size_t *free_now, size_t *min_free, size_t *total,
		      uint32_t *fails)
{
	if (free_now != NULL)
		*free_now = free_bytes();
	if (min_free != NULL)
		*min_free = s_min_free;
	if (total != NULL)
		*total = sizeof(s_arena);
	if (fails != NULL)
		*fails = s_fails;
}
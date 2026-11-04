/*
 * @file   cmsis2_ext.c
 * @brief  Zephyr-side critical section, heap and profile answers for the
 *         CMSIS-RTOS2 extension.
 *
 * The extension header (os/cmsis2/cmsis_os2_ext.h) names three services CMSIS
 * itself does not define:
 *
 *  - osCriticalEnter/osCriticalExit: a short section that excludes every other
 *    context, taken from threads and from the completion path alike.  On a
 *    single-core Zephyr image that is irq_lock()/irq_unlock(): interrupts
 *    masked, the caller's key restored on exit, nesting per-context by
 *    construction.  It is deliberately NOT k_spin_lock(): that is the SMP
 *    global lock, and holding it across a reschedule would trip Zephyr's own
 *    "interrupt lock held" assertion, while these sections never block.
 *
 *  - osMemoryAlloc/osMemoryFree/osMemoryGetStats: the one allocator the
 *    components see, here over Zephyr's system heap (k_malloc).  The same call
 *    pair is what CherryUSB's osal reaches through pvPortMalloc/vPortFree.
 *
 *  - osExecProfileGet: no execution-profile kit exists on this backend, and the
 *    extension header's contract is that every backend without one answers
 *    with an empty profile so callers need no per-kernel switch.
 *
 * Heap gauge: Zephyr's system heap does not publish free/low-water marks
 * through a public call, and the extension header allows a backend to answer
 * zeros there.  The one field that IS truthful here is alloc_fail, which is
 * the field the ledger reads.
 */

#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/__assert.h>

#include "os/cmsis2/cmsis2.h"

#if !defined(CONFIG_HEAP_MEM_POOL_SIZE) || (CONFIG_HEAP_MEM_POOL_SIZE == 0)
#error "the zephyr backend allocates from the system heap: set CONFIG_HEAP_MEM_POOL_SIZE"
#endif

/* --- critical section ----------------------------------------------------- */

os_critical_state_t osCriticalEnter(void)
{
	return (os_critical_state_t)irq_lock();
}

void osCriticalExit(os_critical_state_t state)
{
	irq_unlock((unsigned int)state);
}

/* --- heap ----------------------------------------------------------------- */

/* The prefix keeps the payload 16-byte aligned (k_malloc answers at least
 * 8-byte aligned, and the 16-byte prefix preserves that) and carries the
 * payload size so a later debug build can report live bytes. */
struct z_alloc_hdr {
	size_t size;
	size_t pad;
};

static size_t alloc_live_bytes;
static size_t alloc_peak_bytes;
static uint32_t alloc_fail_count;

void *osMemoryAlloc(size_t size)
{
	struct z_alloc_hdr *hdr;

	if (size == 0u) {
		return NULL;
	}
	hdr = k_malloc(sizeof(*hdr) + size);
	if (hdr == NULL) {
		alloc_fail_count++;
		return NULL;
	}
	hdr->size = size;

	alloc_live_bytes += size;
	if (alloc_live_bytes > alloc_peak_bytes) {
		alloc_peak_bytes = alloc_live_bytes;
	}
	return (void *)(hdr + 1);
}

void osMemoryFree(void *ptr)
{
	struct z_alloc_hdr *hdr;

	if (ptr == NULL) {
		return;
	}
	hdr = (struct z_alloc_hdr *)((uintptr_t)ptr - sizeof(struct z_alloc_hdr));
	alloc_live_bytes -= hdr->size;
	k_free(hdr);
}

void osMemoryGetStats(struct osMemoryStats *out)
{
	if (out == NULL) {
		return;
	}
	/* free_bytes / total_bytes stay 0: unknown on this backend, which the
	 * contract permits.  alloc_fail and the peak are this backend's own
	 * bookkeeping and are exact. */
	out->free_bytes = 0u;
	out->min_free_bytes = (alloc_peak_bytes != 0u) ? alloc_peak_bytes : 0u;
	out->total_bytes = 0u;
	out->alloc_fail = alloc_fail_count;
}

/* --- execution profile ---------------------------------------------------- */

size_t osExecProfileGet(os_exec_profile_entry_t *entries, size_t max,
			uint64_t *isr_ticks, uint64_t *idle_ticks)
{
	ARG_UNUSED(entries);
	ARG_UNUSED(max);

	if (isr_ticks != NULL) {
		*isr_ticks = 0u;
	}
	if (idle_ticks != NULL) {
		*idle_ticks = 0u;
	}
	return 0u;
}

/* --- host exit seam ------------------------------------------------------- */

void cmsis2_host_end_scheduler(void)
{
	/* Zephyr's main thread is the scheduling point; there is no scheduler
	 * to unwind.  Ending the image cleanly is the caller's park or the
	 * board's reset, both of which stay outside this file. */
}

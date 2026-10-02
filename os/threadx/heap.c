/*
 * @file   heap.c
 * @brief  The system allocation face over the mirror-image heap (SPEC S3
 *         threadx-up-boot §3.4.3, FR-6).
 *
 * ThreadX has no integrated heap; thread stacks (from the CMSIS adapter on)
 * and later component allocations come out of the linker-reserved heap
 * region through tlsf (components/tlsf, vendored v3.1 BSD-2). The pool is
 * created lazily on the first allocation; the first allocation also runs a
 * RAM probe (write/read compare down the arena) so an MMU mapping mistake
 * surfaces as a boot-time log line instead of silent corruption.
 *
 * tlsf is not thread-safe: every entry wraps the work in the UP protect
 * pair (_tx_thread_smp_protect/_unprotect, tx_up_shim.c) - with the kernel
 * up that masks IRQ+FIQ, before it does the same via DAIF, so the critical
 * section holds in both worlds.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tlsf.h"

#include "log.h"
#include "minilibc.h"

/* The UP critical-section pair (tx_up_shim.c): UP kernel has no SMP port
 * spinlocks, so the adapter-provided pair IS the mutual exclusion. */
extern unsigned int _tx_thread_smp_protect(void);
extern void _tx_thread_smp_unprotect(unsigned int posture);

/* tlsf v3.1 reports its internal sanity-check failures through bare
 * printf; this freestanding image has no libc to answer. Land the message
 * on the raw console outlet (fatal-path discipline: no stamp, no line
 * assembly - byte-honest diagnostics). Same service the prior project's
 * libc provided ([IMP]:20). */
int printf(const char *fmt, ...)
{
	static char buf[192];
	va_list ap;
	size_t len;

	va_start(ap, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	len = strlen(buf);
	board_console_write_raw(buf, len);
	if (len != 0U && buf[len - 1U] != '\n') {
		board_console_write_raw("\n", 1U);
	}
	return 0;
}

/* Linker symbols from arch/aarch64/baremetal.ld (heap reservation, NOLOAD:
 * zero image-cost, S1 R1 landed both). */
extern unsigned char __heap_start[];
extern unsigned char __heap_end[];

#ifndef CONFIG_HEAP_BYTES
#define CONFIG_HEAP_BYTES	0x400000U
#endif

static tlsf_t heap_tlsf;
static pool_t heap_pool;
static bool heap_ready;
static size_t heap_allocated;	/* live byte count (v3.1 has no query API) */

/* --- assert landing (freestanding) ----------------------------------------- */

/* tlsf's tlsf_assert is <assert.h>'s assert; -DNDEBUG is not set this
 * round, so the checks are live and need a place to land. */
void __assert_func(const char *file, int line, const char *func,
		   const char *expr)
{
	/* Emergency outlet (N7-stab): a heap assert can land inside the tlsf
	 * critical section or a live print - board_log would deadlock on the
	 * print lock and erase the evidence. */
	board_log_emergency("heap: assert %s at %s:%d (%s)", expr, file, line,
			    func != NULL ? func : "?");
	for (;;) {
		__asm__ __volatile__("wfe");
	}
}

/* --- pool bring-up ---------------------------------------------------------- */

/* Write/read compare walking down the arena: the cheap probe that turns a
 * wrong MMU mapping into a boot-time FAIL line (spec §3.4.3). */
static bool heap_ram_probe(void)
{
	volatile unsigned long *p;
	size_t arena = (size_t)(__heap_end - __heap_start);
	unsigned int i;

	for (i = 0U; i < 4U; i++) {
		p = (volatile unsigned long *)(__heap_start +
					       (arena >> (2U + i)));
		*p = 0xa55a5aa5UL;
		if (*p != 0xa55a5aa5UL) {
			return false;
		}
		*p = 0UL;
	}
	return true;
}

static bool heap_init(void)
{
	if (heap_ready) {
		return true;
	}

	if (!heap_ram_probe()) {
		board_log("heap: RAM probe FAIL\n");
		return false;
	}

	heap_tlsf = tlsf_create_with_pool(__heap_start,
					  (size_t)CONFIG_HEAP_BYTES);
	if (heap_tlsf == NULL) {
		board_log("heap: tlsf_create FAIL (%u bytes)\n",
			  (unsigned)CONFIG_HEAP_BYTES);
		return false;
	}
	heap_pool = tlsf_get_pool(heap_tlsf);
	heap_ready = true;
	board_log("heap: tlsf ready %u bytes at %p\n",
		  (unsigned)CONFIG_HEAP_BYTES, __heap_start);
	return true;
}

/* --- the allocation face ------------------------------------------------------ */

void *pvPortMalloc(size_t size)
{
	unsigned int posture;
	void *p;

	if (!heap_init()) {
		return NULL;
	}

	posture = _tx_thread_smp_protect();
	p = tlsf_malloc(heap_tlsf, size);
	if (p != NULL) {
		heap_allocated += size;
	}
	_tx_thread_smp_unprotect(posture);

	if (p == NULL) {
		board_log("heap: OOM %u bytes\n", (unsigned)size);
	}
	return p;
}

/* P6-b forensics: the tlsf double-free assert needs its caller's
 * address trail - keep the last frees (and the next alloc) in a small
 * ring so the assert line can be paired with what released before it. */
static void *free_log[8];
static unsigned int free_log_n;

void vPortFree(void *ptr)
{
	unsigned int posture;
	size_t size;

	if (ptr == NULL) {
		return;
	}

	posture = _tx_thread_smp_protect();
	size = tlsf_block_size(ptr);
	free_log[free_log_n % 8U] = ptr;
	free_log_n++;
#ifdef CONFIG_HEAP_FREE_LOG
	/* P6-b forensics default-off since S7b: the netutils world frees per
	 * request, and one line per free floods the console. The ring above
	 * stays live (re-enable with CONFIG_HEAP_FREE_LOG=y). */
	board_log("heap: free #%u %p size %u\n",
		  (unsigned)free_log_n, ptr, (unsigned)size);
#endif
	tlsf_free(heap_tlsf, ptr);
	if (heap_allocated >= size) {
		heap_allocated -= size;
	}
	_tx_thread_smp_unprotect(posture);
}

size_t xPortGetFreeHeapSize(void)
{
	/* tlsf v3.1 has no free-size query; the allocated-bytes ledger over
	 * the known arena is the honest approximation (block-size rounding
	 * makes it drift low on frees by a few header bytes). */
	size_t arena = (size_t)CONFIG_HEAP_BYTES;

	if (!heap_ready) {
		return arena;
	}
	return (arena > heap_allocated) ? (arena - heap_allocated) : 0U;
}

/* --- image-level libc bindings (S7b/netutils FR-10) -------------------------- */

/* The standard-name malloc family over the SAME tlsf pool - one allocator
 * image-wide (the prior workspace's netutils_shim rule: no second
 * allocator, cross-world free must stay safe). realloc follows the C
 * library contract: realloc(NULL, n) == malloc, realloc(p, 0) == free. */
void *malloc(size_t size)
{
	return pvPortMalloc(size);
}

void free(void *ptr)
{
	vPortFree(ptr);
}

void *calloc(size_t count, size_t size)
{
	size_t bytes = count * size;
	void *p = pvPortMalloc(bytes);

	if (p != NULL) {
		memset(p, 0, bytes);
	}
	return p;
}

void *realloc(void *ptr, size_t size)
{
	unsigned int posture;
	void *p;

	if (ptr == NULL) {
		return malloc(size);
	}
	if (size == 0U) {
		free(ptr);
		return NULL;
	}
	if (!heap_init()) {
		return NULL;
	}

	posture = _tx_thread_smp_protect();
	p = tlsf_realloc(heap_tlsf, ptr, size);
	if (p != NULL) {
		heap_allocated += size;
	}
	_tx_thread_smp_unprotect(posture);

	if (p == NULL) {
		board_log("heap: OOM realloc %u bytes\n", (unsigned)size);
	}
	return p;
}

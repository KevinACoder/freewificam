/*
 * @file    heap.c
 * @brief   The system allocation face on the board: tlsf over the linker window.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  proxy - the same two symbols (os/threadx/heap.h:20-21) resolve to a
 *          different arena per side: the host binds the C library allocator
 *          (os/threadx/heap.c), the board binds tlsf over the window the linker
 *          reserves (arch/aarch64/baremetal.ld:101-104, 1M). ThreadX has no
 *          integrated heap, so thread stacks and kernel control blocks come
 *          from here.
 *
 * The pool is created lazily on the first allocation (by then the console and
 * the stamped sink are up, so the bring-up line lands) and guarded by the
 * kernel's public interrupt-control primitive: tlsf is not thread-safe, and on
 * one core masking interrupts IS the whole mutual exclusion.
 *
 * Size is not a knob here on purpose: the arena is the linker window, and the
 * linker script is the single place to grow it - two sources of the same number
 * would drift.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "tx_api.h"

#ifndef OS_THREADX_BOARD_RK3568_CMSIS2_TX_USER_H
#error "the CMSIS2 flavour of the ThreadX board configuration is not the one in effect"
#endif

#include "tlsf.h"

#include "arch_asm.h"
#include "heap.h"
#include "log.h"
#include "minilibc.h"

/* Linker symbols of the reserved window (NOLOAD: zero image cost). */
extern unsigned char __heap_start[];
extern unsigned char __heap_end[];

static tlsf_t heap_tlsf;
static int heap_state;	/* 0 = cold, 1 = ready, -1 = failed */

static int heap_init(void)
{
	unsigned char *base = __heap_start;
	size_t size = (size_t)(__heap_end - __heap_start);
	unsigned int i;

	/* RAM write/read probe at four points of the arena: an MMU mapping
	 * mistake surfaces as one boot-time line instead of silent corruption. */
	for (i = 0U; i < 4U; i++) {
		volatile uint32_t *p = (volatile uint32_t *)(void *)
			(base + (size / 4U) * (size_t)i);

		*p = 0xa55a5aa5U;
		if (*p != 0xa55a5aa5U) {
			board_log("heap: RAM probe FAIL\n");
			return -1;
		}
	}

	heap_tlsf = tlsf_create_with_pool(base, size);
	if (heap_tlsf == 0) {
		board_log("heap: tlsf_create FAIL (%u bytes)\n", (unsigned)size);
		return -1;
	}

	/* Explicit hex rather than %p: the anchor line reads the same under any
	 * formatter the image links. */
	board_log("heap: tlsf ready %u bytes at 0x%08x\n", (unsigned)size,
		  (unsigned)(uintptr_t)base);
	return 1;
}

void *pvPortMalloc(size_t size)
{
	unsigned int posture;
	void *p;

	if (heap_state == 0) {
		heap_state = heap_init();
	}
	if (heap_state < 0) {
		return NULL;
	}

	posture = tx_interrupt_control(TX_INT_DISABLE);
	p = tlsf_malloc(heap_tlsf, size);
	(void)tx_interrupt_control(posture);

	if (p == NULL) {
		board_log("heap: OOM %u bytes\n", (unsigned)size);
	}
	return p;
}

void vPortFree(void *ptr)
{
	unsigned int posture;

	if (ptr == NULL) {
		return;
	}

	posture = tx_interrupt_control(TX_INT_DISABLE);
	tlsf_free(heap_tlsf, ptr);
	(void)tx_interrupt_control(posture);
}

/* --- the freestanding C surface tlsf references ---------------------------- */

/* tlsf reports its internal check failures through bare printf; this image has
 * no libc to answer. Land the message on the raw console outlet (no stamp, no
 * line assembly - byte-honest diagnostics). */
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

/* tlsf's tlsf_assert is assert(); a heap assert can fire inside the
 * allocator's critical section or a live print, so the line goes through the
 * lock-free outlet (board_log would deadlock on the print lock and erase the
 * evidence), then the machine parks - the serial line is the whole report. */
void __assert_func(const char *file, int line, const char *func,
		   const char *expr)
{
	board_log_emergency("heap: assert %s at %s:%d (%s)", expr, file, line,
			    func != NULL ? func : "?");
	for (;;) {
		ARCH_WFE();
	}
}

/*
 * @file    libc_bindings.c
 * @brief   The board image's C-library names the imported application world
 *          expects, bound to the board's own heap and line sink.
 * @author  zhugengyu
 * @date    09.10.2026
 * @design  proxy - ref linux:lib/string_helpers.c:1 (the thin bindings that give
 *          a freestanding world the names its imported code was written
 *          against). The heap stays single-seam: every allocation lands on
 *          pvPortMalloc/vPortFree (os/threadx/board/rk3568-itx/heap.c), and
 *          realloc learns the old block's size from the allocator itself
 *          (tlsf_block_size) instead of keeping a second size table. printf
 *          assembles a line and hands it to the stamped sink (os/baremetal/
 *          log.c), which owns the CR/LF and timestamp discipline.
 *
 * Why a separate translation unit from heap.c: heap.c is the kernel-facing
 * allocation face (pvPortMalloc/vPortFree) that the CMSIS2 layer itself calls
 * before the console is up; this file adds the C library's names on top, and
 * is only linked into images whose imported world uses them (the WLAN image
 * carries the supplicant port, whose os.h maps malloc/free onto these).
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tlsf.h"

#include "heap.h"
#include "log.h"

void *malloc(size_t size)
{
	return pvPortMalloc(size <= 0 ? 1 : size);
}

void free(void *ptr)
{
	vPortFree(ptr);
}

void *calloc(size_t nmemb, size_t size)
{
	size_t total = nmemb * size;
	void *p;

	if (nmemb != 0 && size != 0 && total / nmemb != size) {
		return NULL;	/* overflow: no valid allocation exists */
	}
	p = pvPortMalloc(total <= 0 ? 1 : total);
	if (p != NULL) {
		memset(p, 0, total);
	}
	return p;
}

void *realloc(void *ptr, size_t size)
{
	void *p;
	size_t old;

	if (ptr == NULL) {
		return pvPortMalloc(size <= 0 ? 1 : size);
	}
	if (size == 0) {
		vPortFree(ptr);
		return NULL;
	}
	old = tlsf_block_size(ptr);
	if (old >= size) {
		return ptr;
	}
	p = pvPortMalloc(size);
	if (p == NULL) {
		return NULL;
	}
	memcpy(p, ptr, old);
	vPortFree(ptr);
	return p;
}

/*
 * Note: printf is NOT defined here. The board heap already carries one (the
 * raw-console outlet tlsf's diagnostics use, os/threadx/board/rk3568-itx/
 * heap.c), and the imported supplicant world prints through whatever printf
 * means in the image - one definition, one mechanism.
 */

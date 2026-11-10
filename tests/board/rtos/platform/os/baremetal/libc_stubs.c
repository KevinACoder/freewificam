/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * libc_stubs.c - the one piece of the C library's system seam this project
 * answers itself.
 *
 * The images link the toolchain's newlib for its string, memory, qsort and
 * conversion families, but every image carries its own heap (TLSF, through
 * os/threadx/board/rk3568-itx/heap.c) and hands the standard names to it
 * (malloc/free/... in libc_bindings.c where an imported world asks for
 * them). A libc member that lazily wants a system heap - newlib's stdio
 * pulls one the moment a vendored unit references setvbuf, which the
 * supplicant's debug module does - must be refused rather than served from
 * an unmapped region: a NULL allocation is something the caller survives,
 * memory outside the heap is not. Nothing in these images calls it.
 *
 * (The alternative, defining the linker's `end` and letting libnosys's
 * _sbrk hand out memory after it, was rejected: that region belongs to the
 * image's own layout, not to a second heap.)
 */

#include <stddef.h>

void *_sbrk(ptrdiff_t incr)
{
	(void)incr;

	return (void *)-1;	/* ENOMEM: this image has no libc heap */
}

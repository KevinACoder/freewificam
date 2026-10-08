/*
 * @file    cc.h
 * @brief   lwIP's compile/platform abstraction for this project.
 *
 * lwIP reaches this header as <arch/cc.h> from lwip/arch.h:48; the defaults it
 * replaces are the ones that would drag libc into a freestanding image - the
 * printf/abort assertions, rand(), and the ctype tables. cc.h holds compiler
 * and platform facts only; stack options live in lwipopts.h.
 *
 * Covered here:
 *   - byte order: AArch64 is little-endian; the protocol headers need the
 *     builtin byte swaps;
 *   - LWIP_RAND(): no rand() exists; the free-running virtual counter
 *     supplies the bits. CNTVCT rather than CNTPCT: the physical counter is
 *     gated by CNTHCTL_EL2.EL1PCEN, which this board's boot path leaves off;
 *   - diagnostics and assertions: routed into the board's log sinks. An
 *     assertion prints on the fault-path sink and parks - a failed assertion
 *     that speaks is findable, a compiled-out one is not;
 *   - the libc facts lwIP probes for (ctype tables absent; ssize_t comes from
 *     the compiled-in C library's headers, whose SSIZE_MAX this build must
 *     surface itself).
 *
 * @design kernel-services shim: the compile-time stand-in layer delivered
 * with the port, replaced per platform, not per kernel.
 * third-party/lwip/contrib/ports/win32/include/arch/cc.h:32-119 is the
 * upstream reference shape for this file (byte order, rand, diag/assert
 * hooks); the hook bodies follow this project's log discipline declared in
 * os/baremetal/log.h:1-43 (fault-path prints never take the console lock,
 * never write raw console bytes).
 */

#ifndef FREEWEB_LWIP_ARCH_CC_H
#define FREEWEB_LWIP_ARCH_CC_H

#include <stdint.h>
#include <limits.h>

#include "arch/aarch64/generic_timer.h"

/* --- libc facts ------------------------------------------------------------ */

/* lwIP maps lwip_isdigit()/lwip_isspace()/... onto <ctype.h> unless told
 * otherwise; the range-check implementations in lwip/arch.h are what a
 * freestanding image wants. */
#define LWIP_NO_CTYPE_H 1

/* lwIP probes "is ssize_t available?" via SSIZE_MAX and typedefs int ssize_t
 * itself when the macro is absent. The container headers provide ssize_t
 * already, so the macro must be defined for the probe to find them. */
#define SSIZE_MAX INT_MAX

/* --- byte order ----------------------------------------------------------- */

#define LITTLE_ENDIAN 1234
#define BIG_ENDIAN    4321
#define BYTE_ORDER    LITTLE_ENDIAN

#define LWIP_PLATFORM_BYTESWAP 1
#define LWIP_PLATFORM_HTONS(x) ((uint16_t)__builtin_bswap16((uint16_t)(x)))
#define LWIP_PLATFORM_HTONL(x) ((uint32_t)__builtin_bswap32((uint32_t)(x)))

/* --- random --------------------------------------------------------------- */

static inline uint32_t lwip_platform_rand(void)
{
	uint64_t v = gtimer_count_get();

	/* Mix the halves: the counter's low bits advance fast enough for a
	 * fragment id, the high bits make successive calls differ. */
	return (uint32_t)(v ^ (v >> 32));
}

#ifndef LWIP_RAND
#define LWIP_RAND() lwip_platform_rand()
#endif

/* --- diagnostics ---------------------------------------------------------- */

/* Implemented in platform_hooks.c: one prints through the stamped log sink,
 * the other through the fault-path sink and parks. */
void lwip_platform_diag(const char *fmt, ...);
void lwip_platform_assert(const char *message, const char *file, int line);

#define LWIP_PLATFORM_DIAG(x)   do { lwip_platform_diag x; } while (0)
#define LWIP_PLATFORM_ASSERT(x) lwip_platform_assert(x, __FILE__, __LINE__)

#endif /* FREEWEB_LWIP_ARCH_CC_H */

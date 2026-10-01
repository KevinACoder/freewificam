/*
 * @file   arch/cc.h
 * @brief  Compiler and platform abstraction for lwIP on this carrier.
 *
 * lwIP requires a compiler abstraction header, reached as <arch/cc.h> from
 * lwip/arch.h. Nothing here is an lwIP option (those live in lwipopts.h);
 * these are the platform facts lwIP cannot guess:
 *
 *  - byte order and byte-swap primitives (aarch64 is little-endian; the
 *    protocol headers need the swaps, so they are compiler builtins rather
 *    than a library call),
 *
 *  - LWIP_RAND(): lwIP uses it for the DHCP transaction id and the IPv4
 *    fragment id. There is no rand() in this freestanding image, so the
 *    free-running virtual counter supplies the bits. CNTVCT_EL0 and not
 *    CNTPCT_EL0: the physical counter is gated by CNTHCTL_EL2.EL1PCEN, which
 *    the `go` boot path leaves at 0 - reading it traps to the firmware's
 *    old EL2 vector table.
 *
 *  - the diagnostic and assertion hooks. The defaults call printf()/abort(),
 *    neither of which exists here (-nostdlib, freestanding). Assertions stay
 *    enabled on purpose during bring-up: a failed lwIP assertion that prints
 *    and parks is findable, one that was compiled out is not.
 *
 * Shape carried over from the prior workspace's lwip line (specs/lwip-adapt
 * §3.2.2: cc.h 同构搬运); the ssize_t/ctype facts live here and not in
 * lwipopts.h because arch.h probes them right after including this file,
 * before it has seen lwipopts.h in translation units that reach arch.h
 * through lwip/debug.h.
 *
 * @author zhugengyu
 * @date   02.10.2026
 */

#ifndef FREEWEBCAM_LWIP_ARCH_CC_H
#define FREEWEBCAM_LWIP_ARCH_CC_H

#include <stdint.h>

/* --- ssize_t -------------------------------------------------------------- */

/*
 * lwIP's arch.h uses "is SSIZE_MAX defined?" as its probe for "does this libc
 * provide ssize_t?" and otherwise typedefs `int ssize_t` itself. Newlib does
 * provide ssize_t - in <sys/types.h>, and again in <stdio.h> as
 * `typedef _ssize_t ssize_t` (long) - but it hides SSIZE_MAX, because that is
 * a POSIX limit and this build is -std=c11 (strict ANSI). Left alone, lwIP's
 * `typedef int ssize_t` then collides with newlib's long one, in whichever
 * file happens to include both.
 */
#include <limits.h>
#include <sys/types.h>
#include <sys/time.h>	/* struct timeval, for the P7-2a TIMEVAL_PRIVATE=0 */
#define SSIZE_MAX	LONG_MAX

/* --- libc ---------------------------------------------------------------- */

/*
 * lwIP maps lwip_isdigit()/lwip_isspace()/... onto <ctype.h> unless told
 * otherwise, and newlib's ctype macros read the _ctype_ table, which this
 * freestanding image does not link. With this on, lwIP uses its own inline
 * range checks (lwip/arch.h) and the image stays libc-free.
 */
#define LWIP_NO_CTYPE_H	1

/* --- byte order ----------------------------------------------------------- */

#define LITTLE_ENDIAN	1234
#define BIG_ENDIAN	4321
#define BYTE_ORDER	LITTLE_ENDIAN

#define LWIP_PLATFORM_BYTESWAP	1
#define LWIP_PLATFORM_HTONS(x)	((uint16_t)__builtin_bswap16((uint16_t)(x)))
#define LWIP_PLATFORM_HTONL(x)	((uint32_t)__builtin_bswap32((uint32_t)(x)))

/* --- random --------------------------------------------------------------- */

static inline uint32_t lwip_platform_rand(void)
{
	uint64_t v;

	__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(v));
	/* Mix the low bits: the counter's bottom bits advance fast enough for
	 * a fragment id, the top bits make successive calls differ. */
	return (uint32_t)(v ^ (v >> 32));
}

#define LWIP_RAND()	lwip_platform_rand()

/* --- diagnostics ---------------------------------------------------------- */

/* Implemented in the adapter (lwip_diag.c). */
void lwip_arch_diag(const char *fmt, ...);
void lwip_arch_assert(const char *message, const char *file, int line);

/* lwIP calls LWIP_PLATFORM_DIAG(x) with x being the parenthesised argument
 * list of a printf-style call, so the macro has to splice it onto a function
 * name. */
#define LWIP_PLATFORM_DIAG(x)	do { lwip_arch_diag x; } while (0)
#define LWIP_PLATFORM_ASSERT(x)	lwip_arch_assert(x, __FILE__, __LINE__)

/* --- misc ----------------------------------------------------------------- */

#ifndef LWIP_UNUSED_ARG
#define LWIP_UNUSED_ARG(x)	(void)x
#endif

/* lwIP's s16_t/s32_t come from arch.h's typedefs over stdint; nothing to add. */

#endif /* FREEWEBCAM_LWIP_ARCH_CC_H */

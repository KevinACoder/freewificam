/**
 * @file    cmsis_compiler.h
 * @brief   Board stand-in for the CMSIS Core compiler abstraction header.
 * @author  zhugengyu
 * @date    07.10.2026
 *
 * The CMSIS Core header of the same name switches to a compiler-specific
 * header whose A-profile support is AArch32 (CPS instructions, CPSR reads);
 * it cannot serve an AArch64 image. This stand-in provides the subset the
 * CMSIS-FreeRTOS adapter needs and follows the upstream include name, so no
 * upstream file changes: keep this directory on the include path instead of
 * CMSIS/Core/Include.
 *
 * The macro set is target-neutral and mirrors the host stand-in in
 * os/cmsis2/host-compat; each target keeps its own copy so neither include
 * path reaches into the other's tree.
 */

#ifndef CMSIS_COMPILER_H
#define CMSIS_COMPILER_H

#if defined( __GNUC__ )

/* The CMSIS-RTOS2 API header defines some of these on its own, so every macro
 * is guarded: this header must stay compatible with whichever upstream header
 * is included first. */
#ifndef __ASM
#define __ASM                 __asm
#endif
#ifndef __INLINE
#define __INLINE              inline
#endif
#ifndef __STATIC_INLINE
#define __STATIC_INLINE       static inline
#endif
#ifndef __STATIC_FORCEINLINE
#define __STATIC_FORCEINLINE  static inline
#endif
#ifndef __NO_RETURN
#define __NO_RETURN           __attribute__( ( __noreturn__ ) )
#endif
#ifndef __USED
#define __USED                __attribute__( ( used ) )
#endif
#ifndef __WEAK
#define __WEAK                __attribute__( ( weak ) )
#endif
#ifndef __PACKED
#define __PACKED              __attribute__( ( packed ) )
#endif
#ifndef __PACKED_STRUCT
#define __PACKED_STRUCT       struct __attribute__( ( packed ) )
#endif
#ifndef __PACKED_UNION
#define __PACKED_UNION        union __attribute__( ( packed ) )
#endif
#ifndef __ALIGNED
#define __ALIGNED( x )        __attribute__( ( aligned( x ) ) )
#endif
#ifndef __RESTRICT
#define __RESTRICT            __restrict
#endif

#else
#error "unsupported compiler for the CMSIS compiler stand-in"
#endif

#endif /* CMSIS_COMPILER_H */

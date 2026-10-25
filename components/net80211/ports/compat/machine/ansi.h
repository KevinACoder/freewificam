/*
 * @file
 * @brief Empty machine/ansi.h shim for the compiler's <stddef.h>.
 *
 * The compiler's stddef.h takes an unconditional __NetBSD__ branch that
 * includes <machine/ansi.h> - the NetBSD target header that tells the compiler
 * which of the BSD type guards the target's headers already define. The port
 * defines __NetBSD__ for the imported sources, so that include happens on the
 * host and on newlib targets alike, where no such header exists.
 *
 * An empty copy is correct: with none of the BSD guards claimed, the compiler's
 * stddef.h falls back to defining size_t/ptrdef_t/wchar_t itself, which is
 * exactly what the C libraries here do (their size_t comes from the compiler's
 * stddef.h too, and a repeated identical typedef is legal C11).
 *
 * @design port-owned target-header shim: the same guard-claim mechanism the
 * component's compat tree uses for the other missing target headers
 * (compat/netbsd/machine/endian.h is the sibling case).
 */

#ifndef _WLAN_MACHINE_ANSI_H_
#define _WLAN_MACHINE_ANSI_H_

/* deliberately empty: see the file comment */

#endif /* _WLAN_MACHINE_ANSI_H_ */

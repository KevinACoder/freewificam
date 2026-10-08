/*
 * @file
 * @brief inttypes shim for the imported sources: the C library owns the values
 *        when it has them, the target gets the LP64 basics.
 *
 * The component's prelude includes <inttypes.h> because one imported source
 * concatenates PRIu64 into a diagnostic string. A hosted build resolves that to
 * the C library's header; a bare-metal target toolchain ships no inttypes.h at
 * all (the freestanding surface stops at <stdint.h>), so this port owns the
 * name: it forwards to a real header when one exists (include_next, so the C
 * library keeps its macro spelling on the host) and otherwise defines the
 * printing macros for the target's own data model.
 *
 * @design port-owned shadow shim: the same guard-claim-plus-forwarding
 * mechanism as ports/net80211/compat/sys/errno.h. AArch64 LP64 facts: uint64_t
 * is unsigned long, uint32_t is unsigned int (AAPCS64), which is what the
 * fallback macros below encode.
 */

#ifndef _WLAN_INTTYPES_H_
#define _WLAN_INTTYPES_H_

#include <stdint.h>

#if defined(__has_include_next)
#if __has_include_next(<inttypes.h>)
#include_next <inttypes.h>
#endif
#endif

/* Fallbacks for a freestanding toolchain without the header (AArch64 LP64). */
#ifndef PRIu64
#define PRIu64 "lu"
#endif
#ifndef PRId64
#define PRId64 "ld"
#endif
#ifndef PRIx64
#define PRIx64 "lx"
#endif
#ifndef PRIX64
#define PRIX64 "lX"
#endif
#ifndef PRIu32
#define PRIu32 "u"
#endif
#ifndef PRId32
#define PRId32 "d"
#endif
#ifndef PRIx32
#define PRIx32 "x"
#endif

#endif /* _WLAN_INTTYPES_H_ */

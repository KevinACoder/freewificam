/*
 * @file
 * @brief errno shim for the imported sources: the C library owns the values.
 *
 * The component's compat copy of this name only included <errno.h>, which works
 * on a host (glibc defines the values there) but not on a target whose C library
 * keeps them in its own <sys/errno.h> that this file would shadow. The port
 * therefore owns the name: it forwards to the library's header with
 * include_next and then supplies the NetBSD names a C library may lack, so the
 * imported sources have one vocabulary on every backend.
 *
 * @design port-owned shadow shim: the same guard-claim-plus-forwarding mechanism
 * as ports/net80211/compat/sys/cdefs.h. The component's copy retires with the
 * trim (see the component's DIVERGENCES ledger).
 */

#ifndef _WLAN_SYS_ERRNO_H_
#define _WLAN_SYS_ERRNO_H_

#include_next <sys/errno.h>
#include <errno.h>

/* The imported sources pass these through to their callers; they never compare
 * against the raw values, so a C library that already defines one keeps its own. */
#ifndef ETIMEDOUT
#define ETIMEDOUT 110
#endif
#ifndef ENETRESET
#define ENETRESET 102
#endif
#ifndef ENOBUFS
#define ENOBUFS 105
#endif

#endif /* _WLAN_SYS_ERRNO_H_ */

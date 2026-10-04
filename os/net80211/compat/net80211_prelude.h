/*
 * net80211_prelude.h - force-included preamble for every translation unit
 * that lives in the net80211 world (the vendored protocol core, the AES
 * subset, and the first-party compat units themselves).
 *
 * S8 net80211-adapt spec FR-14: os/net80211/compat/ is the single
 * dispatch point for the BSD shadow headers, and this preamble claims the
 * NetBSD identity plus the vocabulary the imported sources select with.
 * The freewebcam base is freestanding (no libc of its own beyond
 * os/baremetal/minilibc.c), so unlike the prior project's embox port
 * there are no foreign header guards to pre-empt - the compat tree is
 * simply first in the include order for these units.
 */

#ifndef NET80211_PRELUDE_H
#define NET80211_PRELUDE_H

/* The freestanding headers the toolchain ships, pulled FIRST and in
 * this order: newlib's machine/ansi.h suppresses GCC stddef.h's size_t
 * typedef once __NetBSD__ is visible, and stdarg.h pulls stddef.h in
 * need-mode which arms its guard against a later full include - so
 * <stddef.h> must be the first include of the translation unit and the
 * NetBSD identity claim must come after it. */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

/* The imported sources select NetBSD kernel semantics with __NetBSD__;
 * defined after the base-type headers for the ansi.h reason above. */
#ifndef __NetBSD__
#define __NetBSD__ 1
#endif

/* errno vocabulary this newlib profile's <errno.h> does not carry but
 * the imported core uses (values are the platform errno conventions;
 * guarded so any real definition wins) */
#ifndef ENOTTY
#define ENOTTY 25
#endif
#ifndef ENOBUFS
#define ENOBUFS 105
#endif
#ifndef ENETRESET
#define ENETRESET 122
#endif

/* The sysctl configuration tree compiles out: the vocabulary lives in
 * the compat sys/sysctl.h shadow (sysctl_createv always fails, every
 * caller takes its error path). */
#define IEEE80211_PORT_NO_SYSCTL 1

#endif /* NET80211_PRELUDE_H */

/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * drvcheck_overlay.h - compile-check-only overlay for the P6-a 判据 2
 * three-driver sweep (gcc -fsyntax-only against the [LNX] 7.3-rc2 tree;
 * the drivers are never linked into the image).
 *
 * Why this exists: the wlan drivers' include chain enters through
 * include/net/mac80211.h, whose thin header hard-wires the PRIOR LINE's
 * core.h (same include guard), so API gaps are fixed in two places:
 *   - kernel-face gaps go into components/linuxkpi/linuxkpi_core.h
 *     (timer_container_of, DUMP_PREFIX_*, kmalloc_obj, WQ_PERCPU, S8_MIN);
 *   - 80211-face enum constants below ride here because their truth lives
 *     in the [EXPL] net shape headers this check references but does not
 *     migrate (SPEC S6/linuxkpi §3.1: include/net 不迁, P6-2 域).
 * Every entry cites its [LNX] source.  The overlay defines ONLY names the
 * old shape headers lack - a name that later collides with an enum body
 * would break the parse, so each addition needs the collision check.
 *
 * Known-excluded from the sweep (struct-shaped 7.3 API growth in the
 * 80211 glue face, P6-2/S8 domain): rtw88 mac80211.c, wow.c.
 */

#ifndef LKPI_DRVCHECK_OVERLAY_H
#define LKPI_DRVCHECK_OVERLAY_H

/* kernel-face aliases the rtw88 TUs cannot get from our core.h: their
 * include chain enters via include/net/mac80211.h, whose thin header
 * hard-wires the PRIOR LINE's core.h (same include guard), so anything
 * that header lacks must ride here.  Bodies are identical to our core.h
 * spellings - identical redefinition is silent if both are seen. */

/* [LNX] include/linux/timer.h:132 (6.15+ rename of from_timer) */
#define	timer_container_of(var, timer, field)	container_of(timer, typeof(*var), field)

/* [LNX] include/linux/kernel.h print_hex_dump prefix args */
#define	DUMP_PREFIX_NONE	0
#define	DUMP_PREFIX_OFFSET	1
#define	DUMP_PREFIX_ADDRESS	2

/* kbuild CONFIG_ convention (kconfig.h): defined iff the build passed -D */
#ifndef IS_ENABLED
#define	___is_defined(val)	____is_defined(__ARG_PLACEHOLDER_##val)
#define	__is_defined(val)	___is_defined(val)
#define	__ARG_PLACEHOLDER_1	0,
#define	____is_defined(arg1_or_junk)	__take_second_arg(arg1_or_junk 1, 0)
#define	__take_second_arg(first, second, ...)	second
#define	IS_ENABLED(option)	__is_defined(option)
#endif
#ifndef IS_REACHABLE
#define	IS_REACHABLE(option)	__is_defined(option)
#endif
#ifndef IS_BUILTIN
#define	IS_BUILTIN(option)	__is_defined(option)
#endif

/* [LNX] include/linux/ieee80211.h: IEEE80211_HT_MPDU_DENSITY_16 - the
 * value range grew past the shape header's enum (chip tables use it as
 * a register constant) */
#define	IEEE80211_HT_MPDU_DENSITY_16	((u16)16)

/* [LNX] include/linux/ieee80211.h: IEEE80211_VHT_MAX_AMPDU_<n>K family -
 * 7.x added the 128K..1024K encodings */
#define	IEEE80211_VHT_MAX_AMPDU_128K	((u16)5)
#define	IEEE80211_VHT_MAX_AMPDU_256K	((u16)6)
#define	IEEE80211_VHT_MAX_AMPDU_512K	((u16)7)
#define	IEEE80211_VHT_MAX_AMPDU_1024K	((u16)8)

#endif /* LKPI_DRVCHECK_OVERLAY_H */

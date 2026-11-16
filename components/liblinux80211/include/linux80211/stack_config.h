/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux80211/stack_config.h
 * @brief   The configuration face of the imported mac80211/cfg80211 tree.
 *
 * @design The kernel builds these translation units only for the features its
 *         Kconfig selects, so the feature macros are part of the source set's
 *         contract, not an accident of the image profile.  This header states
 *         them once, for the S0 compile and for the future stack build; it is
 *         force-included (`-include`) so every unit sees the same answers.
 *
 *         The imported TU set is the feature set: mac80211's mesh, debugfs,
 *         LED and PM units are not in `sources.mk`, so those symbols stay
 *         undefined; the minstrel rate-control unit IS imported, so its symbol
 *         is on and the default algorithm names it.  `STA_HASH_MAX_SIZE` is the
 *         kernel Kconfig's default (512).
 * @date    10.11.2026
 */

#ifndef _LINUX80211_STACK_CONFIG_H_
#define _LINUX80211_STACK_CONFIG_H_

/*
 * The imported tree is two halves behind two image symbols.  net/cfg80211.h's
 * IS_ENABLED() arms pick between the real declarations and "not configured"
 * inline stubs; with the symbols unset, util.c's real cfg80211_get_station()
 * collided with the stub (S0 run-40).  These two are the stack's own identity
 * and follow the image selection; S0/S1 add CONFIG_LIBLINUX80211_S0 through
 * their own face header because the inventory measures the whole tree, not
 * one image (tools/build/s0_face.h).
 */
#if CONFIG_LIBLINUX80211_CFG80211 || CONFIG_LIBLINUX80211_S0
#define CONFIG_CFG80211				1
#endif
#if CONFIG_LIBLINUX80211_MAC80211 || CONFIG_LIBLINUX80211_S0
#define CONFIG_MAC80211				1
#endif

/*
 * rate.h's two arms: with the symbol on, the real rc80211_minstrel_init() is
 * declared (and rc80211_minstrel_ht.c defines it); with it off, an inline stub
 * with the same name is defined and the real definition collides (S0 run-24:
 * redefinition of rc80211_minstrel_init).
 */
#if CONFIG_LIBLINUX80211_MAC80211 || CONFIG_LIBLINUX80211_S0
#define CONFIG_MAC80211_RC_MINSTREL		1
#define CONFIG_MAC80211_RC_DEFAULT		"minstrel_ht"

/* the STA hash bound sta_info.c's rhashtable initialiser reads */
#define CONFIG_MAC80211_STA_HASH_MAX_SIZE	512
#endif

#endif /* _LINUX80211_STACK_CONFIG_H_ */

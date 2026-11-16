/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * S0/S1 measure the imported tree, not one image.  The image selects
 * cfg80211 and/or mac80211 through Kconfig; the inventory compiles both
 * halves regardless, so its face says so once, before stack_config.h is
 * force-included.  This is a header, not a -D: the no-command-line-define
 * rule still holds.
 */
#ifndef FREEWIFICAM_S0_FACE_H
#define FREEWIFICAM_S0_FACE_H

#define CONFIG_LIBLINUX80211_S0 1

#endif /* FREEWIFICAM_S0_FACE_H */

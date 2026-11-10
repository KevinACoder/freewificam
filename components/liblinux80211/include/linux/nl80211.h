/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/nl80211.h
 * @brief   The wireless-configuration enumerations a driver's firmware and
 *          capability tables name.
 *
 * @design declaration-only compatibility face. The kernel reaches the
 *         configuration enumerations through the uapi header of this name; this
 *         port carries them in its wireless-configuration face
 *         (components/linuxkpi/include/net/cfg80211.h, which is where the uapi
 *         enumerations are transcribed at their computed positions), so this
 *         header is the include-path spelling of that face rather than a second
 *         copy: a consumer that includes only linux/nl80211.h still names the same
 *         enumerators. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_NL80211_H_
#define _LINUXKPI_LINUX_NL80211_H_

#include "../../linuxkpi_core.h"
/* The interface keeps these enumerations in the uapi header; the kernel build
 * resolves <linux/nl80211.h> there through its include/uapi path.  This face
 * is the same spelling: the uapi file is the one definition, carried verbatim
 * in include/uapi/linux/nl80211.h.  Including net/cfg80211.h here (as the face
 * used to) made the two headers circular and the enums incomplete. */
#include <uapi/linux/nl80211.h>

#endif /* _LINUXKPI_LINUX_NL80211_H_ */

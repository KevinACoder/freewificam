/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/rfkill.h
 * @brief   The rfkill switch surface a wireless driver's platform layer names.
 *
 * @design declaration-only compatibility face. The switch record and the type
 *         enumeration are the interface's own (include/uapi/linux/rfkill.h): the values
 *         are the block reasons a driver reports, so they are contract. This port has no
 *         rfkill switch; the entries whose bodies the compiled set reaches are stated in
 *         the contract header as the answers of a port without one, and a caller keeps
 *         its default path.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_RFKILL_H_
#define _LINUXKPI_LINUX_RFKILL_H_

#include "../../linuxkpi_core.h"

struct rfkill;

/* The wire enumerations are the uapi header's own file, carried verbatim by
 * the port (include/uapi/linux/rfkill.h); they used to be restated here as a
 * face.  The real include/net/cfg80211.h includes the uapi header by name, so
 * restating the enums made every translation unit that includes both report a
 * redeclaration.  One definition, in the file the interface keeps it in. */
#include <uapi/linux/rfkill.h>

/*
 * [LNX] include/linux/rfkill.h:239,313-316: `bool rfkill_soft_blocked(struct
 * rfkill *)` is a real entry point in the rfkill core and, in that header's
 * !IS_ENABLED(CONFIG_RFKILL) arm, an inline returning false. This port has no
 * rfkill switch and no rfkill core (the wiphy record's `rfkill` member is never
 * set), so the no-core arm is the honest mapping: the switch is never
 * soft-blocked. The iwlwifi caller reads it as "is the radio soft-blocked" and
 * takes its default (not-blocked) path, which is the interface's own answer when
 * there is no switch.
 */
/* [LNX] include/linux/rfkill.h:299-312: the polling pause/resume pair cfg80211's
 * wiphy registration calls.  The port has no rfkill switch (and therefore no
 * polling to pause); the calls keep their shape and do nothing, which is the
 * no-core arm's answer. */
static __always_inline __unused void
rfkill_pause_polling(struct rfkill *rfkill)
{
	(void)rfkill;
}

static __always_inline __unused void
rfkill_resume_polling(struct rfkill *rfkill)
{
	(void)rfkill;
}

static __always_inline __unused bool
rfkill_soft_blocked(struct rfkill *rfkill)
{
	(void)rfkill;
	return false;
}

#endif /* _LINUXKPI_LINUX_RFKILL_H_ */

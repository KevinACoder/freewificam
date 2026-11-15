/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   f429_ap_link.c
 * @brief  Case f429_ap_link: the link-cost probe for the imported hostapd AP
 *         stack on the STM32F429.
 *
 * This image exists to answer one question with the linker rather than with a
 * boot: what does the AP role cost in flash when the AP MLME is the imported
 * NXP hostapd (components/liblinux80211/hostap) instead of the port's own
 * minimal MLME?  A case that never references the stack would let
 * --gc-sections drop it, and the image size would then say nothing.
 *
 * So the whole case is two address-of expressions: they keep the AP entry and
 * its readiness poll in the link closure, and with them everything hostapd
 * calls (the daemon task, the AP MLME, the internal crypto).  Nothing is
 * started, no bus is touched, and the case cannot fail.
 *
 * The image is a measurement artifact.  A delivery image for the AP role on
 * this platform is either the imported stack driven end to end (if the link
 * cost fits the board's 1 MiB flash) or the port's minimal MLME
 * (components/liblinux80211/linuxkpi_ap_mlme.c); this case is neither.
 */

#include <stdint.h>

#include "cases.h"

#include "linux80211/hostap_ap.h"

int
case_f429_ap_link_run(void)
{
	/* volatile: the compiler must not fold the reference away, and the
	 * linker must keep the section the symbol lives in. */
	volatile const void *entry = (const void *)lkpi_hostap_start;
	volatile const void *ready = (const void *)lkpi_hostap_ready;

	CASE_ANCHOR("f429_ap_link: hostapd entry=%p ready=%p\n",
		    (void *)entry, (void *)ready);
	return 0;
}

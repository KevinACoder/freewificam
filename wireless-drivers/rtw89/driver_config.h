/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    driver_config.h
 * @brief   The configuration face of the vendored rtw89 (RTL8851BU, USB).
 *
 * @design The driver is written against a kernel Kconfig tree; this project has
 *         one configuration channel and no `-D` on any compile line
 *         (docs/BUILD.md), so the answers are restated here once and the header
 *         is force-included in the driver's flag world (tests/board/rtos/build/
 *         wl80211.mk) and nowhere else.  A feature is on only when its four
 *         symbols are named; everything else is off by omission.
 *
 *         The face is the one the previous line's board-proven RTOS image
 *         compiled: RTW89_CORE + RTW89_USB + RTW89_8851B + RTW89_8851BU.  The
 *         GT kernel selects more (RTW89_DEBUG, RTW89_DEBUGMSG, RTW89_LEDS,
 *         RTW89_LEDS_MC, CONFIG_PM), and each difference is logging, LED or
 *         suspend work, never the data path:
 *
 *           - CONFIG_RTW89_DEBUG off: the driver's `debug.c` (sysfs/debugfs
 *             register dump) is not compiled.  `rtw89_info`/`rtw89_warn`/
 *             `rtw89_err` are macro-level `dev_*` calls and stay; the bring-up's
 *             `Firmware version` line comes from `rtw89_info`, so the attach
 *             evidence does not depend on this symbol.
 *           - CONFIG_RTW89_DEBUGMSG off: `rtw89_debug()` is the header's inline
 *             no-op.  The board's console is 115200 baud and the driver's
 *             HCI/C2H paths would otherwise print; the GT rounds paid for that
 *             lesson on the 8189FTV arm.
 *           - CONFIG_RTW89_LEDS / RTW89_LEDS_MC off: no LED class in the image.
 *           - CONFIG_PM off: `wow.c` is not compiled and the suspend arms of
 *             `core.c`/`mac80211.c` stay closed; an RTOS image does not suspend.
 *
 *         The PCI bus and every chip other than 8851B are off; the target is
 *         the `0bda:b851` USB part.  The three `*_be` units the driver's own
 *         Makefile lists unconditionally are compiled (they are part of the
 *         core aggregate); the chip selection never reaches them, and the
 *         image's zero-undefined-symbol gate is the check.
 * @date    13.11.2026
 */

#ifndef _RTW89_DRIVER_CONFIG_H_
#define _RTW89_DRIVER_CONFIG_H_

/* --- the compiled set ----------------------------------------------------
 *
 * These four are the driver's own Kconfig selects for the 8851BU part:
 * RTW89_8851BU selects RTW89_CORE, RTW89_USB and RTW89_8851B.
 */
#define CONFIG_RTW89_CORE	1
#define CONFIG_RTW89_USB	1
#define CONFIG_RTW89_8851B	1
#define CONFIG_RTW89_8851BU	1

/* --- stated-off answers ---------------------------------------------------
 *
 * Left undefined on purpose; listed so the face is readable without diffing it
 * against the upstream Kconfig: RTW89_PCI, RTW89_8851BE, every other chip
 * (8852A/B/BT/C, 8922A/D), RTW89_DEBUG, RTW89_DEBUGMSG, RTW89_DEBUGFS,
 * RTW89_LEDS, RTW89_LEDS_MC, CONFIG_PM, CONFIG_ACPI, CONFIG_EFI.
 *
 * CONFIG_RTW89_DEBUGFS additionally requires cfg80211's debugfs, which this
 * kernel's cfg80211 does not carry; the debugfs entry points in debug.h are the
 * header's inline stubs.
 */

#endif /* _RTW89_DRIVER_CONFIG_H_ */

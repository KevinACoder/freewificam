/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    driver_config.h
 * @brief   The configuration face of the vendored rtw88 (RTL8821CU, USB).
 *
 * @design The driver is written against a kernel Kconfig tree; this project has
 *         one configuration channel and no `-D` on any compile line
 *         (docs/BUILD.md), so the answers are restated here once and the header
 *         is force-included in the driver's flag world (tests/board/rtos/build/
 *         Makefile, RTW88_CFLAGS) and nowhere else.  A feature is on only when
 *         its symbol is named; everything else is off by omission.
 *
 *         The four on symbols are the driver's own Kconfig selects for the
 *         0bda:c820 part: RTW88_8821CU selects RTW88_CORE, RTW88_USB and
 *         RTW88_8821C.  That is the same data-path set the GT Image reaches
 *         with CONFIG_RTW88_8821CU=y, and the 20 translation units the vendored
 *         sources.mk names.
 *
 *         The off answers are the GT build's optional units, none of which is
 *         the data path:
 *
 *           - RTW88_LEDS off: `led.c` is not compiled.  The target has no LED
 *             class; the GT Image compiles it because CONFIG_RTW88_LEDS=y.
 *           - CONFIG_PM off: `wow.c` is not compiled and the suspend arms of
 *             `main.c`/`mac80211.c` stay closed; an RTOS image does not
 *             suspend.  The GT Image compiles wow.c under CONFIG_PM.
 *           - RTW88_DEBUGFS off: cfg80211's debugfs is not in this stack; the
 *             debugfs entry points in `debug.h` are the header's inline stubs.
 *             `debug.c` itself is part of `rtw88_core-y` and stays in the set.
 *           - RTW88_DEBUG off: the driver's `rtw_dbg()` masks are compile-time
 *             no-ops beyond the error/warn/info levels.  The console is 115200
 *             baud and the HCI/efuse paths would otherwise print per transfer;
 *             the GT rounds paid for that lesson on the 8189FTV arm.
 *           - PCI, SDIO and every chip other than 8821C are off: the target is
 *             the USB part.  `main.c`'s chip table is built from the enabled
 *             CONFIG_RTW88_* symbols, so the face is also the chip selection.
 *
 *         Byte pin: linux-upstream `b98a1dfd4661` (the same pin as the GT
 *         Image and the rtw89 import); `wireless-drivers/rtw88/IMPORT.tsv` is
 *         the digest manifest and `import-driver.py --check` re-derives it.
 * @date    14.11.2026
 */

#ifndef _RTW88_DRIVER_CONFIG_H_
#define _RTW88_DRIVER_CONFIG_H_

/* --- the compiled set ----------------------------------------------------
 *
 * These four are the driver's own Kconfig selects for the 8821CU part:
 * RTW88_8821CU selects RTW88_CORE, RTW88_USB and RTW88_8821C.
 */
#define CONFIG_RTW88_CORE	1
#define CONFIG_RTW88_USB	1
#define CONFIG_RTW88_8821C	1
#define CONFIG_RTW88_8821CU	1

/* --- stated-off answers ---------------------------------------------------
 *
 * Left undefined on purpose; listed so the face is readable without diffing it
 * against the upstream Kconfig/Makefile: RTW88_PCI, RTW88_SDIO, RTW88_8822B,
 * RTW88_8822BE/BS/BU, RTW88_8822C, RTW88_8822CE/CS/CU, RTW88_8723X,
 * RTW88_8703B, RTW88_8723CS/D/DE/DS/DU, RTW88_8821CE/CS/AU, RTW88_88XXA,
 * RTW88_8821A, RTW88_8812A/AU, RTW88_8814A/AE/AU, RTW88_DEBUG,
 * RTW88_DEBUGFS, RTW88_LEDS, CONFIG_PM, CONFIG_ACPI, CONFIG_EFI.
 */

#endif /* _RTW88_DRIVER_CONFIG_H_ */

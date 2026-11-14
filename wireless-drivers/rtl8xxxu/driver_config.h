/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    driver_config.h
 * @brief   The configuration face of the vendored rtl8xxxu (RTL8188EU, USB).
 *
 * @design The driver is written against a kernel Kconfig tree; this project has
 *         one configuration channel and no `-D` on any compile line
 *         (docs/BUILD.md), so the answers are restated here once and the header
 *         is force-included in the driver's flag world (tests/board/rtos/build/
 *         Makefile, RTL8XXXU_CFLAGS) and nowhere else.  A feature is on only
 *         when its symbol is named; everything else is off by omission.
 *
 *         This driver is the simplest face of the four vendored trees: one
 *         symbol, because the module's `fileops` table names every chip it
 *         supports and the Makefile compiles all nine units unconditionally
 *         (`rtl8xxxu-y := core.o 8192e.o 8723b.o 8723a.o 8192c.o 8188f.o
 *         8188e.o 8710b.o 8192f.o`).  Chip selection is the run-time `id_table`
 *         match plus the firmware image, so CONFIG_RTL8XXXU=m is the whole GT
 *         build's answer and the whole face here.
 *
 *         The one stated-off answer is CONFIG_RTL8XXXU_UNTESTED: it only widens
 *         the usb id table with parts this bench does not carry (8192c.c and
 *         core.c's `#ifdef` block), and 0bda:8179 - the bench part - is in the
 *         driver's tested table (core.c's `untested` switch).
 *
 *         Two consequences of the driver's own `depends on LEDS_CLASS` are
 *         answered by the kernel-API face, not by this header, and both are
 *         already inert there: `led_classdev_register()` is the contract
 *         header's 0-returning macro (the image has no LED class), and the
 *         debugfs registration is the debugfs face's NULL-returning stub.
 *         Neither is on a data path.
 *
 *         Byte pin: linux-upstream `b98a1dfd4661` (the same tree the previous
 *         net80211 line vendored, byte-identical); `wireless-drivers/rtl8xxxu/
 *         IMPORT.tsv` is the digest manifest and `import-driver.py --check`
 *         re-derives it.
 * @date    14.11.2026
 */

#ifndef _RTL8XXXU_DRIVER_CONFIG_H_
#define _RTL8XXXU_DRIVER_CONFIG_H_

/* --- the compiled set ----------------------------------------------------
 *
 * The driver's single Kconfig symbol.  Its Kconfig lists
 * `depends on MAC80211 && USB` (both in the image) and `depends on LEDS_CLASS`
 * (answered inert by the kpi face), so this one define is the complete face.
 */
#define CONFIG_RTL8XXXU	1

/* --- stated-off answers ---------------------------------------------------
 *
 * Left undefined on purpose; listed so the face is readable without diffing it
 * against the upstream Kconfig: CONFIG_RTL8XXXU_UNTESTED (the untested-parts
 * id-table block only).
 */

#endif /* _RTL8XXXU_DRIVER_CONFIG_H_ */

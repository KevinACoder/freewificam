/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    kunit/visibility.h
 * @brief   The conditional-visibility and conditional-export spellings a driver's
 *          shared symbols wear for the unit-test build.
 *
 * @design declaration-only compatibility face. The interface (include/kunit/
 *         visibility.h) selects one of two arms on CONFIG_KUNIT: with the option
 *         on the visibility spelling is empty and the export spelling exports into
 *         the test namespace, and with it off the visibility spelling is `static`
 *         and the export spelling is empty. This port does not select the option,
 *         so the off arm is the reachable one and is what is stated here; a
 *         translation unit includes this header unconditionally (the vendored
 *         driver's own header does), which is why the names must exist even though
 *         the build has no unit-test consumer. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_KUNIT_VISIBILITY_H_
#define _LINUXKPI_KUNIT_VISIBILITY_H_

#include "../linuxkpi_core.h"

/* [LNX] include/kunit/visibility.h:28-29: the CONFIG_KUNIT-off arm. */
#define	VISIBLE_IF_KUNIT	static
#define	EXPORT_SYMBOL_IF_KUNIT(symbol)

#endif /* _LINUXKPI_KUNIT_VISIBILITY_H_ */

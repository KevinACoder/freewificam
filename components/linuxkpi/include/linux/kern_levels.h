/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/kern_levels.h
 * @brief   The message-level prefixes the interface's logging macros put in front of a
 *          format string.
 *
 * @design declaration-only compatibility face. The interface (include/linux/
 *         kern_levels.h) spells each level as a start-of-header byte plus the level
 *         digit, and the console decodes it; four of them are already stated in this
 *         component's contract header (components/linuxkpi/linuxkpi_core.h, its logging
 *         section: KERN_ERR, KERN_WARNING, KERN_INFO, KERN_DEBUG) and all four are the
 *         empty string there, because this project's sink receives a complete line and
 *         does its own stamping rather than parsing a level prefix out of the text. The
 *         remaining levels the vendored logging macros name are stated here in that same
 *         model - the empty string - instead of the interface's escape sequence, so the
 *         two halves of the set cannot disagree; the four in the contract header are not
 *         repeated. When a sink that stamps levels arrives, this file and those four
 *         move together, which is the single-definition rule the project applies to every
 *         mechanism. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_KERN_LEVELS_H_
#define _LINUXKPI_LINUX_KERN_LEVELS_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/kern_levels.h: the levels the vendored macros name that the
 * contract header does not state; see the file comment for why they are empty. */
#define	KERN_EMERG	""
#define	KERN_ALERT	""
#define	KERN_CRIT	""
#define	KERN_NOTICE	""
#define	KERN_DEFAULT	""	/* the interface's own value is empty too */
#define	KERN_CONT	""

#endif /* _LINUXKPI_LINUX_KERN_LEVELS_H_ */

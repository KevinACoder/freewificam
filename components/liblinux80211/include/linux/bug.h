/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/bug.h
 * @brief   The include Linux code names for BUG()/WARN()/BUILD_BUG_ON(); the
 *          vocabulary already lives in the contract header.
 *
 * @design declaration-only compatibility face.  The interface's bug.h is the
 *         home of the diagnostic macros (BUG, WARN, WARN_ON, BUILD_BUG_ON and
 *         their _ONCE/_MSG variants) plus the trap-type enum.  In this port the
 *         macros live in linuxkpi_core.h (its "diagnostics" section), whose
 *         WARN_ON prints through the component's line sink and whose BUG_ON
 *         parks through lkpi_fatal - the same bodies every existing driver
 *         already compiles against.  Restating them here would be the second
 *         spelling of one mechanism, so this header carries no definitions of
 *         its own: it exists because the real net/mac80211.h and the real
 *         net/cfg80211.h include it by name, and the contract header answers.
 *         The trap-type enum is included because it is the only name in the
 *         interface's header that the new stack's headers read; it is data,
 *         not mechanism.
 * @date    10.11.2026
 */

#ifndef _LINUXKPI_LINUX_BUG_H_
#define _LINUXKPI_LINUX_BUG_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/bug.h:14-19: what a trap was.  The port's BUG_ON parks
 * and its WARN_ON prints; no caller in the compiled set reads the enum, but a
 * face that names WARN/BUG should carry the interface's vocabulary whole. */
enum bug_trap_type {
	BUG_TRAP_TYPE_NONE = 0,
	BUG_TRAP_TYPE_WARN = 1,
	BUG_TRAP_TYPE_BUG = 2,
};

#endif /* _LINUXKPI_LINUX_BUG_H_ */

/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/capability.h
 * @brief   The include Linux code names for capability checks; this port has
 *          one privileged context.
 *
 * @design declaration-only compatibility face.  The interface's header owns the
 *         capability vocabulary (CAP_* numbers, capable(), ns_capable(), the
 *         cred records).  This port runs every component in the same privileged
 *         context and has no credential model, so every check the compiled stack
 *         reaches answers "allowed" - the same answer the interface gives a
 *         caller with CAP_NET_ADMIN, which is the capability the wireless
 *         control plane requires.  A caller that needs a *different* answer
 *         (a negative test) would be the falsifier: none exists in the compiled
 *         set.  Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    10.11.2026
 */

#ifndef _LINUXKPI_LINUX_CAPABILITY_H_
#define _LINUXKPI_LINUX_CAPABILITY_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/uapi/linux/capability.h: the capability numbers the control
 * plane names.  Only the networking pair is stated: they are the ones the
 * wireless stack tests, and an unused number would be a claim this port does
 * not make. */
#define	CAP_NET_ADMIN		12
#define	CAP_NET_RAW		13

/* [LNX] include/linux/capability.h: `bool capable(int cap)`.  One privileged
 * context: allowed, for the capability the caller names. */
static __always_inline __unused bool
capable(int cap)
{
	(void)cap;
	return true;
}

static __always_inline __unused bool
ns_capable(void *ns, int cap)
{
	(void)ns;
	(void)cap;
	return true;
}

#endif /* _LINUXKPI_LINUX_CAPABILITY_H_ */

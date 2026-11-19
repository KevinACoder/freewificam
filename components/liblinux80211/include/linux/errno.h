/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/errno.h
 * @brief   The include a driver names for the error numbers; the numbers are the
 *          platform's on a hosted build and the contract header's on the target.
 *
 * @design declaration-only compatibility face. The interface (include/linux/errno.h,
 *         which pulls the uapi numbers) defines the errno set; the set this component
 *         serves is stated in the contract header (components/linuxkpi/
 *         linuxkpi_core.h, its error-number section), together with the project's
 *         private ENOTSUPP.
 *
 *         The hosted branch exists because this face sits in the middle of the C
 *         library's own errno chain: glibc's <bits/errno.h> includes <linux/errno.h>,
 *         so on a hosted build this header is reached from inside <errno.h> and
 *         anything it drags in lands in the middle of the library's declarations -
 *         pulling the contract header in there leaked its rename macros
 *         (timer_delete and friends) into <time.h> and broke every host build.
 *         The hosted branch therefore serves the platform's own numbers
 *         (<asm/errno.h>, the same values) and the project's private ENOTSUPP
 *         alone; the target build has no system headers and takes the contract
 *         header. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    10.10.2026
 */

#ifndef _LINUXKPI_LINUX_ERRNO_H_
#define _LINUXKPI_LINUX_ERRNO_H_

#if defined(__linux__)
/* hosted: the platform's numbers, reached without the contract header so the
 * C library's errno chain stays clean.  ENOTSUPP is the kernel's private one
 * and the platform does not carry it; the value matches the contract header's
 * (upstream include/linux/errno.h:27). */
#include <asm/errno.h>
#ifndef ENOTSUPP
#define ENOTSUPP 524
#endif
#else
#include "../../linuxkpi_core.h"

/*
 * [LNX] include/uapi/asm-generic/errno.h:84,109: the two numbers the compiled set
 * names that the contract header's error-number block does not (yet) state. They are
 * the platform's values (ENETDOWN 100, ENOMEDIUM 123) and are guarded so a future
 * statement in the contract header wins without a redefinition.
 */
#ifndef ENETDOWN
#define ENETDOWN 100
#endif
#ifndef ENOMEDIUM
#define ENOMEDIUM 123
#endif
/*
 * [LNX] include/uapi/asm-generic/errno.h:33,46,104,120 (the numbers are the
 * asm-generic ones; include/linux/errno.h is the in-kernel spelling):
 * EBADE, ENOSR, ENAVAIL and EROM are the error numbers brcmfmac's firmware
 * protocol answers with (fwil.c's "invalid exchange", the flow-control ring's
 * "out of streams resources", the dongle's "not available" and "read-only
 * memory"). This platform's <asm/errno.h> has no such names, so they are stated
 * here with the interface's values, guarded like the two above.
 */
/* [LNX] include/linux/errno.h:19: the in-kernel "this device is not ready, probe
 * again later" answer. A driver's probe returns it when a dependency has not arrived
 * (brcmfmac's platform/OF path, common.c:564); the value is the interface's. */
#ifndef EPROBE_DEFER
#define EPROBE_DEFER 517
#endif
/* [LNX] include/linux/errno.h:14: the private "a signal interrupted this sleep, the
 * caller should restart" answer. It has no public errno number because it never
 * leaves the kernel; brcmfmac's SDIO control-frame wait returns it when a pending
 * signal ended the wait (sdio.c:3281). The value is the interface's. */
#ifndef ERESTARTSYS
#define ERESTARTSYS 512
#endif
#ifndef EBADE
#define EBADE 52
#endif
#ifndef ENOSR
#define ENOSR 63
#endif
#ifndef ENAVAIL
#define ENAVAIL 119
#endif
#ifndef EROM
#define EROM 120
#endif
#endif

#endif /* _LINUXKPI_LINUX_ERRNO_H_ */

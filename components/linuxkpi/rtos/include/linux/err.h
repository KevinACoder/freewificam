/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/err.h
 * @brief   The error-pointer header a driver names, over the error-pointer
 *          vocabulary the contract header already carries.
 *
 * @design compatibility face over the contract header's error-pointer section. The
 *         interface (include/linux/err.h) owns the whole error-pointer vocabulary:
 *         IS_ERR_VALUE / ERR_PTR / PTR_ERR / IS_ERR / IS_ERR_OR_NULL / ERR_CAST /
 *         PTR_ERR_OR_ZERO. The contract header already states the first five
 *         (components/linuxkpi/linuxkpi_core.h, its "error pointers" section), so
 *         this file does not restate them - a second copy of any of the five would
 *         be the same mechanism written twice. It names the interface's header for
 *         a consumer that includes it and adds the one helper the compiled set
 *         names that the contract header does not carry, PTR_ERR_OR_ZERO.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_ERR_H_
#define _LINUXKPI_LINUX_ERR_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/err.h:125-131: the error code of an error pointer, or zero.
 * Stated over the contract header's IS_ERR()/PTR_ERR() (the interface's own two
 * helpers, kept identical there). */
static __always_inline __unused int
PTR_ERR_OR_ZERO(const void *ptr)
{
	if (IS_ERR(ptr))
		return (int)PTR_ERR(ptr);
	return 0;
}

#endif /* _LINUXKPI_LINUX_ERR_H_ */

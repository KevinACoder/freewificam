/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/firmware.h
 * @brief   The firmware object a driver loads through, and the two calls that fetch
 *          and release it.
 *
 * @design declaration-only compatibility face. In this project the bytes come from a
 *         compiled-in registry (a first-party table of name -> blob) rather than from a
 *         filesystem, so `request_firmware` is a lookup; the target has no userland to
 *         answer a udev-style fallback, so the asynchronous spelling the 8821CU probe
 *         path names is served by completing through its continuation rather than by
 *         deferring. The structure layout follows the interface (include/linux/firmware.h)
 *         because drivers read `size`/`data` directly. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    08.10.2026
 */

#ifndef _LINUXKPI_LINUX_FIRMWARE_H_
#define _LINUXKPI_LINUX_FIRMWARE_H_

#include "../../linuxkpi_core.h"

struct module;

struct firmware {
	size_t		size;
	const u8	*data;

	/* Private field of the loader; drivers must not touch it. */
	void		*priv;
};

/*
 * Fetch a blob by the name the driver asked for. Returns 0 and a pointer to a
 * borrowed description on success; on failure a negative errno and no allocation.
 * The blob itself is owned by the registry and is not copied.
 */
int request_firmware(const struct firmware **firmware_p, const char *name,
		     struct device *device);

/*
 * The asynchronous spelling of the same lookup. The interface's contract is that the
 * caller is not blocked and the continuation runs with the result; this project's
 * registry is a synchronous lookup, so the call is served by completing through the
 * continuation. [LNX] include/linux/firmware.h:109. The declaration is what the 8821CU
 * probe path names; its body arrives with the firmware-registry work.
 */
int request_firmware_nowait(struct module *module, bool uevent, const char *name,
			    struct device *device, gfp_t gfp, void *context,
			    void (*cont)(const struct firmware *fw, void *context));

/* Release a description obtained from request_firmware(). */
void release_firmware(const struct firmware *firmware);

#endif /* _LINUXKPI_LINUX_FIRMWARE_H_ */
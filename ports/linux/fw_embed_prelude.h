/*
 * @file
 * @brief Kernel-world prelude for the compiled-in firmware registry.
 *
 * The registry and the blob are the RTOS line's first-party files
 * (components/linuxkpi/fw*), imported verbatim. Their only kernel dependencies
 * are alloc, string, errno and the device/module types; the kernel's
 * <linux/firmware.h> supplies struct firmware. The prelude keeps the imported
 * files byte-identical instead of adding includes to them.
 */

#ifndef WLAN_FW_EMBED_PRELUDE_H
#define WLAN_FW_EMBED_PRELUDE_H

#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/device.h>

/* The imported blobs' registration entry points (called from the module init). */
int lkpi_fw_rtw8821c_fw_register(void);
int lkpi_fw_rtw8851b_fw_register(void);

#endif

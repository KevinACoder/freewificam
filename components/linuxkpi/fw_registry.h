/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    fw_registry.h
 * @brief   The compiled-in firmware registry: the loader this project has instead of a
 *          filesystem.
 *
 * @design static table + lookup by name (registry pattern), the carrier every blob in
 *         this project uses: a blob translation unit registers its bytes at start-up
 *         and the loader resolves a driver's requested name to them. The table is one
 *         object with two consumers - the Linux face's `request_firmware`
 *         (include/linux/firmware.h) and the net80211 port's firmware entry point - so
 *         the net80211 side asks this registry rather than carrying a second table.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20
 *         (first-party declaration face); the registry-and-lookup shape is the one the
 *         previous line used for its own blobs.
 * @date    08.10.2026
 */

#ifndef _LINUXKPI_FW_REGISTRY_H_
#define _LINUXKPI_FW_REGISTRY_H_

#include <stddef.h>
#include <stdint.h>

/*
 * One registered blob. The bytes stay owned by the registering translation unit (they
 * are compiled-in constants); the registry only points at them.
 */
struct lkpi_fw_entry {
	const char	*name;
	const uint8_t	*data;
	size_t		size;
};

/*
 * Register a blob under the name a driver will ask for. Called from start-up code
 * before any consumer runs (the registry has no lock by design: it is filled once, on
 * the initialisation path, and only read afterwards). Returns 0, or a negative value
 * when the table is full or the name is already taken.
 */
int lkpi_fw_register(const char *name, const uint8_t *data, size_t size);

/*
 * Resolve a name to its blob. Returns the entry (borrowed) or NULL when no blob is
 * registered under that name.
 */
const struct lkpi_fw_entry *lkpi_fw_registry_find(const char *name);

#endif /* _LINUXKPI_FW_REGISTRY_H_ */

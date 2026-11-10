/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    fw_registry.h
 * @brief   The compiled-in firmware registry: the loader this project has
 *          instead of a filesystem.
 *
 * @design a fixed table of borrowed name/data/size entries, filled once at
 *         start-up (before any driver probe) and read-only afterwards.  The
 *         stack and the driver ask for a blob by the name the interface's
 *         request_firmware() would use; the table answers.  No lock: the table
 *         is filled before the threads that read it exist.
 * @date    10.11.2026
 */

#ifndef _LINUX80211_FW_REGISTRY_H_
#define _LINUX80211_FW_REGISTRY_H_

#include <stddef.h>
#include <stdint.h>

struct lkpi_fw_entry {
	const char	*name;
	const uint8_t	*data;
	size_t		size;
};

/* Register a blob under the name a driver will request.  Idempotent for the
 * same name/data; a full table is a programming error, reported as -ENOSPC. */
int lkpi_fw_register(const char *name, const uint8_t *data, size_t size);

/* Resolve a request name to its entry (borrowed) or NULL when absent. */
const struct lkpi_fw_entry *lkpi_fw_registry_find(const char *name);

#endif /* _LINUX80211_FW_REGISTRY_H_ */

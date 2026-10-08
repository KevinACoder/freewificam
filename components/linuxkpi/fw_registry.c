/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    fw_registry.c
 * @brief   The compiled-in firmware registry and the two calls the Linux face exposes
 *          over it.
 *
 * @design registry with a fixed table; the entries are borrowed pointers, so a lookup
 *         copies nothing and releasing a description frees nothing. Reference for the
 *         shape: components/linuxkpi/include/linux/usb.h:1-20 (declaration face) and
 *         fw_registry.h in this directory (the contract implemented here).
 * @date    08.10.2026
 */

#include "fw_registry.h"
#include <linux/firmware.h>

/*
 * Slots: every blob this project compiles in needs one, and the table is deliberately
 * small - it is filled at start-up and a full table is a programming error, not a
 * runtime condition to grow around.
 */
#define	LKPI_FW_SLOTS	8

static struct lkpi_fw_entry lkpi_fw_table[LKPI_FW_SLOTS];

int lkpi_fw_register(const char *name, const uint8_t *data, size_t size)
{
	int i;

	if (name == NULL || data == NULL || size == 0)
		return -EINVAL;

	for (i = 0; i < LKPI_FW_SLOTS; i++) {
		if (lkpi_fw_table[i].name == NULL) {
			lkpi_fw_table[i].name = name;
			lkpi_fw_table[i].data = data;
			lkpi_fw_table[i].size = size;
			return 0;
		}
		if (lkpi_fw_table[i].name == name ||
		    (lkpi_fw_table[i].data == data)) {
			/* Already registered (the same blob or the same name):
			 * idempotent, so a start-up path can call it twice. */
			return 0;
		}
	}
	return -ENOSPC;
}

const struct lkpi_fw_entry *lkpi_fw_registry_find(const char *name)
{
	int i;

	if (name == NULL)
		return NULL;

	for (i = 0; i < LKPI_FW_SLOTS; i++) {
		if (lkpi_fw_table[i].name == NULL)
			continue;
		if (strcmp(lkpi_fw_table[i].name, name) == 0)
			return &lkpi_fw_table[i];
	}
	return NULL;
}

/*
 * The Linux face. The description is allocated (as the interface's loader does) and
 * points at the table's bytes, which are borrowed: the caller may read `size` and
 * `data` and must hand the description back through release_firmware().
 */
int request_firmware(const struct firmware **firmware_p, const char *name,
		     struct device *device)
{
	const struct lkpi_fw_entry *entry;
	struct firmware *view;

	(void)device;

	if (firmware_p == NULL || name == NULL)
		return -EINVAL;

	entry = lkpi_fw_registry_find(name);
	if (entry == NULL)
		return -ENOENT;

	view = kmalloc(sizeof(*view), GFP_KERNEL);
	if (view == NULL)
		return -ENOMEM;

	view->size = entry->size;
	view->data = entry->data;
	view->priv = (void *)entry;
	*firmware_p = view;

	return 0;
}

void release_firmware(const struct firmware *firmware)
{
	/* The bytes belong to the table; only the description is ours to free. */
	kfree((void *)firmware);
}

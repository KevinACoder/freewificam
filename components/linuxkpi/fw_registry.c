/* SPDX-License-Identifier: BSD-3-Clause */
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
#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

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
/*
 * A view whose bytes came from the image rather than from the table.  The
 * registry keeps borrowed pointers and frees only the description; a loaded
 * file owns its buffer, so the view carries the pointer to it and
 * release_firmware() tells the two apart by priv (the table entry vs NULL).
 */
struct lkpi_fw_owned {
	struct firmware view;	/* must stay first: the release recovers it */
	uint8_t *data;
};

/*
 * The registry first, the image second.  A module built without a compiled-in
 * blob still probes when the firmware sits at /lib/firmware: this kernel has no
 * userland firmware helper in the initramfs, so the file is read directly
 * through the VFS instead of going through firmware_request()'s fallback.
 */
static const struct firmware *lkpi_fw_lookup(const char *name)
{
	const struct lkpi_fw_entry *entry;
	struct lkpi_fw_owned *owned;
	struct firmware *view;
	struct file *filp;
	loff_t pos = 0;
	ssize_t got;
	size_t len;
	char path[256];

	entry = lkpi_fw_registry_find(name);
	if (entry != NULL) {
		view = kmalloc(sizeof(*view), GFP_KERNEL);
		if (view == NULL)
			return ERR_PTR(-ENOMEM);
		view->size = entry->size;
		view->data = entry->data;
		view->priv = (void *)entry;
		return view;
	}

	if (strlen(name) + sizeof("/lib/firmware/") > sizeof(path))
		return ERR_PTR(-ENAMETOOLONG);
	snprintf(path, sizeof(path), "/lib/firmware/%s", name);
	filp = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(filp))
		return ERR_CAST(filp);

	len = i_size_read(file_inode(filp));
	if (len == 0) {
		filp_close(filp, NULL);
		return ERR_PTR(-EINVAL);
	}

	owned = kmalloc(sizeof(*owned), GFP_KERNEL);
	if (owned == NULL) {
		filp_close(filp, NULL);
		return ERR_PTR(-ENOMEM);
	}
	owned->data = vmalloc(len);
	if (owned->data == NULL) {
		filp_close(filp, NULL);
		kfree(owned);
		return ERR_PTR(-ENOMEM);
	}

	got = kernel_read(filp, owned->data, len, &pos);
	filp_close(filp, NULL);
	if (got != (ssize_t)len) {
		vfree(owned->data);
		kfree(owned);
		return ERR_PTR(-EIO);
	}

	owned->view.size = len;
	owned->view.data = owned->data;
	owned->view.priv = NULL;

	return &owned->view;
}

int request_firmware(const struct firmware **firmware_p, const char *name,
		     struct device *device)
{
	const struct firmware *fw;

	(void)device;

	if (firmware_p == NULL || name == NULL)
		return -EINVAL;

	fw = lkpi_fw_lookup(name);
	if (IS_ERR(fw))
		return PTR_ERR(fw);

	*firmware_p = fw;

	return 0;
}

void release_firmware(const struct firmware *firmware)
{
	struct lkpi_fw_owned *owned;

	if (firmware == NULL)
		return;

	if (firmware->priv != NULL) {
		/* The bytes belong to the table; only the description is ours. */
		kfree((void *)firmware);
		return;
	}

	owned = container_of(firmware, struct lkpi_fw_owned, view);
	vfree(owned->data);
	kfree(owned);
}

/*
 * [LNX] include/linux/firmware.h: `firmware_request_nowarn()` is the interface's
 * spelling for "load this, and do not print a message when it is absent" - the
 * optional-firmware path (iwlwifi asks for the PNVM this way). The registry is the
 * only loader and it is silent already, so the difference from request_firmware()
 * is exactly nothing here; the entry point exists so the caller compiles and reads
 * unchanged, and the name says which of the two contracts it is under.
 */
int firmware_request_nowarn(const struct firmware **fw, const char *name,
			    struct device *device)
{
	return request_firmware(fw, name, device);
}

/*
 * The asynchronous spelling. The interface's contract is that the caller is not
 * blocked and the continuation runs with the result; this project's registry is
 * a synchronous lookup with no filesystem or userland helper behind it, so the
 * call is served by completing through the caller's continuation rather than by
 * deferring it. The continuation therefore runs before this call returns, which
 * is the same observable order the caller sees (it waits on its own completion
 * afterwards); a lookup miss still runs the continuation, with no firmware, as
 * the interface's async spelling does.
 */
int request_firmware_nowait(struct module *module, bool uevent, const char *name,
			    struct device *device, gfp_t gfp, void *context,
			    void (*cont)(const struct firmware *fw, void *context))
{
	const struct firmware *fw;

	(void)module;
	(void)uevent;
	(void)device;
	(void)gfp;

	if (name == NULL || cont == NULL)
		return -EINVAL;

	fw = lkpi_fw_lookup(name);
	if (IS_ERR(fw))
		fw = NULL;

	cont(fw, context);

	return 0;
}

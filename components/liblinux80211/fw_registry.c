/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    fw_registry.c
 * @brief   The compiled-in firmware registry and the Linux firmware face over it.
 *
 * @design registry with a fixed table; the entries are borrowed pointers, so a
 *         lookup copies nothing and releasing a description frees nothing but
 *         the description.  The target has no filesystem and no udev helper, so
 *         this registry is the only loader: a miss is a miss, and the caller
 *         sees the interface's negative errno (or a NULL continuation for the
 *         asynchronous spelling), never a fake blob.
 * @date    10.11.2026
 */

#include <stdint.h>
#include <string.h>

#include "linuxkpi_core.h"

#include "fw_registry.h"
#include <linux/firmware.h>

#define	LKPI_FW_SLOTS	8

static struct lkpi_fw_entry lkpi_fw_table[LKPI_FW_SLOTS];

int
lkpi_fw_register(const char *name, const uint8_t *data, size_t size)
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
		    lkpi_fw_table[i].data == data)
			return 0;	/* idempotent: a start-up may run twice */
	}
	return -ENOSPC;
}

const struct lkpi_fw_entry *
lkpi_fw_registry_find(const char *name)
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
 * The description is allocated (as the interface's loader does) and points at
 * the table's borrowed bytes; release_firmware() frees only the description.
 * This is the shape of the previous line's registry, kept because the driver
 * reads `size`/`data` directly.
 */
static const struct firmware *
lkpi_fw_lookup(const char *name)
{
	const struct lkpi_fw_entry *entry;
	struct firmware *view;

	entry = lkpi_fw_registry_find(name);
	if (entry == NULL)
		return ERR_PTR(-ENOENT);
	view = kmalloc(sizeof(*view), GFP_KERNEL);
	if (view == NULL)
		return ERR_PTR(-ENOMEM);
	view->size = entry->size;
	view->data = entry->data;
	view->priv = (void *)entry;
	return view;
}

int
request_firmware(const struct firmware **firmware_p, const char *name,
		 struct device *device)
{
	const struct firmware *fw;

	(void)device;
	if (firmware_p == NULL || name == NULL)
		return -EINVAL;
	fw = lkpi_fw_lookup(name);
	if (IS_ERR(fw))
		return (int)PTR_ERR(fw);
	*firmware_p = fw;
	return 0;
}

/* [LNX] include/linux/firmware.h: the optional-firmware spelling.  The registry
 * is silent already, so it is the same lookup. */
int
firmware_request_nowarn(const struct firmware **fw, const char *name,
			struct device *device)
{
	return request_firmware(fw, name, device);
}

void
release_firmware(const struct firmware *firmware)
{

	if (firmware == NULL)
		return;
	/* the bytes belong to the table; only the description is ours */
	kfree((void *)firmware);
}

/*
 * The asynchronous spelling.  The interface's contract is that the caller is
 * not blocked and the continuation runs later, outside the caller's locks.  The
 * registry lookup is synchronous but the *completion* is not: the continuation
 * is queued on the component's system workqueue.  Running it inline is wrong
 * in a way the board showed: cfg80211's regulatory work (`reg_todo`) holds RTNL
 * while it asks for regulatory.db, and an inline callback re-takes RTNL on the
 * same thread -- a self-deadlock on a non-recursive mutex (runs/2026-11-10-
 * mac80211-c2, boot1f: `rtnl: lock begin` twice on one thread).
 *
 * A miss queues the continuation with NULL, which is the interface's own
 * asynchronous result for an absent blob.
 */
struct lkpi_fw_async {
	struct work_struct		work;
	void				(*cont)(const struct firmware *fw,
						void *context);
	void				*context;
	const struct firmware		*fw;
};

static void
lkpi_fw_async_run(struct work_struct *work)
{
	struct lkpi_fw_async *a = container_of(work, struct lkpi_fw_async, work);

	a->cont(a->fw, a->context);
	kfree(a);
}

int
request_firmware_nowait(struct module *module, bool uevent, const char *name,
			struct device *device, gfp_t gfp, void *context,
			void (*cont)(const struct firmware *fw, void *context))
{
	struct lkpi_fw_async *a;
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
	a = kmalloc(sizeof(*a), GFP_KERNEL);
	if (a == NULL) {
		if (fw != NULL)
			release_firmware(fw);
		return -ENOMEM;
	}
	a->cont = cont;
	a->context = context;
	a->fw = fw;
	INIT_WORK(&a->work, lkpi_fw_async_run);
	if (!schedule_work(&a->work)) {
		if (fw != NULL)
			release_firmware(fw);
		kfree(a);
		return -EBUSY;
	}
	return 0;
}

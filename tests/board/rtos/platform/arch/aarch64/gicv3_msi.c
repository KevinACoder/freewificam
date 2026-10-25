/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   gicv3_msi.c
 * @brief  Message-interrupt domain on the GICv3 ITS: a PCIe Requester ID
 *         becomes a set of LPIs.
 *
 * The mapping, in one line: the ITS DeviceID is the PCIe Requester ID
 * (bus << 8 | slot << 3 | func), each vector takes the next free LPI slot, the
 * message address is GITS_TRANSLATER and the message payload is the *event
 * id* - not the LPI number. The endpoint writes "event 2" and the ITS looks up
 * which LPI that is; a payload carrying the LPI number instead would translate
 * to nothing and be dropped without an error anywhere.
 *
 * The domain is reached through arch/aarch64/msi.h; the PCI MSI framework is
 * not vendored (see drivers/pcie/dwc_msix.c for what of it is kept). LPIs are
 * addressed by raw INTID (8192 + slot) and armed by the caller through CMSIS
 * IRQ_SetHandler/IRQ_Enable. A failed allocation unwinds the events it already
 * mapped, and the per-function record kept here is what Free() needs to find
 * the events again.
 *
 * Context: Allocate/Free run in task context - they post ITS commands and wait
 * for the command queue to drain. They are not concurrent-safe against another
 * ITS user (the `its`/`itsdump` shell commands also post commands); today's
 * boot order keeps the allocation at probe time, before the shell is
 * interactive, and nothing calls Free yet.
 *
 * @author zhugengyu
 * @date   16.09.2026
 */

#include <stdbool.h>
#include <stdint.h>

#include "gicv3_its.h"
#include "log.h"
#include "msi.h"

/* Vectors one function may hold. The ITS hands out 48 LPIs in total and a
 * device's ITT holds 64 events, so this is a policy cap, not a hardware one. */
#define ITS_MSI_VECTORS_MAX 8

/* Functions with live mappings, for Free(). Sized like the ITS device pool. */
#define ITS_MSI_FN_MAX ITS_MAX_DEVICES

struct its_msi_fn {
	uint32_t rid;
	uint32_t count;
};

static struct its_msi_fn its_msi_fns[ITS_MSI_FN_MAX];

static struct its_msi_fn *fn_record(uint32_t rid, bool create)
{
	uint32_t i;
	struct its_msi_fn *free_slot = NULL;

	for (i = 0; i < ITS_MSI_FN_MAX; i++) {
		if (its_msi_fns[i].rid == rid && its_msi_fns[i].count != 0) {
			return &its_msi_fns[i];
		}
		if (its_msi_fns[i].count == 0 && free_slot == NULL) {
			free_slot = &its_msi_fns[i];
		}
	}

	return create ? free_slot : NULL;
}

static void its_msi_unmap_all(uint32_t rid, uint32_t count)
{
	uint32_t i;

	for (i = 0; i < count; i++) {
		gic_its_event_unmap(rid, i);
	}
}

static ARM_DRIVER_VERSION its_msi_get_version(void)
{
	return (ARM_DRIVER_VERSION){ .api = MSI_API_VERSION, .drv = 0x0100 };
}

static int32_t its_msi_allocate(uint32_t rid, uint32_t count,
				MSI_VECTOR *vectors)
{
	struct its_msi_fn *fn;
	uint64_t doorbell;
	uint32_t i;

	if (vectors == NULL || count == 0 || count > ITS_MSI_VECTORS_MAX) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}

	if (fn_record(rid, false) != NULL) {
		/* One set of vectors per function in this design; a second
		 * request would silently overwrite the first one's LPIs. */
		return ARM_DRIVER_ERROR;
	}

	if (gic_its_device_attach(rid) != 0) {
		board_log("its: cannot map device %#x\n", rid);
		return ARM_DRIVER_ERROR;
	}

	doorbell = gic_its_trans_addr();

	for (i = 0; i < count; i++) {
		int intid = gic_its_event_map(rid, i);

		if (intid < 0) {
			its_msi_unmap_all(rid, i);
			return ARM_DRIVER_ERROR;
		}

		vectors[i].intid = (uint32_t)intid;
		vectors[i].address = doorbell;
		/* The event id, which is also the MSI-X table index the PCIe
		 * side programs. See drivers/pcie/dwc_msix.c for why they must be
		 * the same number. */
		vectors[i].data = i;
	}

	fn = fn_record(rid, true);
	if (fn == NULL) {
		its_msi_unmap_all(rid, count);
		return ARM_DRIVER_ERROR;
	}
	fn->rid = rid;
	fn->count = count;

	board_log("its: %02x:%02x.%x got %u message irq(s)"
		  " (event 0 -> intid %u)\n",
		  (rid >> 8) & 0xffu, (rid >> 3) & 0x1fu, rid & 7u, count,
		  vectors[0].intid);

	return (int32_t)count;
}

static void its_msi_free(uint32_t rid)
{
	struct its_msi_fn *fn = fn_record(rid, false);

	if (fn == NULL) {
		return;
	}

	its_msi_unmap_all(rid, fn->count);
	fn->rid = 0;
	fn->count = 0;
}

static const MSI_DOMAIN its_msi_domain = {
	.GetVersion = its_msi_get_version,
	.Allocate = its_msi_allocate,
	.Free = its_msi_free,
};

/* The active domain. Installed on first use so that a driver asking for
 * vectors is what brings the ITS up - there is no init order to get wrong. */
static const MSI_DOMAIN *active_domain;

int32_t msi_domain_register(const MSI_DOMAIN *domain)
{
	if (domain == NULL) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}

	active_domain = domain;

	return ARM_DRIVER_OK;
}

const MSI_DOMAIN *msi_domain_get(void)
{
	if (active_domain != NULL) {
		return active_domain;
	}

	/* its_init() is idempotent; a failure leaves the domain uninstalled,
	 * which callers report as "no message interrupts" and fall back from
	 * (polling) rather than treating as a fatal error. */
	if (its_init() != 0) {
		board_log("its: init failed, no message interrupts available\n");
		return NULL;
	}

	active_domain = &its_msi_domain;

	return active_domain;
}
/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file    mock_board.c
 * @brief   The scripted board drivers and ITS primitives: every symbol the
 *          adapter and the message-interrupt domain call, answered from a
 *          constant table and recorded.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "Driver_Common.h"
#include "gicv3.h"
#include "gicv3_its.h"

#include "mock_board.h"

static struct mock_board mock;

void
mock_board_reset(void)
{

	memset(&mock, 0, sizeof(mock));
	mock.n_devs = MOCK_BOARD_DEVS;
	mock.cfg_fail_dev = -1;
	mock.its_fail_at_event = -1;
	mock.msi_intid = 8192;
	mock.msi_addr = 0xfd450040ULL;
	mock.msi_data = 0;
	mock.its_doorbell = 0xfd450040ULL;

	/* two functions: one with both capabilities, one with MSI only */
	mock.dev[0].busn = 1;
	mock.dev[0].devfn = 0;
	mock.dev[0].vendor = 0x1234;
	mock.dev[0].device = 0x5678;
	mock.dev[0].baseclass = 0x02;
	mock.dev[0].subclass = 0x80;
	mock.dev[0].msi_cap = 0x50;
	mock.dev[0].msix_cap = 0x70;
	mock.dev[0].bar[0] = 0x100000u;

	mock.dev[1].busn = 2;
	mock.dev[1].devfn = 0;
	mock.dev[1].vendor = 0x1234;
	mock.dev[1].device = 0x5679;
	mock.dev[1].baseclass = 0x02;
	mock.dev[1].subclass = 0x00;
	mock.dev[1].msi_cap = 0x50;
	mock.dev[1].msix_cap = 0;
	mock.dev[1].bar[0] = 0x200000u;
}

struct mock_board *
mock_board(void)
{

	return &mock;
}

/* ------------------------------------------------------------------ */
/* the root-complex calls the adapter maps onto                        */
/* ------------------------------------------------------------------ */

void
dwc_pcie_init(void)
{

	mock.init_calls++;
}

uint32_t
dwc_pcie_ctrl_count(void)
{

	return 1;
}

uint32_t
dwc_pcie_dev_count(void)
{

	return (uint32_t)mock.n_devs;
}

const struct dwc_pcie_dev *
dwc_pcie_dev(uint32_t index)
{

	if ((int)index >= mock.n_devs)
		return NULL;
	return &mock.dev[index];
}

uint32_t
dwc_pcie_requester_id(const struct dwc_pcie_dev *dev)
{

	return (dev->busn << 8) | dev->devfn;
}

static int
mock_dev_index(uint32_t bus, uint32_t devfn)
{
	int i;

	for (i = 0; i < mock.n_devs; i++) {
		if (mock.dev[i].busn == bus && mock.dev[i].devfn == devfn)
			return i;
	}
	return -1;
}

int32_t
dwc_pcie_cfg_read(uint32_t bus, uint32_t devfn, uint32_t where, uint32_t size,
    uint32_t *value)
{
	int idx = mock_dev_index(bus, devfn);
	uint32_t v = 0;
	uint32_t i;

	mock.cfg_reads++;
	if (idx < 0)
		return ARM_DRIVER_ERROR;
	if (mock.cfg_fail_dev == idx) {
		if (!mock.cfg_fail_forever)
			mock.cfg_fail_dev = -1;	/* one-shot */
		return ARM_DRIVER_ERROR;
	}
	if (where + size > MOCK_BOARD_CFG_SIZE) {
		*value = (size == 1) ? 0xffu : (size == 2) ? 0xffffu : 0xffffffffu;
		return ARM_DRIVER_OK;
	}
	for (i = 0; i < size; i++)
		v |= (uint32_t)mock.cfg[idx][where + i] << (8 * i);
	*value = v;
	return ARM_DRIVER_OK;
}

int32_t
dwc_pcie_cfg_write(uint32_t bus, uint32_t devfn, uint32_t where, uint32_t size,
    uint32_t value)
{
	int idx = mock_dev_index(bus, devfn);
	uint32_t i;

	mock.cfg_writes++;
	if (idx < 0)
		return ARM_DRIVER_ERROR;
	if (where + size > MOCK_BOARD_CFG_SIZE)
		return ARM_DRIVER_ERROR;
	for (i = 0; i < size; i++)
		mock.cfg[idx][where + i] = (uint8_t)(value >> (8 * i));
	return ARM_DRIVER_OK;
}

/* ------------------------------------------------------------------ */
/* the MSI / MSI-X programming the adapter maps onto                   */
/* ------------------------------------------------------------------ */

int32_t
dwc_msi_alloc(const struct dwc_pcie_dev *dev, uint32_t nvec_max,
    MSI_VECTOR *vectors)
{

	(void)dev;
	if (mock.msi_alloc_fails)
		return ARM_DRIVER_ERROR;
	if (nvec_max == 0)
		return ARM_DRIVER_ERROR_PARAMETER;
	mock.msi_allocs++;
	vectors[0].intid = mock.msi_intid;
	vectors[0].address = mock.msi_addr;
	vectors[0].data = mock.msi_data;
	return 1;
}

int32_t
dwc_msi_release(const struct dwc_pcie_dev *dev)
{

	(void)dev;
	mock.msi_releases++;
	return ARM_DRIVER_OK;
}

int32_t
dwc_msix_alloc(const struct dwc_pcie_dev *dev, uint32_t nvec_max,
    MSI_VECTOR *vectors)
{

	(void)dev;
	(void)nvec_max;
	if (mock.msix_unsupported)
		return ARM_DRIVER_ERROR_UNSUPPORTED;
	mock.msix_allocs++;
	vectors[0].intid = mock.msi_intid;
	vectors[0].address = mock.msi_addr;
	vectors[0].data = mock.msi_data;
	return 1;
}

int32_t
dwc_msix_release(const struct dwc_pcie_dev *dev)
{

	(void)dev;
	mock.msix_releases++;
	return ARM_DRIVER_OK;
}

/* ------------------------------------------------------------------ */
/* the cache layer the adapter's DMA mapping stands on                 */
/* ------------------------------------------------------------------ */

void
cache_dcache_flush(uintptr_t addr, unsigned long size)
{

	(void)addr;
	mock.cache_flushes++;
	mock.last_cache_dir = 1;
	mock.last_cache_len = (unsigned int)size;
}

void
cache_dcache_invalidate(uintptr_t addr, unsigned long size)
{

	(void)addr;
	mock.cache_invalidates++;
	mock.last_cache_dir = 2;
	mock.last_cache_len = (unsigned int)size;
}

void
cache_dcache_flush_invalidate(uintptr_t addr, unsigned long size)
{

	(void)addr;
	(void)size;
	mock.cache_flushes++;
	mock.cache_invalidates++;
}

/* ------------------------------------------------------------------ */
/* the ITS primitives the message-interrupt domain stands on           */
/* ------------------------------------------------------------------ */

int
its_init(void)
{

	return 0;
}

uint64_t
gic_its_trans_addr(void)
{

	return mock.its_doorbell;
}

int
gic_its_device_attach(uint32_t devid)
{

	if (mock.its_attach_n < 8u)
		mock.its_attach_rid[mock.its_attach_n] = devid;
	mock.its_attach_n++;
	return 0;
}

int
gic_its_event_map(uint32_t devid, uint32_t eventid)
{

	if (mock.its_fail_at_event >= 0 &&
	    (int)mock.its_map_n == mock.its_fail_at_event)
		return -1;
	if (mock.its_map_n < 16u) {
		mock.its_map_rid[mock.its_map_n] = devid;
		mock.its_map_event[mock.its_map_n] = eventid;
	}
	mock.its_map_n++;
	return (int)(IRQ_LPI_INTID_FIRST + (mock.its_map_n - 1u));
}

void
gic_its_event_unmap(uint32_t devid, uint32_t eventid)
{

	(void)devid;
	(void)eventid;
	mock.its_unmap_n++;
}

void
gic_lpi_set_state(uint32_t intid, int enable)
{

	(void)intid;
	if (enable)
		mock.lpi_enables++;
	else
		mock.lpi_disables++;
}

/* ------------------------------------------------------------------ */
/* the arch interrupt registry, as a double                            */
/* ------------------------------------------------------------------ */

/* The registry the adapter arms its vectors through. The real one lives in
 * arch/aarch64/gicv3.c beside the controller's system-register accesses, so it
 * cannot be compiled for the host; this double keeps the same signature and the
 * same one-handler-per-INTID rule, and the test drives a delivery itself. */
#define MOCK_IRQ_SLOTS	16

static struct {
	uint32_t	intid;
	irq_handler_t	fn;
	void		*arg;
	bool		used;
} mock_irq[MOCK_IRQ_SLOTS];

int32_t
irq_register(uint32_t intid, irq_handler_t handler, void *arg)
{
	unsigned int i;
	int slot = -1;

	for (i = 0; i < MOCK_IRQ_SLOTS; i++) {
		if (mock_irq[i].used && mock_irq[i].intid == intid) {
			slot = (int)i;
			break;
		}
		if (!mock_irq[i].used && slot < 0)
			slot = (int)i;
	}
	if (slot < 0)
		return -1;
	mock_irq[slot].intid = intid;
	mock_irq[slot].fn = handler;
	mock_irq[slot].arg = arg;
	mock_irq[slot].used = true;
	return 0;
}

void
mock_board_dispatch(uint32_t intid)
{
	unsigned int i;

	for (i = 0; i < MOCK_IRQ_SLOTS; i++) {
		if (mock_irq[i].used && mock_irq[i].intid == intid) {
			if (mock_irq[i].fn != NULL)
				mock_irq[i].fn(mock_irq[i].arg);
			return;
		}
	}
}

/* ------------------------------------------------------------------ */
/* the log the board drivers call                                      */
/* ------------------------------------------------------------------ */

void
board_log(const char *fmt, ...)
{

	(void)fmt;
}

/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * RK3568 DesignWare PCIe root-complex core for this board. Derived from
 * NetBSD sys/arch/arm/rockchip/rk_pcie.c; the NetBSD Foundation notice below
 * is retained as the derivation attribution.
 */
/*	$NetBSD$	*/

/*-
 * Copyright (c) 2026 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by the RK3568 bring-up lab.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED.  IN NO
 * EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * RK3568 DesignWare PCIe root complex core, derived from NetBSD
 * sys/arch/arm/rockchip/rk_pcie.c. Register sequences kept from the
 * upstream driver:
 *
 *   - the link-up check reads the client APB LTSSM status and only
 *     accepts L0 with LINKUP (a bare L0 reading can be transient)
 *   - config TLPs for the secondary bus go through the outbound CFG0
 *     viewport (CFG1 deeper), reprogrammed per access with the
 *     bus/device/function in the target address
 *   - only device 0 exists on the root and secondary bus; a config
 *     access anywhere else is answered with all-ones without a TLP
 *     (single-device links ack every slot and ghost devices otherwise)
 *   - the outbound MEM window is identity-mapped, and one inbound
 *     identity window covers host RAM plus the MSI doorbell page,
 *     because the DWC programs only outbound regions by default and
 *     would silently drop the endpoint's posted doorbell writes
 *
 * @date 10.09.2026
 * @author zhugengyu
 */

#include <stdint.h>
#include <string.h>

#include "pcie_dw.h"

static inline uint32_t
dbi_read_4(uint64_t dbi, uint32_t reg)
{
	return *(volatile uint32_t *) (uintptr_t) (dbi + reg);
}

static inline void
dbi_write_4(uint64_t dbi, uint32_t reg, uint32_t val)
{
	*(volatile uint32_t *) (uintptr_t) (dbi + reg) = val;
}

static inline uint32_t
apb_read_4(uint64_t apb, uint32_t reg)
{
	return *(volatile uint32_t *) (uintptr_t) (apb + reg);
}

static inline void
apb_write_4(uint64_t apb, uint32_t reg, uint32_t val)
{
	*(volatile uint32_t *) (uintptr_t) (apb + reg) = val;
}

bool
pcie_dw_link_up(struct pcie_dw_softc *sc)
{
	uint32_t val = apb_read_4(sc->ctlr->apb_base,
	    PCIE_CLIENT_LTSSM_STATUS);

	return (val & LTSSM_LINKUP) == LTSSM_LINKUP &&
	    (val & 0x3f) == LTSSM_STATE_L0;
}

static void
pcie_dw_atu_prog(struct pcie_dw_softc *sc, uint32_t block, uint32_t type,
    uint64_t cpu_addr, uint64_t pci_addr, uint64_t size)
{
	u_int n;

	dbi_write_4(sc->ctlr->dbi_base, block + ATU_REGION_CTRL2, 0);
	dbi_write_4(sc->ctlr->dbi_base, block + ATU_REGION_CTRL1, type);
	dbi_write_4(sc->ctlr->dbi_base, block + ATU_REGION_LOWER_BASE,
	    (uint32_t) cpu_addr);
	dbi_write_4(sc->ctlr->dbi_base, block + ATU_REGION_UPPER_BASE,
	    (uint32_t) (cpu_addr >> 32));
	dbi_write_4(sc->ctlr->dbi_base, block + ATU_REGION_LIMIT,
	    (uint32_t) (cpu_addr + size - 1));
	dbi_write_4(sc->ctlr->dbi_base, block + ATU_REGION_LOWER_TARGET,
	    (uint32_t) pci_addr);
	dbi_write_4(sc->ctlr->dbi_base, block + ATU_REGION_UPPER_TARGET,
	    (uint32_t) (pci_addr >> 32));
	dbi_write_4(sc->ctlr->dbi_base, block + ATU_REGION_CTRL2,
	    ATU_ENABLE);

	__asm volatile("dsb st");
	for (n = 0; n < 5; n++) {
		if ((dbi_read_4(sc->ctlr->dbi_base,
		    block + ATU_REGION_CTRL2) & ATU_ENABLE) != 0)
			return;
		for (volatile int d = 0; d < 1000; d++)
			;
	}
}

/* outbound windows + the inbound identity window for host RAM */
static void
pcie_dw_atu_init(struct pcie_dw_softc *sc)
{
	const struct pcie_dw_controller *ctlr = sc->ctlr;

	pcie_dw_atu_prog(sc, ATU_REG_BASE(ctlr->mem_atu_idx), ATU_TYPE_MEM,
	    ctlr->mem_base, ctlr->mem_base, ctlr->mem_size);

	/* one inbound identity-mapped region covering both the host RAM
	 * the card DMAs against and the MSI doorbell page */
	pcie_dw_atu_prog(sc, ATU_REG_BASE_INB(0), ATU_TYPE_MEM,
	    ctlr->dma_base, ctlr->dma_base,
	    ctlr->doorbell_base + ctlr->doorbell_size - ctlr->dma_base);
}

static bool
pcie_dw_conf_ok(struct pcie_dw_softc *sc, u_int bus, u_int dev, u_int reg)
{

	if (reg >= 4096)
		return false;
	if (dev != 0 && (bus == 0 || bus == sc->secondary_bus))
		return false;
	return true;
}

uint32_t
pcie_dw_conf_read(struct pcie_dw_softc *sc, u_int bus, u_int dev, u_int fn,
    u_int reg)
{
	const struct pcie_dw_controller *ctlr = sc->ctlr;
	uint32_t type;
	uint64_t pci_addr;

	if (!pcie_dw_conf_ok(sc, bus, dev, reg))
		return 0xffffffff;

	if (bus == 0)
		return dbi_read_4(ctlr->dbi_base, reg);

	/* arm the config viewport once for the secondary bus; deeper
	 * buses would need the CFG1 type (nothing lives there on this
	 * single-function link) */
	if (!sc->cfg_window_armed) {
		type = bus == sc->secondary_bus ? ATU_TYPE_CFG0 : ATU_TYPE_CFG1;
		pci_addr = (uint64_t) bus << 24;
		pcie_dw_atu_prog(sc, ATU_REG_BASE(ctlr->cfg_atu_idx), type,
		    ctlr->cfg_base, pci_addr, ctlr->cfg_size);
		sc->cfg_window_armed = true;
	}

	return *(volatile uint32_t *) (uintptr_t) (ctlr->cfg_base +
	    ((uint64_t) dev << 15) + ((uint64_t) fn << 12) + (reg & ~3u));
}

void
pcie_dw_conf_write(struct pcie_dw_softc *sc, u_int bus, u_int dev, u_int fn,
    u_int reg, uint32_t val)
{
	const struct pcie_dw_controller *ctlr = sc->ctlr;
	uint32_t type;
	uint64_t pci_addr;

	if (!pcie_dw_conf_ok(sc, bus, dev, reg))
		return;

	if (bus == 0) {
		dbi_write_4(ctlr->dbi_base, reg, val);
		return;
	}

	if (!sc->cfg_window_armed) {
		type = bus == sc->secondary_bus ? ATU_TYPE_CFG0 : ATU_TYPE_CFG1;
		pci_addr = (uint64_t) bus << 24;
		pcie_dw_atu_prog(sc, ATU_REG_BASE(ctlr->cfg_atu_idx), type,
		    ctlr->cfg_base, pci_addr, ctlr->cfg_size);
		sc->cfg_window_armed = true;
	}

	*(volatile uint32_t *) (uintptr_t) (ctlr->cfg_base +
	    ((uint64_t) dev << 15) + ((uint64_t) fn << 12) + (reg & ~3u)) = val;
}

/* read the root port's secondary bus number from its DBI header */
static uint8_t
pcie_dw_secondary_bus(struct pcie_dw_softc *sc)
{

	return (uint8_t) ((dbi_read_4(sc->ctlr->dbi_base, 0x18) >> 8) & 0xff);
}

int
pcie_dw_claim_func(struct pcie_dw_softc *sc, struct pcie_dw_func *func)
{
	uint32_t id;
	uint32_t prev;

	id = pcie_dw_conf_read(sc, sc->secondary_bus, 0, 0, 0x00);
	if (id == 0xffffffff || id == 0)
		return -1;

	memset(func, 0, sizeof(*func));
	func->is_present = true;
	func->vendor_id = (uint16_t) id;
	func->device_id = (uint16_t) (id >> 16);
	func->revision_id = (uint8_t) pcie_dw_conf_read(sc, sc->secondary_bus,
	    0, 0, 0x08);
	func->bus = sc->secondary_bus;
	func->device = 0;
	func->function = 0;

	/* capability list: MSI and PCIe express */
	prev = pcie_dw_conf_read(sc, sc->secondary_bus, 0, 0, 0x34) & 0xff;
	for (u_int hops = 0; prev != 0 && hops < 48; hops++) {
		uint32_t cap = pcie_dw_conf_read(sc, sc->secondary_bus, 0, 0,
		    prev);

		if ((cap & 0xff) == 0x05)
			func->msi_cap = (uint16_t) prev;
		if ((cap & 0xff) == 0x11)
			func->msix_cap = (uint16_t) prev;
		if ((cap & 0xff) == 0x10)
			func->pcie_cap = (uint16_t) prev;
		prev = (cap >> 8) & 0xfc;
	}

	/* BAR 0 as assigned by the firmware enumeration */
	{
		uint32_t lo = pcie_dw_conf_read(sc, sc->secondary_bus, 0, 0,
		    0x10);
		uint32_t hi = pcie_dw_conf_read(sc, sc->secondary_bus, 0, 0,
		    0x14);
		int is64 = (int) ((lo & 0x6) == 0x4);

		if (lo == 0xffffffff || (lo & ~0xfu) == 0)
			return -1;

		func->bars[0].phys_addr = lo & ~0xfULL;
		if (is64)
			func->bars[0].phys_addr |= (uint64_t) hi << 32;
		func->bars[0].bus_addr = func->bars[0].phys_addr;
		func->bars[0].size = 0x10000;
		func->bars[0].flags = 0x2 | 0x40; /* 32-bit mem, non-prefetch */
		func->bar_count = 1;
	}

	/* enable memory space + bus master: without BME the endpoint's
	 * DMA engine silently ignores descriptors */
	{
		uint32_t cmd = pcie_dw_conf_read(sc, sc->secondary_bus, 0, 0,
		    0x04);

		cmd |= 0x6;	/* MEMORY | BUS_MASTER */
		pcie_dw_conf_write(sc, sc->secondary_bus, 0, 0, 0x04, cmd);
	}

	return 0;
}

void
pcie_dw_intx_arm(struct pcie_dw_softc *sc)
{
	const uint64_t apb = sc->ctlr->apb_base;
	const uint64_t rp = sc->ctlr->dbi_base;
	uint32_t rp_cmd = dbi_read_4(rp, 0x04);
	uint32_t rp_bctl = dbi_read_4(rp, 0x3c);

	/* unmask INTA~INTD in the client aggregation register so the
	 * endpoint's INTA assertion is forwarded to the GIC */
	apb_write_4(apb, PCIE_CLIENT_INTR_MASK_LEGACY, 0xffff0000u);

	/* make sure the root port does not suppress INTx (PCI command
	 * bit 10, and the same bit in its bridge control) */
	if ((rp_cmd & 0x400) != 0) {
		dbi_write_4(rp, 0x04, rp_cmd & ~0x400u);
	}
	if ((rp_bctl & 0x400) != 0) {
		dbi_write_4(rp, 0x3c, rp_bctl & ~0x400u);
	}
	__asm volatile("dsb sy");
}

u_int
pcie_dw_intx_status(struct pcie_dw_softc *sc)
{

	return apb_read_4(sc->ctlr->apb_base,
	    PCIE_CLIENT_INTR_STATUS_LEGACY) & 0xfu;
}

int
pcie_dw_attach(struct pcie_dw_softc *sc,
    const struct pcie_dw_controller *ctlr)
{

	sc->ctlr = ctlr;

	/* root port sanity: its config space is the DBI register file */
	{
		uint32_t id = dbi_read_4(ctlr->dbi_base, 0x00);

		if (id == 0xffffffff || id == 0)
			return -1;
	}

	sc->secondary_bus = pcie_dw_secondary_bus(sc);

	/* An already-trained link (a firmware bring-up) is kept: a
	 * reset would tear down a working link. */
	sc->link_up = pcie_dw_link_up(sc);

	pcie_dw_atu_init(sc);

	return sc->link_up ? 0 : -1;
}

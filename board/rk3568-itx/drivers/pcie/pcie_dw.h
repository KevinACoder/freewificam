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
 * sys/arch/arm/rockchip/rk_pcie.c ("rockchip,rk3568-pcie"). The
 * FDT/arm pcihost framework is replaced by a bare-metal claim flow:
 * keep a firmware-trained link untouched, program the outbound
 * memory window and the per-access config viewport, and expose
 * config-space and INTx helpers to the bus backend.
 *
 * @date 10.09.2026
 * @author zhugengyu
 */

#ifndef _DRIVERS_PCIE_PCIE_DW_H_
#define _DRIVERS_PCIE_PCIE_DW_H_

#include <stdbool.h>
#include <stdint.h>

typedef unsigned int u_int;

#ifndef __BIT
#define	__BIT(n)		(1UL << (n))
#endif
#ifndef __BITS
#define	__BITS(hi, lo)		(((1ULL << ((hi) + 1)) - 1) & ~((1ULL << (lo)) - 1))
#endif

/* Rockchip client APB registers (offsets from the APB base) */
#define	PCIE_CLIENT_GENERAL_CON		0x0000
#define	PCIE_CLIENT_INTR_STATUS_LEGACY	0x0008
#define	PCIE_CLIENT_INTR_MASK_LEGACY	0x001c
#define	PCIE_CLIENT_LTSSM_STATUS	0x0300

#define	LTSSM_LINKUP			(__BIT(16) | __BIT(17))
#define	LTSSM_STATE_L0			0x11

/* DWC DBI: unrolled outbound/inbound iATU viewports */
#define	ATU_REG_BASE(idx)		(((0x3) << 20) | ((idx) << 9))
#define	ATU_REG_BASE_INB(idx)		(((0x3) << 20) | ((idx) << 9) | __BIT(8))
#define	ATU_REGION_CTRL1		0x00
#define	ATU_REGION_CTRL2		0x04
#define	ATU_REGION_LOWER_BASE		0x08
#define	ATU_REGION_UPPER_BASE		0x0c
#define	ATU_REGION_LIMIT		0x10
#define	ATU_REGION_LOWER_TARGET		0x14
#define	ATU_REGION_UPPER_TARGET		0x18
#define	ATU_ENABLE			__BIT(31)
#define	ATU_TYPE_MEM			0x0
#define	ATU_TYPE_CFG0			0x4
#define	ATU_TYPE_CFG1			0x5

/* controller geometry (board constants, see the backend instance) */
struct pcie_dw_controller {
	uint64_t	apb_base;	/* client APB register file */
	uint64_t	dbi_base;	/* root port DBI (config + iATU) */
	uint64_t	cfg_base;	/* CPU aperture for config TLPs */
	uint64_t	cfg_size;	/* aperture size (one bus stride) */
	uint64_t	mem_base;	/* outbound MEM window */
	uint64_t	mem_size;
	uint64_t	dma_base;	/* inbound identity window (host RAM) */
	uint64_t	dma_size;
	uint64_t	doorbell_base;	/* MSI doorbell page (inbound) */
	uint64_t	doorbell_size;
	u_int		cfg_atu_idx;	/* outbound iATU region for config */
	u_int		mem_atu_idx;	/* outbound iATU region for MEM */
};

struct pcie_dw_softc {
	const struct pcie_dw_controller *ctlr;
	uint8_t		secondary_bus;	/* bus behind the root port */
	bool		link_up;	/* firmware-trained link kept */
	bool		cfg_window_armed;
};

/* one claimed endpoint function */
struct pcie_dw_bar {
	uint64_t	phys_addr;
	uint64_t	bus_addr;
	uint64_t	size;
	uint32_t	flags;
};

struct pcie_dw_func {
	uint16_t	vendor_id;
	uint16_t	device_id;
	uint8_t		revision_id;
	uint8_t		bus;
	uint8_t		device;
	uint8_t		function;
	bool		is_present;
	uint16_t	msi_cap;	/* capability offsets, 0 = none */
	uint16_t	msix_cap;
	uint16_t	pcie_cap;
	u_int		bar_count;
	struct pcie_dw_bar bars[6];
};

/*
 * Bring up the controller view: report (and keep) the firmware
 * trained link, program the outbound windows and arm the config
 * viewport for the secondary bus. Returns 0 when the link is up.
 */
int	pcie_dw_attach(struct pcie_dw_softc *sc,
	    const struct pcie_dw_controller *ctlr);

/* config space (dev 0 on the root/secondary bus only; deeper buses
 * answer any slot on this single-device link - guard against the
 * phantom devices that creates) */
uint32_t pcie_dw_conf_read(struct pcie_dw_softc *sc, u_int bus, u_int dev,
	    u_int fn, u_int reg);
void	pcie_dw_conf_write(struct pcie_dw_softc *sc, u_int bus, u_int dev,
	    u_int fn, u_int reg, uint32_t val);

/*
 * Read the function behind the secondary bus: ID, capability list,
 * BAR 0, and memory+bus-master enable. Returns 0 when a function
 * answered.
 */
int	pcie_dw_claim_func(struct pcie_dw_softc *sc,
	    struct pcie_dw_func *func);

/* INTx plumbing: client-side unmask and live status view */
void	pcie_dw_intx_arm(struct pcie_dw_softc *sc);
u_int	pcie_dw_intx_status(struct pcie_dw_softc *sc);

#endif	/* _DRIVERS_PCIE_PCIE_DW_H_ */

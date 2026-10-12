/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    mock_pcie_hcd.h
 * @brief   The synthetic PCI host and scripted device model the linuxkpi PCI
 *          suite drives the engine through.
 *
 * @design test double for the backend seam (components/linuxkpi/pci/
 *         lkpi_pcie_backend.h): a constant config-space table, a BAR memory
 *         region, a scripted doorbell and a recorded DMA direction log - no
 *         hardware. Shape reference: tests/host/linuxkpi-usb-test/mock_hcd.h:1-20
 *         (the synthetic host of the sibling suite).
 * @date    12.10.2026
 */

#ifndef MOCK_PCIE_HCD_H
#define MOCK_PCIE_HCD_H

#include "lkpi_pcie_backend.h"

/* the synthetic host carries three functions: two that a driver registered before
 * the bus came up claims, and one left unclaimed so a driver registered afterwards
 * can claim it (the "device already there" order) */
#define	MOCK_PCIE_DEVS		3
#define	MOCK_PCIE_BAR_SIZE	4096

/* the config-space layout the model answers with (the offsets the engine walks) */
#define	MOCK_CFG_VENDOR		0x00
#define	MOCK_CFG_DEVICE		0x02
#define	MOCK_CFG_COMMAND	0x04
#define	MOCK_CFG_STATUS		0x06
#define	MOCK_CFG_REVISION	0x08
#define	MOCK_CFG_CLASS		0x09
#define	MOCK_CFG_SUBSYS_VENDOR	0x2c
#define	MOCK_CFG_SUBSYS_ID	0x2e
#define	MOCK_CFG_CAP_PTR	0x34
#define	MOCK_CFG_MSI_CAP	0x50
#define	MOCK_CFG_MSIX_CAP	0x70

struct mock_pcie_dev {
	u8		cfg[256];
	u8		bar[MOCK_PCIE_BAR_SIZE];
	u16		vendor;
	u16		device;
	u8		bus;
	u8		devfn;
};

struct mock_pcie_backend {
	struct mock_pcie_dev	dev[MOCK_PCIE_DEVS];
	int			n_devs;
	/* fault injection (one-shot unless noted) */
	int			cfg_fail_at;		/* this offset's next read fails */
	bool			msix_available;		/* false = irq_setup(MSI-X) refuses */
	bool			lying;			/* fire_irq does not deliver */
	/* the live vector */
	int			irq_kind;
	int			irq_intid;
	bool			irq_active;
	void			(*irq_handler)(void *);
	void			*irq_arg;
	unsigned int		irq_setups;
	/* call recording */
	unsigned int		cfg_reads;
	unsigned int		cfg_writes;
	unsigned int		bar_maps;
	unsigned int		dma_allocs;
	unsigned int		dma_frees;
	int			dma_dir_log[16];
	unsigned int		dma_dir_n;
};

/* build a fresh model: the three functions above, no fault armed */
void	mock_pcie_reset(void);

/* the one live model */
struct mock_pcie_backend *mock_pcie_backend(void);

/* the seam operations the model implements */
const struct lkpi_pcie_be_ops *mock_pcie_ops(void);

/* the scripted doorbell: the device raises the vector irq_setup armed */
void	mock_pcie_fire_irq(void);

/* arm the one-shot config read fault at `off` (-1 clears it) */
void	mock_pcie_set_cfg_fail(int off);

/* make the doorbell a no-op (the "lying host" red control) */
void	mock_pcie_set_lying(bool lying);

/* take the MSI-X vector away (the "no vector" path) */
void	mock_pcie_set_msix_available(bool available);

#endif /* MOCK_PCIE_HCD_H */

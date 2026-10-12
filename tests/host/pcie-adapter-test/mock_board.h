/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file    mock_board.h
 * @brief   The scripted board drivers and ITS primitives the adapter and the
 *          message-interrupt domain are driven against.
 *
 * @design test double for the two boundaries below the adapter (dwc_pcie's
 *         root-complex calls and the ITS primitives the domain stands on) plus a
 *         recording cache layer, so a case can assert the call sequence the
 *         adapter and the domain produce without a board. Shape reference:
 *         tests/host/linuxkpi-usb-test/mock_hcd.h:1-20.
 * @date    12.10.2026
 */

#ifndef MOCK_BOARD_H
#define MOCK_BOARD_H

#include <stdint.h>

#include "dwc_pcie.h"
#include "msi.h"

#define MOCK_BOARD_DEVS		2
#define MOCK_BOARD_CFG_SIZE	256

struct mock_board {
	int			n_devs;
	struct dwc_pcie_dev	dev[MOCK_BOARD_DEVS];
	uint8_t			cfg[MOCK_BOARD_DEVS][MOCK_BOARD_CFG_SIZE];

	/* fault injection */
	int			cfg_fail_dev;	/* this device's next cfg read fails */
	bool			cfg_fail_forever;	/* ... and every one after it */
	bool			msix_unsupported;	/* dwc_msix_alloc refuses */
	bool			msi_alloc_fails;	/* dwc_msi_alloc refuses */

	/* what the scripted MSI allocator hands back */
	uint32_t		msi_intid;
	uint64_t		msi_addr;
	uint32_t		msi_data;

	/* call recording */
	unsigned int		init_calls;
	unsigned int		cfg_reads;
	unsigned int		cfg_writes;
	unsigned int		msi_allocs;
	unsigned int		msi_releases;
	unsigned int		msix_allocs;
	unsigned int		msix_releases;
	unsigned int		lpi_enables;
	unsigned int		lpi_disables;
	unsigned int		cache_flushes;
	unsigned int		cache_invalidates;
	int			last_cache_dir;
	unsigned int		last_cache_len;

	/* the scripted ITS the domain stands on */
	uint64_t		its_doorbell;
	uint32_t		its_attach_rid[8];
	unsigned int		its_attach_n;
	uint32_t		its_map_rid[16];
	uint32_t		its_map_event[16];
	unsigned int		its_map_n;
	unsigned int		its_unmap_n;
	int			its_fail_at_event;	/* -1 = none */
};

void	mock_board_reset(void);
struct mock_board *mock_board(void);

/* Deliver the vector `intid` through the registry double, the way the GIC's
 * dispatch would after the adapter armed it. */
void	mock_board_dispatch(uint32_t intid);

#endif /* MOCK_BOARD_H */

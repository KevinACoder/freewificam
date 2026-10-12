/**
 * SPDX-License-Identifier: BSD-3-Clause
 * @file    pcie_nvme.c
 * @brief   Case pcie-nvme: the PCIe root complex comes up, the NVMe controller
 *          is probed, its namespace geometry is read, and one real I/O
 *          completion is forced through the message-interrupt path.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  case row of the suite's table (tests/board/common/board_suite.h); the
 *          body drives the line through the board drivers only
 *          (board/rk3568-itx/drivers/pcie/dwc_pcie.h, .../nvme/dwc_nvme.h and
 *          .../nvme/nvme.h), so what it exercises on the board is the same
 *          interface the rest of the project will use.
 *
 * The chain, in order: bring the root complex up and keep the firmware-trained
 * link (dwc_pcie_init), report the controllers and the claimed downstream
 * functions, probe the NVMe controller (dwc_nvme_init), read the namespace
 * geometry through the frozen ARM_DRIVER_NVME face, then issue one block read
 * and read back the controller's message-interrupt delivery count.
 *
 * The delivery count is the point of the last step: the device posts a
 * completion into the completion queue whether or not its message interrupt was
 * delivered, so a polled completion cannot show that the ITS path works. A
 * nonzero count after the read is the evidence that the interrupt really
 * arrived, and the judge refuses a PASS without it.
 *
 * Anchors (the judge checks each on PASS):
 *   pcie: ctrl <n> link up, downstream bus <n>
 *   pcie: dev <bdf> <ids> class <cc>
 *   nvme<n>: <ids> at 0x<bar>, ns<n>: <blocks> blocks of <bytes> bytes
 *   nvme<n>: message interrupts delivered (intid <n>, <n> so far)
 *   pcie-nvme: ctrls=<n> devs=<n>
 *   pcie-nvme: ns blocks=<n> sector=<n>
 *   pcie-nvme: irqs=<n>
 */

#include <stdint.h>
#include <string.h>

#include "board_suite.h"
#include "pcie_cases.h"

#include "dwc_pcie.h"
#include "dwc_nvme.h"
#include "nvme.h"

#include "log.h"

/* One read of eight 512-byte blocks: the classic sector size, so the buffer is
 * a whole number of blocks on any namespace this board carries. */
#define PCIE_NVME_BLOCKS	8U
#define PCIE_NVME_BYTES		(PCIE_NVME_BLOCKS * 512U)

/* page-aligned, so the transfer can describe the caller's buffer with PRP1
 * plus PRP2 and not fall back to the driver's bounce buffer */
static uint8_t pcie_nvme_buf[PCIE_NVME_BYTES] __attribute__((aligned(4096)));

void pcie_nvme_chain( void )
{
	NVME_CAPABILITIES caps;
	uint32_t ctrls;
	uint32_t devs;
	uint32_t irqs;
	int32_t rc;

	/* --- the root complex ------------------------------------------------ */

	/* The shared bring-up (idempotent, once per boot): the storage cases run
	 * after this one and must not re-probe what this call already did. */
	(void)pcie_nvme_bringup();

	ctrls = dwc_pcie_ctrl_count();
	devs = dwc_pcie_dev_count();
	LOG_ANCHOR( "pcie-nvme: ctrls=%u devs=%u\n", ctrls, devs );
	if( ctrls < 1U ) {
		board_case_fail( "no PCIe controller came up - see the pcie lines above" );
		return;
	}
	if( devs < 1U ) {
		board_case_fail( "no downstream function was claimed - see the pcie lines above" );
		return;
	}

	/* --- the NVMe controller --------------------------------------------- */

	if( dwc_nvme_ctrl_count() < 1U ) {
		LOG_ERROR( "pcie-nvme: nvme=%u\n", dwc_nvme_ctrl_count() );
		board_case_fail( "no NVMe controller found" );
		return;
	}

	/* --- the namespace geometry ------------------------------------------ */

	caps = Driver_NVME.GetCapabilities( 0U );
	LOG_ANCHOR( "pcie-nvme: ns blocks=%llu sector=%u\n",
		    (unsigned long long)caps.sector_count, caps.sector_size );
	if( caps.sector_count == 0U || caps.sector_size == 0U ) {
		board_case_fail( "the namespace geometry is empty" );
		return;
	}

	/* --- one real I/O completion through the interrupt path -------------- */

	(void)memset( pcie_nvme_buf, 0, sizeof( pcie_nvme_buf ) );
	rc = Driver_NVME.ReadBlocks( 0U, 0U, pcie_nvme_buf, PCIE_NVME_BLOCKS );
	irqs = dwc_nvme_irq_count( 0U );

	LOG_ANCHOR( "pcie-nvme: irqs=%u\n", irqs );

	if( rc != ARM_DRIVER_OK ) {
		board_case_fail( "the block read did not complete" );
		return;
	}
	if( irqs < 1U ) {
		board_case_fail( "the read completed without a message interrupt" );
		return;
	}
}

BOARD_TEST( "pcie-nvme", pcie_nvme_chain );

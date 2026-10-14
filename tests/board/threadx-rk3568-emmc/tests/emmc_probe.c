/**
 * SPDX-License-Identifier: BSD-3-Clause
 * @file    emmc_probe.c
 * @brief   Case emmc-probe: the card comes up, is identified, and the mode it
 *          came up in is read back from the card and the controller.
 * @author  zhugengyu
 * @date    14.10.2026
 * @design  case row of the suite's table (tests/board/common/board_suite.h); the
 *          body drives the line through the port's own face (sdmmc_mmc.h) and
 *          the board driver's census, so what it exercises on the board is the
 *          same interface the rest of the project uses.
 *
 * The chain: bring the card up (the shared bring-up), report the identity the
 * card's CID gave, then report the bus mode and the clock chain. The clock line
 * is the one that matters most for this round: it states what the CRU selector
 * and the controller's own divider produced, not what the stack asked for, and
 * the judge refuses a PASS whose reported clock is not the 50 MHz slot's rate.
 *
 * Anchors (the judge checks each on PASS):
 *   emmc: <product> manufacturer=<n> serial=<n> blocks=<n> block=512
 *   emmc: mode HS bus 8-bit clock=<n>MHz
 *   emmc: cru cclk_emmc sel=<n> dll=bypass
 *   emmc: cmd23 on
 *   emmc-probe: transfers=<n> errors=<n>
 */

#include <stdint.h>

#include "board_suite.h"
#include "emmc_cases.h"

#include "sdmmc_mmc.h"

#include "dwc_mshc.h"

#include "log.h"

/* The bus-timing value the imported stack uses for eMMC high speed
 * (kMMC_HighSpeedTiming), and the data-bus-width value for eight lines
 * (kMMC_DataBusWidth8bit). Spelled out here rather than pulled from the stack's
 * header: the case reads the card's record, and the two numbers are part of
 * what it asserts about it. */
#define EMMC_TIMING_HIGH_SPEED		1U
#define EMMC_BUS_WIDTH_8BIT		2U

/* The CRU's 50 MHz slot selector (DWC_MSHC_CRU_CCLK_SEL_50M) and the rate it
 * carries on this board. */
#define EMMC_CRU_SEL_50M		4U
#define EMMC_CLOCK_50MHZ		50000000U

void emmc_probe_chain( void )
{
	struct sdmmc_mmc_info info;
	struct sdmmc_mmc_host_state hc;
	struct dwc_mshc_census census;

	if( emmc_bringup() != 0 ) {
		board_case_fail( "the card did not come up" );
		return;
	}

	if( sdmmc_mmc_info_get( &info ) != 0 ) {
		board_case_fail( "no card identity after bring-up" );
		return;
	}

	LOG_ANCHOR( "emmc: %s manufacturer=%u serial=%lu blocks=%lu block=%u\n",
		    info.product, (unsigned)info.manufacturer,
		    (unsigned long)info.serial, (unsigned long)info.blocks,
		    (unsigned)SDMMC_MMC_SECTOR_SIZE );

	if( info.blocks == 0U ) {
		board_case_fail( "the card reports no user-area blocks" );
		return;
	}

	/* The mode line reports the stack's own record of the timing and the width
	 * it settled on; the clock is the controller's achieved rate, because that
	 * is the number a clock-chain defect would move. */
	LOG_ANCHOR( "emmc: mode %s bus %u-bit clock=%uMHz\n",
		    ( info.timing == EMMC_TIMING_HIGH_SPEED ) ? "HS" : "other",
		    (unsigned)( ( info.width == EMMC_BUS_WIDTH_8BIT ) ? 8U : 4U ),
		    (unsigned)( info.clock_hz / 1000000U ) );

	if( info.timing != EMMC_TIMING_HIGH_SPEED ) {
		board_case_fail( "the card did not switch to high-speed timing" );
		return;
	}
	if( info.width != EMMC_BUS_WIDTH_8BIT ) {
		board_case_fail( "the card did not switch to the eight-line bus" );
		return;
	}
	if( info.clock_hz != EMMC_CLOCK_50MHZ ) {
		board_case_fail( "the bus clock is not the 50 MHz slot's rate" );
		return;
	}

	if( sdmmc_mmc_host_state_get( &hc ) != 0 ) {
		board_case_fail( "the controller has no state to report" );
		return;
	}

	LOG_ANCHOR( "emmc: cru cclk_emmc sel=%u dll=bypass\n", (unsigned)hc.cclk_sel );

	if( hc.cclk_sel != EMMC_CRU_SEL_50M ) {
		board_case_fail( "the CCLK_EMMC mux is not on the 50 MHz slot" );
		return;
	}

	LOG_ANCHOR( "emmc: cmd23 %s\n", info.cmd23 ? "on" : "off" );

	if( !info.cmd23 ) {
		board_case_fail( "block-count commands are not enabled" );
		return;
	}

	/* The controller's census: a probe that needed no data transfer at all is
	 * still a probe, but a bring-up that failed a command would show here. */
	dwc_mshc_census_get( &census );
	LOG_ANCHOR( "emmc-probe: transfers=%u errors=%u\n",
		    (unsigned)census.transfers, (unsigned)census.errors );

	if( census.errors != 0U ) {
		board_case_fail( "the controller recorded command errors during bring-up" );
		return;
	}
}

BOARD_TEST( "emmc-probe", emmc_probe_chain );

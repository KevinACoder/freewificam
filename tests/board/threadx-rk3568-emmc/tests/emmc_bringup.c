/**
 * SPDX-License-Identifier: BSD-3-Clause
 * @file    emmc_bringup.c
 * @brief   The eMMC line's shared bring-up: the card service, the block device,
 *          and the helpers the cases' evidence lines are built from.
 * @author  zhugengyu
 * @date    14.10.2026
 * @design  shared bring-up row of the suite's table - the PCIe/NVMe suite's
 *          pcie_bringup.c, with the storage line replaced by the eMMC one. It
 *          registers no case of its own; the cases call it and stay idempotent
 *          by construction, so a round that selects one case still brings the
 *          line up exactly once.
 *
 * What it does, in order: the controller and the card (the port's card service,
 * which runs the imported stack's initialization ladder), then the block-device
 * registration (the seam's own binding, which is also what `stor` uses). It
 * deliberately does not mount anything: mounting and formatting are the file
 * cases' decisions, and a bring-up that formatted a card would destroy evidence
 * before anyone asked for it.
 */

#include <stdint.h>

#include "board_suite.h"
#include "emmc_cases.h"

#include "blkdev.h"
#include "sdmmc_mmc.h"

#include "generic_timer.h"
#include "log.h"

static int emmc_up;

int emmc_bringup( void )
{
	struct sdmmc_mmc_info info;

	if( emmc_up ) {
		return 0;
	}

	if( sdmmc_mmc_card_start() != 0 ) {
		LOG_ERROR( "emmc-bringup: the card did not come up - see the stack lines above\n" );
		return -1;
	}

	if( sdmmc_mmc_info_get( &info ) != 0 ) {
		LOG_ERROR( "emmc-bringup: no card identity after bring-up\n" );
		return -1;
	}

	/* The registry's binding is what makes the card a block device; it is also
	 * what `stor` and the file cases reach the card through, so it is done here
	 * once rather than by each of them. */
	if( blkdev_bind() != BLKDEV_OK ) {
		LOG_ERROR( "emmc-bringup: the block device did not register\n" );
		return -1;
	}

	emmc_up = 1;

	return 0;
}

uint32_t emmc_now_ms( void )
{
	uint32_t freq = gtimer_freq_get();

	if( freq == 0U ) {
		return 0U;
	}

	return (uint32_t)( ( gtimer_count_get() * 1000u ) / freq );
}

uint32_t emmc_kib( uint32_t bytes, uint32_t ms )
{
	return (uint32_t)( ( (uint64_t)bytes / 1024u ) * 1000u / ms );
}

uint8_t emmc_pattern( uint32_t index )
{
	uint32_t seed = index * 2654435761u + 12345u;

	return (uint8_t)( seed >> 16 );
}

uint32_t emmc_fnv1a( const uint8_t *data, uint32_t len, uint32_t hash )
{
	uint32_t i;

	for( i = 0U; i < len; i++ ) {
		hash ^= data[i];
		hash *= 16777619u;
	}

	return hash;
}

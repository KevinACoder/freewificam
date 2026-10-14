/**
 * SPDX-License-Identifier: BSD-3-Clause
 * @file    emmc_fs_verify.c
 * @brief   Case emmc-fs-verify: mount the volume WITHOUT formatting it, read the
 *          file the burn-in case wrote, and checksum it.
 * @author  zhugengyu
 * @date    14.10.2026
 * @design  case row of the suite's table (tests/board/common/board_suite.h),
 *          split from the burn-in case so the table lists it in execution order
 *          and a round can select it alone (which is how the cold-boot
 *          persistence round runs it: same image, second boot, no format).
 *
 * This case exists so a second (and third) cold boot can prove the volume the
 * first boot wrote survived the power loss: the file is the same, the checksum
 * must be the same, and nothing here formats anything.
 *
 * Anchors (the judge checks each on PASS):
 *   emmc-fs-verify: mounted type=exFAT sector=4096
 *   emmc-fs-verify: read bytes=<n> ms=<n> kib=<n>
 *   emmc-fs-verify: verify ok checksum=<8 hex digits>
 */

#include <stdint.h>

#include "board_suite.h"
#include "emmc_cases.h"
#include "emmc_fs.h"

#include "fs.h"

#include "log.h"

void emmc_fs_verify_chain( void )
{
	uint32_t ms;
	uint32_t hash;
	int mismatch;
	int ret;

	if( emmc_bringup() != 0 ) {
		board_case_fail( "the card did not come up" );
		return;
	}

	if( fs_init() != 0 ) {
		board_case_fail( "the filesystem layer did not bind the block device" );
		return;
	}

	/* No format here, by design: this case's whole point is what the previous
	 * boot left on the card. */
	if( fs_mount( EMMC_FS_VOLUME ) != 0 || !fs_is_mounted( EMMC_FS_VOLUME ) ) {
		board_case_fail( "the volume did not mount - was it ever formatted?" );
		return;
	}
	LOG_ANCHOR( "emmc-fs-verify: mounted type=%s sector=%u\n",
		    fs_type_name( EMMC_FS_VOLUME ), (unsigned)fs_sector_size() );

	ret = emmc_fs_read_verify( &hash, &ms, &mismatch );
	if( ret == -1 ) {
		board_case_fail( "the file read failed" );
		return;
	}
	if( mismatch >= 0 ) {
		LOG_ERROR( "emmc-fs-verify: mismatch at file offset %d\n", mismatch );
		board_case_fail( "the file did not read back byte-exact" );
		return;
	}
	if( ret == -2 ) {
		board_case_fail( "the file read was short" );
		return;
	}

	LOG_ANCHOR( "emmc-fs-verify: read bytes=%lu ms=%lu kib=%lu\n",
		    (unsigned long)EMMC_FS_BYTES, (unsigned long)ms,
		    (unsigned long)emmc_kib( EMMC_FS_BYTES, ms ) );
	LOG_ANCHOR( "emmc-fs-verify: verify ok checksum=%08lx\n", (unsigned long)hash );
}

BOARD_TEST( "emmc-fs-verify", emmc_fs_verify_chain );

/**
 * @file    wlan8189_probe.c
 * @brief   Case wlan8189-probe: the SDIO bus is brought up, the vendored driver
 *          registers itself, and its probe runs far enough to leave a registered
 *          wireless device behind.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  case row of the suite's table (tests/board/common/board_suite.h). The
 *          chain is the three layers in their real order: the SDIO seam over the
 *          board's controller (components/linuxkpi/sdio/lkpi_sdio_backend.h),
 *          the engine that owns the card and probes drivers
 *          (components/linuxkpi/sdio/linuxkpi_sdio.c), and the module-init
 *          channel that runs the driver's entry point
 *          (components/linuxkpi/linuxkpi_core.h, module_init).
 *
 * The order carries one decision worth naming: the driver's entry point is run
 * BEFORE the engine starts, because the entry point only registers the driver
 * (sdio_register_driver) - the engine's start is what brings the card up and
 * probes whatever is registered, which is the same order a Linux boot takes.
 *
 * What the case can honestly claim: the driver's probe path runs its own power
 * sequence, downloads its firmware and initialises its MAC/BB/RF before it
 * registers anything. A registered device with a real hardware address is
 * therefore evidence that all of that ran - the probe returns failure before
 * registration if any step fails. The case says exactly that rather than
 * inferring more.
 *
 * Anchors (the judge checks each on PASS):
 *   wlan8189: sdio up card=<vid>:<pid> funcs=<n>
 *   wlan8189: driver attached mac=<addr>
 *   wlan8189: netdev up rc=0
 *   wlan8189: probe ok
 */

#include <string.h>

#include "board_suite.h"
#include "wlan8189_cases.h"

#include "lkpi_sdio_backend.h"
#include "linuxkpi_sdio.h"
#include "linuxkpi_cfg80211.h"

/* the board side of the SDIO seam: the install entry point the case calls. The
 * port header names the imported stack's types, so the stack's own SDIO header
 * is read first. */
#include "fsl_sdio.h"
#include "sdmmc_port.h"

#include "os/cmsis2/cmsis2.h"
#include "log.h"

void wlan8189_probe_chain( void )
{
	struct lkpi_sdio_be_card_info info;
	struct wiphy *wiphy;
	uint8_t mac[6];
	unsigned int i;
	int st;

	if( lkpi_sdio_be_ready() ) {
		/* the enum case ran first on a full sweep; nothing to install twice */
		(void)lkpi_sdio_be_card_init( &info );
	} else {
		lkpi_sdio_fsl_install();
		(void)memset( &info, 0, sizeof( info ) );
		st = lkpi_sdio_be_card_init( &info );
		if( st != 0 ) {
			board_case_fail( "the card did not come up on the SDIO bus - see the lines above" );
			return;
		}
	}

	LOG_ANCHOR( "wlan8189: sdio up card=%04x:%04x funcs=%u\n",
		    (unsigned)info.vendor, (unsigned)info.device,
		    (unsigned)info.func_count );

	/* the driver's own entry point, through the module-init channel */
	lkpi_module_init_run();

	/* bring the card up and probe the driver the entry point registered */
	lkpi_sdio_start();

	wiphy = lkpicfg_wiphy();
	if( wiphy == NULL ) {
		board_case_fail( "the driver registered no wireless device (see the driver's own lines above)" );
		return;
	}

	if( lkpicfg_get_hwaddr( wiphy, mac ) != 0 ) {
		board_case_fail( "the registered device reports no hardware address" );
		return;
	}

	LOG_ANCHOR( "wlan8189: driver attached mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] );

	/* a hardware address of all zeros is the shape of "the driver never read
	 * the chip's eFuse", so it is refused here rather than counted as attach */
	for( i = 0U; i < sizeof( mac ); i++ ) {
		if( mac[i] != 0U ) {
			break;
		}
	}
	if( i == sizeof( mac ) ) {
		board_case_fail( "the hardware address is all zero (the driver did not read the chip)" );
		return;
	}

	st = lkpicfg_netdev_up( wiphy );
	LOG_ANCHOR( "wlan8189: netdev up rc=%d\n", st );
	if( st != 0 ) {
		board_case_fail( "the driver's network interface could not be brought up" );
		return;
	}

	LOG_ANCHOR( "wlan8189: probe ok\n" );
}

BOARD_TEST( "wlan8189-probe", wlan8189_probe_chain );

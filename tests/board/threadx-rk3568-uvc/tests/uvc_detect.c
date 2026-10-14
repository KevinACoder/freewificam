/**
 * @file    uvc_detect.c
 * @brief   Case usb-host-up: bring the bus-0 host up, wait for the camera.
 * @author  zhugengyu
 * @date    13.10.2026
 * @design  case row of the camera table (tests/board/common/board_suite.h);
 *          the bring-up body follows the regression image's storage case
 *          (tests/board/common/cases/usb_msc.c) - board layer, stack
 *          initialiser, then wait for the class instance - because the
 *          enumeration chain (root port, external hub, camera) either ends in
 *          that instance or the run has to say where it stopped.
 *
 * Bus 0 (0xFD800000) is the camera group; the panel ports sit on bus 1 and are
 * not this image's business. The kernel-side wait is bounded so a camera that
 * never appears fails the case instead of hanging the suite.
 */

#include "board_suite.h"

#include "os/cmsis2/cmsis2.h"
#include "log.h"

#include "usb_glue.h"

#include "usbh_core.h"
#include "usbh_video.h"

#define UVC_DETECT_WAIT_SPINS 600U /* 100 ms per spin -> 60 s */

struct usbh_video *uvc_app_class(void);

static void usb_host_up( void )
{
	struct usbh_video *video_class;

	if( usb_board_init( 0 ) != 0 ) {
		board_case_fail( "usb board layer bring-up failed at bus0" );
		return;
	}
	LOG_ANCHOR( "usbh: core init bus=0 hcd=ehci base=0x%lx\n",
		    (unsigned long)usb_board_reg_base( 0 ) );

	if( usbh_initialize( 0, usb_board_reg_base( 0 ), usb_board_event_handler ) != 0 ) {
		board_case_fail( "usbh_initialize failed" );
		return;
	}
	usb_board_after_start( 0 );

	for( unsigned spin = 0; spin < UVC_DETECT_WAIT_SPINS; spin++ ) {
		video_class = uvc_app_class();
		if( video_class != 0 ) {
			break;
		}
		osDelay( 100U );
	}

	if( video_class == 0 ) {
		board_case_fail( "no video class instance within 60 s" );
		return;
	}

	/* The acceptance slice names this camera: a different video device on the
	 * bus would otherwise pass the row and fail every later step. */
	if( video_class->hport->device_desc.idVendor != 0x046d ||
	    video_class->hport->device_desc.idProduct != 0x0990 ) {
		board_case_fail( "unexpected video device on bus0" );
		return;
	}
	if( video_class->hport->bus->busid != 0 ) {
		board_case_fail( "video class on a bus other than bus0" );
	}
}

BOARD_TEST( "usb-host-up", usb_host_up );

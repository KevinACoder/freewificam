/**
 * @file    uvc_open.c
 * @brief   Case uvc-open: open the camera at the target configuration.
 * @author  zhugengyu
 * @date    13.10.2026
 * @design  case row of the camera table (tests/board/common/board_suite.h); the
 *          open body is the application half's policy call (tests/uvc_app.c),
 *          which runs the class driver's probe/commit sequence and selects the
 *          alternate setting through SET_INTERFACE (class/video/usbh_video.c).
 *
 * This case is the acceptance slice's A10: the device was recognised on bus0
 * (previous case) and the streaming interface must now be opened with a real
 * negotiation - the anchor line carries the negotiated format/frame indices,
 * the alternate setting, the endpoint's payload bound and what the device
 * reported for this configuration.
 */

#include "board_suite.h"

#include "usbh_core.h"
#include "usbh_video.h"

#define UVC_OPEN_MPS_MIN   1U
#define UVC_OPEN_MPS_MAX   4096U
#define UVC_OPEN_IVAL_MAX  4U

struct usbh_video *uvc_app_class(void);
int uvc_app_ensure_open(void);

static void uvc_open( void )
{
	struct usbh_video *video_class;

	if( uvc_app_ensure_open() != 0 ) {
		board_case_fail( "usbh_video_open failed" );
		return;
	}

	video_class = uvc_app_class();
	if( video_class == 0 || !video_class->is_opened ) {
		board_case_fail( "camera not open after the open call" );
		return;
	}
	if( video_class->isoin == 0 ) {
		board_case_fail( "no isochronous endpoint after open" );
		return;
	}
	if( video_class->isoin_mps < UVC_OPEN_MPS_MIN ||
	    video_class->isoin_mps > UVC_OPEN_MPS_MAX ) {
		board_case_fail( "endpoint payload bound out of range" );
		return;
	}
	/* The isochronous engine schedules per USB frame on the micro-frame mask
	 * table; an interval outside it would be rejected at the first submit. */
	if( video_class->isoin->bInterval < 1 ||
	    video_class->isoin->bInterval > UVC_OPEN_IVAL_MAX ) {
		board_case_fail( "endpoint interval outside the engine's table" );
	}
}

BOARD_TEST( "uvc-open", uvc_open );

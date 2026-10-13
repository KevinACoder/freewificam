/**
 * @file    uvc_iso_smoke.c
 * @brief   Case uvc-iso-smoke: isochronous IN transfers on the camera endpoint.
 * @author  zhugengyu
 * @date    13.10.2026
 * @design  case row of the camera table (tests/board/common/board_suite.h); the
 *          submission shape follows the host-side scheduling suite
 *          (tests/host/ehci-iso-schedule-test/iso_case.c) - a urb with a
 *          trailing iso-packet array, one packet per micro-frame - driven here
 *          through the public entry (usbh_submit_urb) so the whole transport
 *          (scheduler, frame list, completion scan) runs exactly as it does
 *          for a class driver.
 *
 * This case is the acceptance slice's A10b: after the open negotiation, raw
 * isochronous IN URBs are submitted on the video-streaming endpoint with a
 * hardcoded operating point, counting completions, byte lengths and error
 * codes - deliberately independent of any payload assembly so the transport
 * either works on the board or the failure is local to it. The waiting is the
 * engine's own synchronous mode; the case does not peek inside the controller.
 */

#include <stdint.h>

#include "board_suite.h"

#include "usbh_core.h"
#include "usbh_video.h"

#include "log.h"

#define UVC_SMOKE_PKTS        8U   /* micro-frames per urb: one USB frame */
#define UVC_SMOKE_URBS        16U
#define UVC_SMOKE_TIMEOUT_MS  500U
#define UVC_SMOKE_MAX_PAYLOAD 1024U /* alt ladder bound the smoke geometry fits */

struct usbh_video *uvc_app_class(void);
int uvc_app_ensure_open(void);

/* One urb whose trailing packet array is the storage the flexible array
 * member (urb.iso_packet) refers to; the static assertion pins the two views
 * to the same address, so the case can use either name. */
struct uvc_smoke_urb {
	struct usbh_urb urb;
	struct usbh_iso_frame_packet packets[UVC_SMOKE_PKTS];
};

_Static_assert(offsetof( struct uvc_smoke_urb, packets ) == offsetof( struct usbh_urb, iso_packet ),
		"the packet storage must sit where the urb's flexible array starts" );

static struct uvc_smoke_urb smoke;
static uint8_t smoke_buf[UVC_SMOKE_PKTS * UVC_SMOKE_MAX_PAYLOAD] __attribute__((aligned( 64 )));

static void uvc_iso_smoke( void )
{
	struct usbh_video *video_class;
	uint32_t stride;
	unsigned done = 0, err_urbs = 0, err_pkts = 0;
	uint32_t bytes = 0, max_len = 0;

	if( uvc_app_ensure_open() != 0 ) {
		board_case_fail( "camera not open for the iso smoke" );
		return;
	}
	video_class = uvc_app_class();

	/* The smoke's geometry is the descriptor ladder's single-transaction
	 * region; a larger payload bound would need the multi-transaction layout
	 * the stream engine owns (next round). */
	if( video_class->isoin_mps < 1 || video_class->isoin_mps > UVC_SMOKE_MAX_PAYLOAD ) {
		board_case_fail( "packet geometry outside the smoke's bound" );
		return;
	}
	stride = ( (uint32_t)video_class->isoin_mps + 63U ) & ~63U;
	if( stride * UVC_SMOKE_PKTS > sizeof( smoke_buf ) ) {
		board_case_fail( "smoke buffer smaller than the packet geometry" );
		return;
	}

	smoke.urb.hport = video_class->hport;
	smoke.urb.ep = video_class->isoin;
	smoke.urb.num_of_iso_packets = UVC_SMOKE_PKTS;
	smoke.urb.timeout = UVC_SMOKE_TIMEOUT_MS;
	for( unsigned i = 0; i < UVC_SMOKE_PKTS; i++ ) {
		smoke.urb.iso_packet[i].transfer_buffer = smoke_buf + ( i * stride );
		smoke.urb.iso_packet[i].transfer_buffer_length = video_class->isoin_mps;
	}

	for( unsigned round = 0; round < UVC_SMOKE_URBS; round++ ) {
		int ret;

		for( unsigned i = 0; i < UVC_SMOKE_PKTS; i++ ) {
			smoke.urb.iso_packet[i].actual_length = 0;
			smoke.urb.iso_packet[i].errorcode = 0;
		}
		smoke.urb.actual_length = 0;
		smoke.urb.errorcode = 0;
		smoke.urb.timeout = UVC_SMOKE_TIMEOUT_MS;

		ret = usbh_submit_urb( &smoke.urb );
		if( ret == 0 ) {
			done++;
		} else {
			err_urbs++;
		}
		if( ret != -USB_ERR_TIMEOUT ) {
			/* a completion ran: actual_length is this round's harvest */
			bytes += smoke.urb.actual_length;
		}

		for( unsigned i = 0; i < UVC_SMOKE_PKTS; i++ ) {
			if( smoke.urb.iso_packet[i].errorcode != 0 ) {
				err_pkts++;
			}
			if( smoke.urb.iso_packet[i].actual_length > max_len ) {
				max_len = smoke.urb.iso_packet[i].actual_length;
			}
		}
	}

	LOG_ANCHOR( "uvc: iso smoke done=%u/%u bytes=%u errpkt=%u errurb=%u max=%u\n",
		    done, UVC_SMOKE_URBS, bytes, err_pkts, err_urbs, max_len );

	if( done == 0 ) {
		board_case_fail( "no isochronous urb completed" );
		return;
	}
	if( max_len > video_class->isoin_mps ) {
		board_case_fail( "packet longer than the alternate's payload bound" );
	}
}

BOARD_TEST( "uvc-iso-smoke", uvc_iso_smoke );

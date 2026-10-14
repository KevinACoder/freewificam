/**
 * @file    uvc_stream.c
 * @brief   Case uvc-stream: the stream engine on the camera - frames, rate, bandwidth.
 * @author  zhugengyu
 * @date    14.10.2026
 * @design  case row of the camera table (tests/board/common/board_suite.h); the
 *          consumer shape follows the upstream host-UVC demos (dequeue ->
 *          use -> enqueue, tests/bouffalolab/src/main.c:40-52) and the engine
 *          is the acceptance slice's FR-3 (class/video/usbh_uvc_stream.c).
 *
 * The case hands the camera back to a closed state first, so the engine runs
 * its own open sequence: probe exchange, the bandwidth question, the
 * smallest-sufficient alternate setting.  Then it consumes frames for a
 * bounded window and anchors what the run produced - frame count, payload
 * bytes, per-frame sizes and the measured rate.  The comparison against the
 * Linux ground truth of the same board and camera is the ledger's (A11); the
 * judge holds the acceptance ratio's floor.
 */

#include "board_suite.h"

#include "os/cmsis2/cmsis2.h"
#include "log.h"

#include "usbh_core.h"
#include "usbh_video.h"
#include "usbh_uvc_stream.h"

#define UVC_STREAM_WINDOW_MS   20000U /* the bounded measurement window */
#define UVC_STREAM_DEQUEUE_MS  500U
#define UVC_STREAM_POOL_FRAMES 3U
#define UVC_STREAM_FRAME_BYTES (48U * 1024U) /* MJPEG 640x480 fits well inside */

struct usbh_video *uvc_app_class(void);
int uvc_app_close(void);

static uint8_t stream_bufs[UVC_STREAM_POOL_FRAMES][UVC_STREAM_FRAME_BYTES] __attribute__((aligned(64)));
static struct usbh_videoframe stream_pool[UVC_STREAM_POOL_FRAMES];

static void uvc_stream( void )
{
	struct usbh_video *video_class;
	struct uvc_stream_stats stats;
	uint32_t start_ms, elapsed_ms;
	uint32_t frames = 0, timeouts = 0, bytes = 0, max_size = 0;

	if( uvc_app_class() == 0 ) {
		board_case_fail( "camera not detected" );
		return;
	}

	(void)uvc_app_close(); /* the engine opens the camera itself, from alt 0 */

	for( unsigned i = 0; i < UVC_STREAM_POOL_FRAMES; i++ ) {
		stream_pool[i].frame_buf = stream_bufs[i];
		stream_pool[i].frame_bufsize = UVC_STREAM_FRAME_BYTES;
		stream_pool[i].frame_size = 0;
	}
	usbh_video_stream_create( stream_pool, UVC_STREAM_POOL_FRAMES );

	{
		int ret = usbh_video_stream_start( 640, 480, USBH_VIDEO_FORMAT_MJPEG );

		if( ret != 0 ) {
			static char note[64];

			snprintf( note, sizeof( note ), "usbh_video_stream_start failed ret=%d", ret );
			board_case_fail( note );
			return;
		}
	}
	video_class = uvc_app_class();
	LOG_ANCHOR( "uvc: stream open mjpeg 640x480 mps=%u dmp=%u pool=%u\n",
		    (unsigned)video_class->isoin_mps,
		    (unsigned)video_class->probe.dwMaxPayloadTransferSize,
		    UVC_STREAM_POOL_FRAMES );

	start_ms = osKernelGetTickCount();
	while( ( osKernelGetTickCount() - start_ms ) < UVC_STREAM_WINDOW_MS ) {
		struct usbh_videoframe *frame = 0;

		if( usbh_video_stream_dequeue( &frame, UVC_STREAM_DEQUEUE_MS ) == 0 ) {
			if( frames == 0 ) {
				LOG_ANCHOR( "uvc: stream first frame size=%u\n",
					    (unsigned)frame->frame_size );
			}
			frames++;
			bytes += frame->frame_size;
			if( frame->frame_size > max_size ) {
				max_size = frame->frame_size;
			}
			usbh_video_stream_enqueue( frame );
		} else {
			timeouts++;
		}
	}
	elapsed_ms = osKernelGetTickCount() - start_ms;

	usbh_video_stream_stop();
	usbh_video_stream_stats( &stats );

	LOG_ANCHOR( "uvc: stream done window_ms=%u frames=%u timeouts=%u bytes=%u max=%u fps_x100=%u kbit_s=%u dropped=%u errpkt=%u\n",
		    (unsigned)elapsed_ms, frames, timeouts, bytes, max_size,
		    frames * 100000U / elapsed_ms, bytes * 8U / elapsed_ms,
		    (unsigned)stats.dropped, (unsigned)stats.err_packets );

	if( frames == 0 ) {
		board_case_fail( "no video frame arrived in the window" );
		return;
	}
	if( max_size > UVC_STREAM_FRAME_BYTES ) {
		board_case_fail( "a frame exceeded the pool buffer" );
	}
}

BOARD_TEST( "uvc-stream", uvc_stream );

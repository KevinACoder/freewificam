# @file    stream.mk
# @brief   The F429 -> Pi 4B stream image: both camera classes, both first-party
#          application halves and the pi_stream case.
#
# Included by usb.mk when CONFIG_CASES carries pi_stream.  The network half
# (app/stream/pi_net.c) is compiled in the WICED flag world by wiced.mk; this
# file only carries the USB/camera world.  The two halves meet at pi_net.h,
# which has no WICED or lwIP type in it.

STREAM_ALL_SRCS := \
	$(USB)/class/video/usbh_video.c \
	$(USB)/class/video/usbh_uvc_stream.c \
	$(LINE)/app/usb/uvc_app.c \
	$(LINE)/app/usb/usb_app.c \
	$(LINE)/app/cases/uvc_iso_smoke.c \
	$(LINE)/app/cases/pi_stream.c

# The microphone class is only linked into the audio+video stage.  An image
# that carries it without using it still enumerates the camera's AC/AS
# interfaces; keeping the class out of the video-only stages is both smaller
# and a cleaner separation.
ifeq ($(CONFIG_STREAM_STAGE),4)
STREAM_ALL_SRCS += \
	$(USB)/class/audio/usbh_uac.c \
	$(USB)/class/audio/usbh_uac_stream.c \
	$(LINE)/app/usb/uac_app.c
endif

# Symbols the stream image must carry.  The camera and microphone class
# tables are the same rows the uvc_*/uac_* images gate on; the stream entry
# points are what the case actually calls.
STREAM_GATE_SYMBOLS := \
	video_ctrl_class_info video_streaming_class_info \
	audio_ctrl_class_info audio_streaming_class_info \
	usbh_video_stream_start usbh_uac_stream_start

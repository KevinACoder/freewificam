# @file    uvc.mk
# @brief   The camera image's source set: the video class, its running-stream
#          half, the first-party application half and the four camera cases.
#
# Included by usb.mk after the storage source sets are decided.  The DWC2
# port's isochronous engine is selected by platform/build_defs.h from the same
# case names (CONFIG_USB_DWC2_ISO), so every non-camera image leaves the
# engine's code out.

UVC_ALL_SRCS := \
	$(USB)/class/video/usbh_video.c \
	$(USB)/class/video/usbh_uvc_stream.c \
	$(LINE)/app/usb/uvc_app.c \
	$(foreach c,$(UVC_CASES),$(LINE)/app/cases/$(c).c)

# Symbols the camera image must carry: the stream engine's entry point and the
# two class-info rows the enumeration table claims the camera with.  The DWC2
# ISO engine is static inside usb_hc_dwc2.c, so its presence is proven by the
# image's UVC cases actually completing an iso URB, not by nm.
UVC_GATE_SYMBOLS := video_ctrl_class_info video_streaming_class_info
UVC_GATE_STREAM_SYMBOLS := usbh_video_stream_start
# The stream engine's entry point is only linked by the images that run the
# stream case; --gc-sections drops it from the detect-only census image.
UVC_GATE_REQUIRED := $(UVC_GATE_SYMBOLS)
ifneq ($(filter uvc_stream,$(UVC_CASES)),)
UVC_GATE_REQUIRED += $(UVC_GATE_STREAM_SYMBOLS)
endif

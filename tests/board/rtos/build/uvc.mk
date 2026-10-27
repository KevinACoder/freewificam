# @file    uvc.mk
# @brief   The camera image's source set: the imported USB host stack (through
#          usb.mk), the video class and its running-stream half, the
#          isochronous engine the camera's endpoint needs, the application
#          half and the camera cases.
#
# Included after usb.mk, which supplies USB_STACK_SRCS / USB_BOARD_SRCS /
# USB_CFLAGS (core, hub, both controller ports, the board glue and the OSAL).
# This file adds only what the camera needs on top - and it defines the EHCI
# port's isochronous switch for this image alone: every other image leaves
# CONFIG_USB_EHCI_ISO undefined, so the engine's code and its per-bus state
# stay out of them.
#
# The class set of a camera image is hub + video; no storage class is linked,
# because a second class on the same controller would make a bring-up failure
# ambiguous (the storage image has the mirror-image rule).

CONFIG_UVC_USB_BUS ?= 0

UVC_CFLAGS := $(USB_CFLAGS) \
	-I$(USB)/class/video \
	-DCONFIG_USB_EHCI_ISO=1 \
	-DUVC_USB_BUS=$(CONFIG_UVC_USB_BUS)

UVC_CASES := $(filter uvc_detect uvc_open uvc_iso_smoke uvc_stream,$(CASES))

UVC_ALL_SRCS := \
	$(USB)/port/ehci/usb_hc_ehci_iso.c \
	$(USB)/class/video/usbh_video.c \
	$(USB)/class/video/usbh_uvc_stream.c \
	$(USB_PORT)/class/uvc_app.c \
	$(foreach c,$(UVC_CASES),$(LINE)/app/cases/$(c).c)

# Symbols the camera image must carry: the isochronous engine (or the port's
# iso calls would not link), the stream engine's entry point, and the two
# class-info rows the enumeration table claims the camera with.
UVC_GATE_SYMBOLS := ehci_iso_urb_init usbh_video_stream_start \
	video_ctrl_class_info video_streaming_class_info

# The iso engine's per-bus state is sized by this key (port header default 4);
# the camera profile can widen it when a second camera is ever attached.
CONFIG_USB_EHCI_ISO_NUM ?= 4
UVC_CFLAGS += -DCONFIG_USB_EHCI_ISO_NUM=$(CONFIG_USB_EHCI_ISO_NUM)

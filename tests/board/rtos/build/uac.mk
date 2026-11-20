# @file    uac.mk
# @brief   The microphone image's source set: the first-party UAC1 host class,
#          its PCM capture half, the application half and the three audio
#          cases.
#
# Included by usb.mk after the video source set is decided.  The DWC2
# isochronous engine is selected by platform/build_defs.h from the same case
# names (CONFIG_USB_DWC2_ISO), so every non-camera image leaves the engine's
# code out.

UAC_ALL_SRCS := \
	$(USB)/class/audio/usbh_uac.c \
	$(USB)/class/audio/usbh_uac_stream.c \
	$(LINE)/app/usb/uac_app.c \
	$(foreach c,$(UAC_CASES),$(LINE)/app/cases/$(c).c)

# Symbols the microphone image must carry: the stream engine's entry point and
# the two class-info rows the enumeration table claims the audio interfaces
# with.  The audio capture case is what keeps usbh_uac_stream_start alive;
# --gc-sections drops it from the detect-only census image, so the gate asks
# for it only when a capture case is in the image.
UAC_GATE_SYMBOLS := audio_ctrl_class_info audio_streaming_class_info
UAC_GATE_STREAM_SYMBOLS := usbh_uac_stream_start
UAC_GATE_REQUIRED := $(UAC_GATE_SYMBOLS)
ifneq ($(filter uac_mic uac_av,$(UAC_CASES)),)
UAC_GATE_REQUIRED += $(UAC_GATE_STREAM_SYMBOLS)
endif
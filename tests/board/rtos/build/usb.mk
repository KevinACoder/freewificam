# @file    usb.mk
# @brief   The USB host domain: the imported CherryUSB stack, the board's USB
#          power/PHY/reset layer, the CMSIS-RTOS2 OSAL, the first-party glue and
#          (for the storage image) the mass-storage class and its test case.
#
# This unit is included when the case list carries a USB consumer
# (`usb_xhci`, `usb_ehci`, or `rtw89`, whose linuxkpi USB engine rides the same
# controllers).  It deliberately does not include the linuxkpi USB engine: that
# belongs to the wireless world (`rtw89.mk`), so the storage image never links
# the URB engine.
#
# Board facts live in `ports/cherryusb/board/rk3568-itx/usb_config.h`, selected
# by putting that directory first on the include path (the imported headers
# include "usb_config.h" themselves).  The interrupt priority is the one
# per-image decision: the completion path gives a semaphore from the handler,
# and a kernel whose port forbids API calls above a threshold needs the line
# moved below it - `BOARD_IRQ_PRIORITY_API_CALL_RAW` is 0xd0 on FreeRTOS and
# 0xb0 on ThreadX, so one define serves both.

USB := $(ROOT)/third-party/cherryusb
USB_PORT := $(ROOT)/ports/cherryusb
USB_BOARD := $(PLATFORM)/board/rk3568-itx/usb

# The widest single transfer the stage pool must cover.  The storage line's
# largest command is 1 MiB and the native MSC path moves the caller's buffer
# directly, so this value sizes the linuxkpi adapter's pool (used by the
# wireless image); the gate below keeps it in step with the driver's RX size.
USB_STAGE_SIZE := 20480

USB_INCLUDES := \
	-I$(USB_PORT)/board/rk3568-itx \
	-I$(USB_PORT)/class \
	-I$(USB_BOARD) \
	-I$(USB)/common \
	-I$(USB)/core \
	-I$(USB)/class/hub \
	-I$(USB)/class/msc \
	-I$(USB)/port/ehci \
	-I$(USB)/port/xhci

USB_CFLAGS := $(filter-out -Wextra,$(CFLAGS)) -std=gnu11 \
	-DUSB_BOARD_IRQ_PRIORITY_RAW=BOARD_IRQ_PRIORITY_API_CALL_RAW \
	$(USB_INCLUDES)

# Diagnostic arm (CONFIG_USB_MSC_DIAG): a short timeout makes a transfer that
# only completes through the port's timeout drain fail fast instead of hiding in
# a five-second wait, and the xHCI port's own timeout dump is compiled in.  The
# arm is not a delivery profile; it exists to name the defect.
ifneq ($(CONFIG_USB_MSC_DIAG),)
USB_CFLAGS += -DMSC_DIAG=1 -DCONFIG_USBHOST_XHCI_DBG_DUMP=1 \
	-DCONFIG_USBHOST_MSC_TIMEOUT=800
endif
ifneq ($(CONFIG_USB_MSC_DIAG_FULL),)
USB_CFLAGS += -DMSC_DIAG_FULL=1
endif

USB_STACK_SRCS := \
	$(USB)/core/usbh_core.c \
	$(USB)/class/hub/usbh_hub.c \
	$(USB)/port/ehci/usb_hc_ehci.c \
	$(USB)/port/xhci/usb_hc_xhci.c

USB_BOARD_SRCS := \
	$(USB_BOARD)/usb_domain.c \
	$(USB_PORT)/board/rk3568-itx/usb_glue.c \
	$(USB_PORT)/osal/cmsis2/osal_cmsis2.c

USB_MSC_CASES := $(filter usb_xhci usb_ehci,$(CASES))

ifeq ($(strip $(USB_MSC_CASES)),)
USB_CLASS_SRCS :=
USB_CASE_SRCS :=
else
USB_CLASS_SRCS := \
	$(USB)/class/msc/usbh_msc.c \
	$(USB_PORT)/class/msc_app.c
USB_CASE_SRCS := \
	$(LINE)/app/cases/usb_msc.c \
	$(foreach c,$(USB_MSC_CASES),$(LINE)/app/cases/$(c).c)
endif

USB_ALL_SRCS := $(USB_STACK_SRCS) $(USB_BOARD_SRCS) $(USB_CLASS_SRCS) $(USB_CASE_SRCS)

# Build-side gates for this domain (the `gates` recipe calls them):
#  - the xHCI/EHCI class table must be in the image, or enumeration succeeds
#    and no class ever claims the device;
#  - a storage image must carry the MSC class driver and the first-party app
#    half, and the class-info table must be non-empty;
#  - the stage size is checked against the wireless driver's receive size in
#    rtw89.mk (that number belongs to that driver, not to this file).
USB_GATE_CLASS := $(if $(strip $(USB_MSC_CASES)),msc_class_info,)

# @file    usb.mk
# @brief   The USB host domain: the imported CherryUSB stack, the board's USB
#          power/PHY/reset layer, the CMSIS-RTOS2 OSAL, the first-party glue and
#          (for the storage image) the mass-storage class and its test case.
#
# This unit is included when the case list carries a USB consumer
# (`usb_xhci`, `usb_ehci`, `usb_dwc2`, or a wireless case whose linuxkpi USB
# engine rides the same controllers).  It deliberately does not include the
# linuxkpi USB engine: that belongs to the wireless world (`rtw89.mk`), so the
# storage image never links the URB engine.
#
# Board facts live in the board's `usb/usb_config.h`, selected by putting that
# directory first on the include path (the imported headers include
# "usb_config.h" themselves).  The interrupt priority is the one per-image
# decision: the completion path gives a semaphore from the handler, and a
# kernel whose port forbids API calls above a threshold needs the line moved
# below it - `BOARD_IRQ_PRIORITY_API_CALL_RAW` is 0xd0 on RK3568 FreeRTOS,
# 0xb0 on ThreadX, and 0x50 on the F429, so one define serves all.

USB := $(ROOT)/third-party/cherryusb
USB_PORT := $(USB)/ports

# The board dimension: which controller facts, which glue, which controller
# ports compile. The Kconfig board choice is the single switch (the generated
# make config carries it).
#
# firefly v2: two xHCI instances, no EHCI (the DTS "firefly,usb2" instances
# are a vendor dual-role IP this project has no source driver for), and no
# usb_domain.c - the board facts are header-only in the same directory.
# F429: one OTG_HS core in full-speed host mode driven by the imported DWC2
# port; single-controller build, so CONFIG_USBHOST_MULT_HC stays undefined and
# the DWC2 source supplies the stack's global entry points directly.
# The Kconfig fragment carries bools as 1 (not y), the same form
# platform/board/sources.mk's obj-$(CONFIG_...) consumes.
ifeq ($(BOARD),stm32f429-challenger)
USB_BOARD := $(PLATFORM)/board/stm32f429-challenger/usb
USB_STACK_SRCS := \
	$(USB)/core/usbh_core.c \
	$(USB)/class/hub/usbh_hub.c \
	$(USB)/port/dwc2/usb_hc_dwc2.c
else ifeq ($(CONFIG_BOARD_FIREFLY_V2),1)
USB_BOARD := $(PLATFORM)/board/firefly/v2/usb
USB_STACK_SRCS := \
	$(USB)/core/usbh_core.c \
	$(USB)/class/hub/usbh_hub.c \
	$(USB)/port/xhci/usb_hc_xhci.c
else
USB_BOARD := $(PLATFORM)/board/rk3568-itx/usb
USB_STACK_SRCS := \
	$(USB)/core/usbh_core.c \
	$(USB)/class/hub/usbh_hub.c \
	$(USB)/port/ehci/usb_hc_ehci.c \
	$(USB)/port/xhci/usb_hc_xhci.c
endif

USB_INCLUDES := \
	-I$(USB_BOARD) \
	-I$(LINE)/app/usb \
	-I$(USB)/common \
	-I$(USB)/core \
	-I$(USB)/class/hub \
	-I$(USB)/class/msc \
	-I$(USB)/class/vendor/wifi \
	-I$(USB)/port/dwc2 \
	-I$(USB)/port/ehci \
	-I$(USB)/port/xhci

USB_CFLAGS := $(filter-out -Wextra,$(CFLAGS)) -std=gnu11 \
	$(USB_INCLUDES)

# Diagnostic arm (CONFIG_USB_MSC_DIAG): a short timeout makes a transfer that
# only completes through the port's timeout drain fail fast instead of hiding in
# a five-second wait, and the xHCI port's own timeout dump is compiled in.  The
# arm is not a delivery profile; it exists to name the defect.
# MSC_DIAG / MSC_DIAG_FULL are defined by platform/build_defs.h from the same
# Kconfig symbols; no per-profile compiler switch is needed here.

USB_BOARD_SRCS := \
	$(USB_PORT)/osal/cmsis2/osal_cmsis2.c \
	$(USB_BOARD)/usb_glue.c

ifneq ($(CONFIG_BOARD_FIREFLY_V2),1)
# Only the RK3568 board has the shared EHCI/xHCI power/PHY/reset domain; the
# F429 USB block is one OTG_HS core configured entirely by its glue.
ifeq ($(BOARD),rk3568-itx)
USB_BOARD_SRCS += $(USB_BOARD)/usb_domain.c
endif
endif

USB_MSC_CASES := $(filter usb_xhci usb_ehci usb_dwc2 f429_sdram,$(CASES))

# The RK storage cases share app/cases/usb_msc.c (2 MiB of static buffers and
# two controller wrappers); the F429 carries its own smaller case.  Keep the
# source sets separate so the F429 image never links the RK body.
USB_MSC_SHARED_CASES := $(filter usb_xhci usb_ehci,$(CASES))
USB_MSC_F429_CASES := $(filter usb_dwc2 f429_sdram,$(CASES))

# The AIC8800 image needs the MSC class for the ZeroCD modeswitch (the switch
# rides the MSC connect path) but carries neither the first-party MSC app half
# nor the storage cases, so the class set and the case set split here.
USB_AIC8800_CASES := $(filter aic8800,$(CASES))

ifeq ($(strip $(USB_MSC_CASES) $(USB_AIC8800_CASES)),)
USB_CLASS_SRCS :=
USB_CASE_SRCS :=
else
USB_CLASS_SRCS := \
	$(USB)/class/msc/usbh_msc.c \
	$(if $(strip $(USB_MSC_CASES)),$(LINE)/app/usb/msc_app.c)
ifneq ($(strip $(USB_MSC_CASES)),)
USB_CASE_SRCS := \
	$(if $(strip $(USB_MSC_SHARED_CASES)),$(LINE)/app/cases/usb_msc.c) \
	$(foreach c,$(USB_MSC_SHARED_CASES),$(LINE)/app/cases/$(c).c) \
	$(foreach c,$(USB_MSC_F429_CASES),$(LINE)/app/cases/$(c).c)
else
USB_CASE_SRCS :=
endif
ifneq ($(strip $(USB_AIC8800_CASES)),)
USB_CLASS_SRCS += $(USB)/class/vendor/wifi/usbh_aic8800.c
endif
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

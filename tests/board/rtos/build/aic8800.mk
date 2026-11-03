# @file    aic8800.mk
# @brief   The AIC8800 wireless world: the CherryUSB PR's vendor Wi-Fi class
#          rides `usb.mk` (the class source itself is added there when the
#          aic8800 case is selected); this fragment adds the demo STA stack
#          (firmware download, FullMAC, WPA2-PSK, lwIP glue), the compiled-in
#          firmware blobs and the bring-up case.
#
# Composition (deliberately NOT the linuxkpi/net80211 bridge shape of
# rtw89.mk / rtl8xxxu.mk): the AIC8800 is a FullMAC whose vendor stack brings
# its own MLME and its own lwIP netif, so no substitute stack, no seam and no
# URB engine are linked.  The stack runs on the CherryUSB OSAL directly
# (wireless-drivers/aic8800/patches/0001-osal-port.patch is the port).
#
# The firmware blob unit is generated, never committed (the bytes are
# Aicsemi's GPL-3.0 firmware): tools/rtos/aic8800-fw-blobs.py writes
# wireless-drivers/aic8800/fw/aic8800_fw_blobs.c and the gate below turns a
# missing unit into the regeneration command, the same contract as the
# linuxkpi blobs in docs/FIRMWARE-NOTICES.md.

AIC8800_DRV := $(ROOT)/wireless-drivers/aic8800
AIC8800_FW_BLOBS_C := $(AIC8800_DRV)/fw/aic8800_fw_blobs.c

ifeq ($(wildcard $(AIC8800_FW_BLOBS_C)),)
$(error aic8800: $(AIC8800_FW_BLOBS_C) is missing - generate it with: python3 tools/rtos/aic8800-fw-blobs.py --fw-dir <dir with the five AIC8800D80 U02 .bin files>)
endif

AIC8800_CFLAGS := $(filter-out -Wextra,$(CFLAGS)) -std=gnu11 \
	-I$(AIC8800_DRV) \
	-I$(USB)/common \
	-I$(USB)/core \
	-I$(USB)/class/hub \
	-I$(USB)/class/vendor/wifi \
	-I$(USB)/class/msc \
	-I$(USB_BOARD) \
	-I$(USB)/port/xhci \
	-I$(LINE)/app/usb

AIC8800_SRCS := \
	$(AIC8800_DRV)/aic8800_boot.c \
	$(AIC8800_DRV)/aic8800_fmac.c \
	$(AIC8800_DRV)/aic8800_fw.c \
	$(AIC8800_DRV)/aic8800_wifi.c \
	$(AIC8800_DRV)/aic8800_wpa2.c \
	$(AIC8800_DRV)/aic8800_lwip.c \
	$(AIC8800_FW_BLOBS_C) \
	$(LINE)/app/cases/aic8800.c

AIC8800_ALL_SRCS := $(AIC8800_SRCS)

# The symbols only the case reaches: with --gc-sections the class-info entry
# (nothing references the table by address; usbh_initialize walks the linker
# section) and the blob register function have no other reference.
AIC8800_LDFLAGS := \
	-Wl,-u,aic8800_class_info \
	-Wl,-u,aic8800_fw_blobs_register \
	-Wl,-u,usb_board_init

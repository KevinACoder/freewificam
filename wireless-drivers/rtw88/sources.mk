# rtw88: the vendored driver, one bus port per image.
#
# The shared core is the seventeen chip/stack units; the bus port is either the
# SDIO pair (sdio.c + rtw8821cs.c, the RTL8821CS arm) or the USB pair (usb.c +
# rtw8821cu.c, the RTL8821CU arm).  wow.c (CONFIG_PM), led.c
# (CONFIG_RTW88_LEDS) and debug.c (CONFIG_RTW88_DEBUGFS) are not among them,
# for the reason rtw88_sdio.mk states.  The other chips' bus and chip units are
# not either - the linker's zero-undefined gate is the check.

ifeq ($(SRC_TAG),drv)
obj-$(CONFIG_WLAN_LINE_RTW88) += \
	main.c \
	mac80211.c \
	util.c \
	tx.c \
	rx.c \
	mac.c \
	phy.c \
	coex.c \
	efuse.c \
	fw.c \
	ps.c \
	sec.c \
	bf.c \
	sar.c \
	regd.c \
	rtw8821c.c \
	rtw8821c_table.c
ifeq ($(CONFIG_RTW88_BUS_USB),1)
obj-$(CONFIG_WLAN_LINE_RTW88) += \
	usb.c \
	rtw8821cu.c
else
obj-$(CONFIG_WLAN_LINE_RTW88) += \
	sdio.c \
	rtw8821cs.c
endif
endif

ifeq ($(SRC_TAG),kpi)
ifeq ($(CONFIG_WLAN_LINE_RTW88),1)
# The firmware blob is generated locally and git-ignored, so a clean checkout
# has to run the recipe before any image of this line; without this check the
# build stops much later on an undefined lkpi_fw_rtw8821c_fw_register.
RTW88_FW_BLOB := $(ROOT)/components/linuxkpi/fw/rtw8821c_fw_blob.c
ifeq ($(wildcard $(RTW88_FW_BLOB)),)
$(error the 8821C firmware blob $(RTW88_FW_BLOB) is missing: generate it with tools/rtos/fw-blob.py --symbol rtw8821c_fw, the recipe is in docs/FIRMWARE-NOTICES.md)
endif
obj-$(CONFIG_WLAN_LINE_RTW88) += $(RTW88_FW_BLOB)
endif
endif

ifeq ($(SRC_TAG),case)
obj-$(CONFIG_WLAN_LINE_RTW88) += \
	$(LINE)/app/cases/rtw88_scan.c \
	$(LINE)/app/cases/rtw88_join.c
ifeq ($(CONFIG_RTW88_BUS_USB),1)
obj-$(CONFIG_WLAN_LINE_RTW88) += \
	$(LINE)/app/cases/rtw88_usb.c
else
obj-$(CONFIG_WLAN_LINE_RTW88) += \
	$(LINE)/app/cases/rtw88.c
endif
endif

# The first-packet arm's IP presentation layer is an lwIP translation unit: its
# headers and the driver world's headers own the same names, so it compiles in
# the lwIP flag world (BASE_CFLAGS plus the bridge's include root) and never
# beside <linuxkpi_core.h>.
ifeq ($(SRC_TAG),lwip)
obj-$(CONFIG_WLAN_LINE_RTW88) += \
	$(LINE)/app/cases/rtw88_net.c
endif

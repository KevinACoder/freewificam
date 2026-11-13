# rtw89: the compiled set for the RTL8851BU (USB) under the feature
# face wireless-drivers/rtw89/driver_config.h (CORE/USB/8851B/8851BU
# on; PCI, every other chip, DEBUG/DEBUGMSG/DEBUGFS/LEDS/LEDS_MC/PM
# off).
ifeq ($(SRC_TAG),drv)
obj-$(CONFIG_LIBLINUX80211_RTW89) += \
	core.c \
	mac80211.c \
	mac.c \
	mac_be.c \
	phy.c \
	phy_be.c \
	fw.c \
	cam.c \
	efuse.c \
	efuse_be.c \
	regd.c \
	sar.c \
	coex.c \
	ps.c \
	chan.c \
	ser.c \
	acpi.c \
	util.c \
	rtw8851b.c \
	rtw8851b_table.c \
	rtw8851b_rfk.c \
	rtw8851b_rfk_table.c \
	rtw8851bu.c \
	usb.c
endif

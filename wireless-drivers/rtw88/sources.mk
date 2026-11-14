# rtw88: the compiled set for the RTL8821CU (USB) under the feature
# face wireless-drivers/rtw88/driver_config.h (CORE/USB/8821C/8821CU
# on; PCI, SDIO, every other chip, DEBUG/DEBUGFS/LEDS/PM off).
ifeq ($(SRC_TAG),drv)
obj-$(CONFIG_LIBLINUX80211_RTW88) += \
	main.c \
	mac80211.c \
	util.c \
	debug.c \
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
	rtw8821c_table.c \
	rtw8821cu.c \
	usb.c
endif

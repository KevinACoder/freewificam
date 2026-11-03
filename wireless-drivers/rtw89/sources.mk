# rtw89: the vendored RTL8851BU driver.
#
# The 8851BU translation units: the same 21 the reference board-proven image
# compiled (core, chip, and the two bus units).  The Wi-Fi 7 `*_be` units are
# not in this set; the linker's zero-undefined gate is the check that the chip
# selection does not reach them.  The firmware blob and the case live here too,
# because they are this driver's image identity, not the application's.

ifeq ($(SRC_TAG),drv)
obj-$(CONFIG_WLAN_LINE_RTW89) += \
	core.c \
	mac80211.c \
	mac.c \
	phy.c \
	fw.c \
	cam.c \
	efuse.c \
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
	usb.c \
	rtw8851bu.c
endif

ifeq ($(SRC_TAG),kpi)
obj-$(CONFIG_WLAN_LINE_RTW89) += \
	$(ROOT)/components/linuxkpi/fw/rtw8851b_fw_blob.c
endif

ifeq ($(SRC_TAG),case)
obj-$(CONFIG_WLAN_LINE_RTW89) += \
	$(LINE)/app/cases/rtw89.c \
	$(LINE)/app/cases/rtw89_scan.c \
	$(LINE)/app/cases/rtw89_join.c \
	$(LINE)/app/cases/rtw89_dataplane.c
endif

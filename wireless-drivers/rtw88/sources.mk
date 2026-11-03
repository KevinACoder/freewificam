# rtw88: the vendored RTL8821CS driver.
#
# The 8821CS translation units: the nineteen the chip selection resolves.
# wow.c (CONFIG_PM), led.c (CONFIG_RTW88_LEDS) and debug.c
# (CONFIG_RTW88_DEBUGFS) are not among them, for the reason rtw88_sdio.mk
# states.  The other chips' bus and chip units are not either - the linker's
# zero-undefined gate is the check.

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
	rtw8821c_table.c \
	sdio.c \
	rtw8821cs.c
endif

ifeq ($(SRC_TAG),kpi)
obj-$(CONFIG_WLAN_LINE_RTW88) += \
	$(ROOT)/components/linuxkpi/fw/rtw8821c_fw_blob.c
endif

ifeq ($(SRC_TAG),case)
obj-$(CONFIG_WLAN_LINE_RTW88) += \
	$(LINE)/app/cases/rtw88.c \
	$(LINE)/app/cases/rtw88_scan.c \
	$(LINE)/app/cases/rtw88_join.c
endif

# The first-packet arm's IP presentation layer is an lwIP translation unit: its
# headers and the driver world's headers own the same names, so it compiles in
# the lwIP flag world (BASE_CFLAGS plus the bridge's include root) and never
# beside <linuxkpi_core.h>.
ifeq ($(SRC_TAG),lwip)
obj-$(CONFIG_WLAN_LINE_RTW88) += \
	$(LINE)/app/cases/rtw88_net.c
endif

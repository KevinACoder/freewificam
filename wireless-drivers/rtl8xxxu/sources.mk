# rtl8xxxu: the vendored RTL8188EU/RTL8723BU driver.
#
# The driver is one module whose fileops table names every chip, so all nine
# translation units are in the link closure; the chip only selects the firmware
# blob.  The firmware unit is compiled in the linuxkpi flag world, the cases in
# the case world, and the driver units in the driver's own freestanding world.

ifeq ($(SRC_TAG),drv)
obj-$(CONFIG_WLAN_LINE_RTL8XXXU) += \
	core.c \
	8188e.c \
	8188f.c \
	8192c.c \
	8192e.c \
	8192f.c \
	8710b.c \
	8723a.c \
	8723b.c
endif

ifeq ($(SRC_TAG),kpi)
obj-$(CONFIG_WLAN_LINE_RTL8XXXU) += \
	$(if $(filter 8723bu,$(CONFIG_RTL8XXXU_CHIP)),$(ROOT)/components/linuxkpi/fw/rtl8723bu_fw_blob.c,$(ROOT)/components/linuxkpi/fw/rtl8188eufw_blob.c)
endif

ifeq ($(SRC_TAG),case)
obj-$(CONFIG_WLAN_LINE_RTL8XXXU) += \
	$(LINE)/app/cases/rtl8xxxu.c \
	$(LINE)/app/cases/rtl8xxxu_scan.c \
	$(LINE)/app/cases/rtl8xxxu_join.c \
	$(LINE)/app/cases/rtl8xxxu_dataplane.c
endif

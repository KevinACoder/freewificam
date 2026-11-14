# rtl8xxxu: the compiled set for the RTL8188EU (USB) under the
# feature face wireless-drivers/rtl8xxxu/driver_config.h (RTL8XXXU
# on, RTL8XXXU_UNTESTED off).  The driver is one module whose
# fileops table names every chip, so all nine units are in the link
# closure; the chip only selects the firmware blob.
ifeq ($(SRC_TAG),drv)
obj-$(CONFIG_LIBLINUX80211_RTL8XXXU) += \
	core.c \
	8192e.c \
	8723b.c \
	8723a.c \
	8192c.c \
	8188f.c \
	8188e.c \
	8710b.c \
	8192f.c
endif

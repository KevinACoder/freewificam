# @file    rtw89.mk
# @brief   The RTL8851BU wireless world: the vendored rtw89 driver over the
#          linuxkpi USB engine and CherryUSB, the NetBSD net80211 stack and its
#          bridge, and the compiled-in firmware blob.
#
# Composition mirrors `iwlwifi.mk` (net80211 is the MLME, linuxkpi supplies the
# Linux face) with one layer swapped: the bus is the linuxkpi USB engine over
# CherryUSB (`usb.mk` must be included first) instead of the PCI engine.  The
# driver compiles from the pinned tree with the four Kconfig keys the kernel
# resolves for the 8851BU; its bytes are not edited.
#
# Included only when the case list carries `rtw89`; the image's ladder is
# `rtw89` (bring-up: USB, firmware, attach, start) then the three bridge cases
# (`rtw89_scan`, `rtw89_join`, `rtw89_dataplane`) which only read the state the
# bring-up leaves behind.

RTW89_DRV := $(ROOT)/wireless-drivers/rtw89
RTW89_NET := $(ROOT)/components/net80211
RTW89_NPORT := $(RTW89_NET)/ports
RTW89_LKPI := $(ROOT)/components/linuxkpi/rtos

# The image's own names for the bridge.  rtw89 walks its firmware format
# numbers downwards, so 8851B asks for the -1 form before the bare one.
RTW89_IMAGE_NAMES :=

# The skb pool geometry the receive path needs: sixteen 20 KiB receive buffers
# are alive for the driver's lifetime and the transmit path allocates one per
# transfer, so the reference image's smaller defaults run dry before the firmware
# download finishes.  2048-byte data slots stay 2048: this driver's buffers are
# larger than that and are served from the heap, which is where the window goes.
RTW89_SKB_DEFS :=

# The linuxkpi USB adapter's stage must cover the driver's single bulk-in
# transfer (RTW89_USB_RECVBUF_SZ = 20480); a smaller slot refuses every receive
# transfer for good and the driver never sees a completion.  The gate below
# reads the driver's number instead of trusting this comment.
RTW89_USB_DEFS :=  \
	

# The controller the dongle is on, per arm: the xHCI profile lists both xHCI
# roots, the EHCI profile both USB2 roots.  The case starts each listed bus and
# waits once, so a device on the second root does not cost a wrong-controller
# timeout.
RTW89_BUS := $(CONFIG_RTW89_USB_BUS)
RTW89_BUS_DEFS := 

RTW89_BSD_INCLUDES := -I$(ROOT) -I$(RTW89_NPORT)/compat \
	-I$(RTW89_NET)/compat/netbsd -I$(RTW89_NET) -I$(RTW89_NPORT)

RTW89_KPI_INCLUDES := -I$(ROOT) -I$(ROOT)/components/linuxkpi \
	-I$(RTW89_LKPI) -I$(RTW89_LKPI)/include -I$(ROOT)/components/linuxkpi/rtos/usb \
	-I$(RTW89_LKPI)/usb -I$(RTW89_NET) -I$(RTW89_NPORT) \
	-I$(RTW89_NPORT)/bridge $(USB_INCLUDES)

RTW89_BSD_CFLAGS := $(filter-out -Wextra,$(BASE_CFLAGS)) -std=gnu11 \
	  -Wno-unused-function -Wno-pointer-sign \
	$(RTW89_IMAGE_NAMES) \
	-include $(RTW89_NPORT)/compat/prelude.h \
	$(RTW89_BSD_INCLUDES) $(RTW89_KPI_INCLUDES)

RTW89_KPI_CFLAGS := $(filter-out -Wextra,$(BASE_CFLAGS)) -std=gnu11 \
	  \
	$(RTW89_SKB_DEFS) $(RTW89_USB_DEFS) \
	 \
	$(RTW89_KPI_INCLUDES)

# The chip and bus selection the kernel's Kconfig resolves for the 8851BU.
RTW89_DRV_CONFIG :=

RTW89_DRV_CFLAGS := -std=c11 -O2 -Wall \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-march=armv8-a -mgeneral-regs-only -mstrict-align -mno-outline-atomics \
	-ffunction-sections -fdata-sections \
	$(RTW89_DRV_CONFIG) $(RTW89_SKB_DEFS) \
	  \
	-I$(ROOT) -I$(RTW89_LKPI) -I$(RTW89_LKPI)/include -I$(RTW89_DRV) \
	-I$(THIRD_PARTY)/ulog/src \
	-I$(CMSIS)/CMSIS/RTOS2/Include -Wno-unused-parameter \
	-Wno-address-of-packed-member \
	-include stdio.h -include string.h -include strings.h -include stdlib.h \
	

RTW89_CASE_CFLAGS := $(RTW89_KPI_CFLAGS) $(RTW89_BUS_DEFS)

RTW89_NET80211_SRCS := $(NET80211_SRCS)



RTW89_LKPI_SRCS := $(LINUXKPI_SRCS) $(NET80211_KPI_SRCS) \
	$(LINUXKPI_CHERRY_SRCS)

# The 8851BU translation units: the same 21 the reference board-proven image
# compiled (core, chip, and the two bus units).  The Wi-Fi 7 `*_be` units are
# not in this set; the linker's zero-undefined gate is the check that the chip
# selection does not reach them.
RTW89_DRV_SRCS := $(RTW89_IMPL_SRCS)

RTW89_FW_SRCS := $(RTW89_BLOB_SRCS)

RTW89_CASE_SRCS := $(RTW89_APP_SRCS)

RTW89_ALL_SRCS := $(RTW89_NET80211_SRCS) $(RTW89_LKPI_SRCS) \
	$(RTW89_DRV_SRCS) $(RTW89_FW_SRCS) $(RTW89_CASE_SRCS)

# The symbols only the case reaches: with --gc-sections the walkers, the
# backend registration, the firmware registration and the bridge install have
# no other reference.
RTW89_LDFLAGS := \
	-Wl,-u,usb_board_init \
	-Wl,-u,lkpi_usb_init \
	-Wl,-u,lkpi_usb_cherryusb_init \
	-Wl,-u,lkpi_fw_rtw8851b_fw_register \
	-Wl,-u,wlan_seam_adapter_init

# The driver's receive size must fit the adapter's stage, and the class table
# must be in the image (a class-info table that the linker dropped enumerates
# the device and then claims nothing).
RTW89_GATE_STAGE := $(shell sed -n 's/^#define[[:space:]]*RTW89_USB_RECVBUF_SZ[[:space:]]*\([0-9][0-9]*\).*/\1/p' $(RTW89_DRV)/usb.h)

# Static object tables: the driver's delayed-work population is larger than
# the storage line's.  These are global (the CMSIS2 backend's tables are built
# from them), so they are appended to the global flag set like iwlwifi.mk does.

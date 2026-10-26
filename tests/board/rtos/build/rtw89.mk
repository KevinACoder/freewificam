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
RTW89_IMAGE_NAMES := -DWLAN_BRIDGE_ADAPTER_NAME='"rtw89"' \
	-DWLAN_BRIDGE_FIRMWARE_NAME='"rtw89/rtw8851b_fw-1.bin"'

# The skb pool geometry the receive path needs: sixteen 20 KiB receive buffers
# are alive for the driver's lifetime and the transmit path allocates one per
# transfer, so the carrier's smaller defaults run dry before the firmware
# download finishes.  2048-byte data slots stay 2048: this driver's buffers are
# larger than that and are served from the heap, which is where the window goes.
RTW89_SKB_DEFS := -DCONFIG_LKPI_SKB_POOL_NUM=128 \
	-DCONFIG_LKPI_SKB_DATA_POOL_NUM=64 \
	-DCONFIG_LKPI_SKB_HEAP_HDR_MAX=64

# The linuxkpi USB adapter's stage must cover the driver's single bulk-in
# transfer (RTW89_USB_RECVBUF_SZ = 20480); a smaller slot refuses every receive
# transfer for good and the driver never sees a completion.  The gate below
# reads the driver's number instead of trusting this comment.
RTW89_USB_DEFS := -DCONFIG_LKPI_USB_CHERRYUSB_STAGE_SIZE=$(USB_STAGE_SIZE) \
	-DCONFIG_LKPI_USB_CHERRYUSB_SLOTS=16

# The controller the dongle is on, per arm: the xHCI profile lists both xHCI
# roots, the EHCI profile both USB2 roots.  The case starts each listed bus and
# waits once, so a device on the second root does not cost a wrong-controller
# timeout.
RTW89_BUS := $(if $(CONFIG_RTW89_USB_BUS),$(CONFIG_RTW89_USB_BUS),3)
RTW89_BUS_DEFS := -DRTW89_USB_BUS=$(RTW89_BUS)

RTW89_BSD_INCLUDES := -I$(ROOT) -I$(RTW89_NPORT)/compat \
	-I$(RTW89_NET)/compat/netbsd -I$(RTW89_NET) -I$(RTW89_NPORT)

RTW89_KPI_INCLUDES := -I$(ROOT) -I$(ROOT)/components/linuxkpi \
	-I$(RTW89_LKPI) -I$(RTW89_LKPI)/include -I$(USB_PORT) \
	-I$(RTW89_LKPI)/usb -I$(RTW89_NET) -I$(RTW89_NPORT) \
	-I$(RTW89_NPORT)/bridge $(USB_INCLUDES)

RTW89_BSD_CFLAGS := $(filter-out -Wextra,$(BASE_CFLAGS)) -std=gnu11 \
	-D_KERNEL -DWLAN_PORT_TARGET -Wno-unused-function -Wno-pointer-sign \
	$(RTW89_IMAGE_NAMES) \
	-include $(RTW89_NPORT)/compat/prelude.h \
	$(RTW89_BSD_INCLUDES) $(RTW89_KPI_INCLUDES)

RTW89_KPI_CFLAGS := $(filter-out -Wextra,$(BASE_CFLAGS)) -std=gnu11 \
	-DWLAN_PORT_TARGET -DLKPI_ABS_PLATFORM_PROVIDED \
	$(RTW89_SKB_DEFS) $(RTW89_USB_DEFS) \
	-DCMSIS_device_header='"cmsis_os2.h"' \
	$(RTW89_KPI_INCLUDES)

# The chip and bus selection the kernel's Kconfig resolves for the 8851BU.
RTW89_DRV_CONFIG := -DCONFIG_RTW89_CORE -DCONFIG_RTW89_8851B \
	-DCONFIG_RTW89_USB -DCONFIG_RTW89_8851BU

RTW89_DRV_CFLAGS := -std=c11 -O2 -Wall \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-march=armv8-a -mgeneral-regs-only -mstrict-align -mno-outline-atomics \
	-ffunction-sections -fdata-sections \
	$(RTW89_DRV_CONFIG) $(RTW89_SKB_DEFS) \
	-DWLAN_PORT_TARGET -DLKPI_ABS_PLATFORM_PROVIDED \
	-I$(ROOT) -I$(RTW89_LKPI) -I$(RTW89_LKPI)/include -I$(RTW89_DRV) \
	-I$(THIRD_PARTY)/ulog/src \
	-I$(CMSIS)/CMSIS/RTOS2/Include -Wno-unused-parameter \
	-Wno-address-of-packed-member \
	-include stdio.h -include string.h -include strings.h -include stdlib.h \
	-D_DEFAULT_SOURCE=1

RTW89_CASE_CFLAGS := $(RTW89_KPI_CFLAGS) $(RTW89_BUS_DEFS)

RTW89_NET80211_SRCS := \
	$(RTW89_NET)/net80211/ieee80211.c \
	$(RTW89_NET)/net80211/ieee80211_proto.c \
	$(RTW89_NET)/net80211/ieee80211_node.c \
	$(RTW89_NET)/net80211/ieee80211_output.c \
	$(RTW89_NET)/net80211/ieee80211_input.c \
	$(RTW89_NET)/net80211/ieee80211_amrr.c \
	$(RTW89_NET)/net80211/ieee80211_netbsd.c \
	$(RTW89_NET)/net80211/ieee80211_crypto.c \
	$(RTW89_NET)/net80211/ieee80211_crypto_none.c \
	$(RTW89_NET)/net80211/ieee80211_crypto_ccmp.c \
	$(RTW89_NET)/crypto/aes/aes_bear.c \
	$(RTW89_NET)/crypto/aes/aes_ct.c \
	$(RTW89_NET)/crypto/aes/aes_ct_enc.c \
	$(RTW89_NET)/crypto/aes/aes_ct_dec.c \
	$(RTW89_NET)/crypto/aes/aes_ccm.c \
	$(RTW89_NET)/crypto/aes/aes_ccm_mbuf.c \
	$(RTW89_NET)/port/aes_impl_compat.c

RTW89_NPORT_SRCS := \
	$(RTW89_NPORT)/osal/cmsis2/osal_memory_target.c \
	$(RTW89_NPORT)/osal/cmsis2/osal_cmsis2.c \
	$(RTW89_NPORT)/net/mbuf.c \
	$(RTW89_NPORT)/net/ifnet.c

RTW89_BRIDGE_SRCS := $(RTW89_NPORT)/bridge/wlan_bridge.c

RTW89_LKPI_SRCS := \
	$(RTW89_LKPI)/linuxkpi_core.c \
	$(RTW89_LKPI)/linuxkpi_compat.c \
	$(RTW89_LKPI)/linuxkpi_platform_target.c \
	$(RTW89_LKPI)/linuxkpi_80211.c \
	$(RTW89_LKPI)/linuxkpi_80211_helpers.c \
	$(RTW89_LKPI)/linuxkpi_wiphy_work.c \
	$(RTW89_LKPI)/linuxkpi_txq.c \
	$(RTW89_LKPI)/linuxkpi_netdev.c \
	$(RTW89_LKPI)/linuxkpi_cfg80211.c \
	$(RTW89_LKPI)/linuxkpi_osdep.c \
	$(RTW89_LKPI)/lkpi_tcp_trace.c \
	$(RTW89_LKPI)/fw_registry_rtos.c \
	$(RTW89_LKPI)/skbuff/skbuff_pool.c \
	$(RTW89_LKPI)/usb/linuxkpi_usb.c \
	$(RTW89_LKPI)/usb/lkpi_usbh_class.c \
	$(RTW89_LKPI)/usb/lkpi_usbh_modeswitch.c \
	$(RTW89_NPORT)/bridge/wlan_seam_adapter.c \
	$(USB_PORT)/lkpi_usb_cherryusb.c

# The 8851BU translation units: the same 21 the carrier's board-proven image
# compiled (core, chip, and the two bus units).  The Wi-Fi 7 `*_be` units are
# not in this set; the linker's zero-undefined gate is the check that the chip
# selection does not reach them.
RTW89_DRV_SRCS := \
	$(RTW89_DRV)/core.c \
	$(RTW89_DRV)/mac80211.c \
	$(RTW89_DRV)/mac.c \
	$(RTW89_DRV)/phy.c \
	$(RTW89_DRV)/fw.c \
	$(RTW89_DRV)/cam.c \
	$(RTW89_DRV)/efuse.c \
	$(RTW89_DRV)/regd.c \
	$(RTW89_DRV)/sar.c \
	$(RTW89_DRV)/coex.c \
	$(RTW89_DRV)/ps.c \
	$(RTW89_DRV)/chan.c \
	$(RTW89_DRV)/ser.c \
	$(RTW89_DRV)/acpi.c \
	$(RTW89_DRV)/util.c \
	$(RTW89_DRV)/rtw8851b.c \
	$(RTW89_DRV)/rtw8851b_table.c \
	$(RTW89_DRV)/rtw8851b_rfk.c \
	$(RTW89_DRV)/rtw8851b_rfk_table.c \
	$(RTW89_DRV)/usb.c \
	$(RTW89_DRV)/rtw8851bu.c

RTW89_FW_SRCS := $(ROOT)/components/linuxkpi/fw/rtw8851b_fw_blob.c

RTW89_CASE_SRCS := \
	$(LINE)/app/cases/rtw89.c \
	$(LINE)/app/cases/rtw89_scan.c \
	$(LINE)/app/cases/rtw89_join.c \
	$(LINE)/app/cases/rtw89_dataplane.c

RTW89_ALL_SRCS := $(RTW89_NET80211_SRCS) $(RTW89_NPORT_SRCS) \
	$(RTW89_BRIDGE_SRCS) $(RTW89_LKPI_SRCS) $(RTW89_DRV_SRCS) \
	$(RTW89_FW_SRCS) $(RTW89_CASE_SRCS)

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
BASE_CFLAGS += -DCMSIS2_MAX_TIMERS=32 -DCMSIS2_MAX_THREADS=16
CFLAGS += -DCMSIS2_MAX_TIMERS=32 -DCMSIS2_MAX_THREADS=16
ASM_CFLAGS += -DCMSIS2_MAX_TIMERS=32 -DCMSIS2_MAX_THREADS=16

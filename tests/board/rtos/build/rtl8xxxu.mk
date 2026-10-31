# @file    rtl8xxxu.mk
# @brief   The Realtek USB wireless world (RTL8188EUS, RTL8723BU): the vendored
#          rtl8xxxu driver over the linuxkpi USB engine and CherryUSB, the
#          NetBSD net80211 stack and its bridge, and the compiled-in firmware
#          blob.
#
# Composition mirrors `rtw89.mk` (net80211 is the MLME, linuxkpi supplies the
# Linux face) with the same bus layer: the linuxkpi USB engine over CherryUSB
# (`usb.mk` must be included first).  The driver compiles from the pinned tree
# with no Kconfig key: `rtl8xxxu` is one module whose fileops table names every
# chip, so all nine units are in the link closure exactly as the kernel's own
# Makefile and the linux-ko arm build them (`CONFIG_RTL8XXXU_UNTESTED` stays
# undefined, matching the kernel's default).
#
# The image's ladder is `rtl8xxxu` (bring-up: USB, efuse/MAC, firmware, attach,
# start) followed by `rtl8xxxu_scan`, `rtl8xxxu_join`, `rtl8xxxu_dataplane` and
# `rtl8xxxu_net`, which only read the state the bring-up leaves behind.
#
# Geometry: this chip's receive path is 32 bulk-in URBs of
# IEEE80211_MAX_FRAME_LEN + its 24-byte RX descriptor (~2.3 KiB), and its
# transmit path one skb per URB with aggregation off by default.  A 4096-byte
# transfer stage therefore covers the widest single transfer with room to
# spare; the gate below keeps a later edit from shrinking it under the RX URB.

comma := ,
empty :=
space := $(empty) $(empty)

RTL8XXXU_DRV := $(ROOT)/wireless-drivers/rtl8xxxu
RTL8XXXU_NET := $(ROOT)/components/net80211
RTL8XXXU_NPORT := $(RTL8XXXU_NET)/ports
RTL8XXXU_LKPI := $(ROOT)/components/linuxkpi/rtos

# The chip this image is for.  The driver is one module whose fileops table
# names every chip, so the image's identity - the firmware blob, the name the
# bridge reports, and the id line the case promotes - is a profile fact, not a
# driver one.  8188eu is the default, so the earlier line's numbers stand.
RTL8XXXU_CHIP := $(if $(CONFIG_RTL8XXXU_CHIP),$(CONFIG_RTL8XXXU_CHIP),8188eu)
ifeq ($(RTL8XXXU_CHIP),8723bu)
RTL8XXXU_FW_NAME := rtlwifi/rtl8723bu_nic.bin
RTL8XXXU_FW_BLOB := $(ROOT)/components/linuxkpi/fw/rtl8723bu_fw_blob.c
RTL8XXXU_FW_REGISTER := lkpi_fw_rtl8723bu_register
RTL8XXXU_CLAIM_ID := 0bda:b720
else
RTL8XXXU_FW_NAME := rtlwifi/rtl8188eufw.bin
RTL8XXXU_FW_BLOB := $(ROOT)/components/linuxkpi/fw/rtl8188eufw_blob.c
RTL8XXXU_FW_REGISTER := lkpi_fw_rtl8188eufw_register
RTL8XXXU_CLAIM_ID := 0bda:8179
endif

# The image's own names for the bridge.  The bridge's defaults are already this
# line's (`wlan_bridge.h`), and naming them here is the guard: a changed default
# must not silently point this image at another driver's identity.
RTL8XXXU_IMAGE_NAMES := -DWLAN_BRIDGE_ADAPTER_NAME='"rtl8xxxu"' \
	-DWLAN_BRIDGE_FIRMWARE_NAME='"$(RTL8XXXU_FW_NAME)"'

# Where the bench AP is: the caller passes `CONFIG_RTL8XXXU_AP_SSID` and
# `CONFIG_RTL8XXXU_AP_FREQ` through the profile, so the case joins the network
# the round's gate actually measured instead of a BSSID from an older bench.
RTL8XXXU_AP_DEFS := \
	-DRTL8XXXU_AP_SSID='"$(if $(CONFIG_RTL8XXXU_AP_SSID),$(CONFIG_RTL8XXXU_AP_SSID),labwifi-brmc)"' \
	-DRTL8XXXU_AP_FREQ=$(if $(CONFIG_RTL8XXXU_AP_FREQ),$(CONFIG_RTL8XXXU_AP_FREQ),2432)

# The dongle this image is for.  The bench carries more than one rtl8xxxu
# device (an RTL8723BU sits on the USB2 root), and the driver's id_table claims
# both, so "attached" alone does not say which chip the ladder measured.  The
# expected MAC is an arm fact, recorded in the profile, and the bring-up case
# refuses an attach whose address is not this one.
RTL8XXXU_EXPECT_MAC := $(if $(CONFIG_RTL8XXXU_EXPECT_MAC),$(CONFIG_RTL8XXXU_EXPECT_MAC),20:f4:1b:52:8f:01)
RTL8XXXU_MAC_BYTES := $(subst :, ,$(RTL8XXXU_EXPECT_MAC))
RTL8XXXU_MAC_DEFS := -DRTL8XXXU_EXPECT_MAC='{$(foreach b,$(RTL8XXXU_MAC_BYTES),0x$(b)$(comma))}'

# The skb pool geometry the receive path needs: 32 receive buffers are alive
# for the driver's lifetime, plus one allocation per transmit transfer.
RTL8XXXU_SKB_DEFS := -DCONFIG_LKPI_SKB_POOL_NUM=128 \
	-DCONFIG_LKPI_SKB_DATA_POOL_NUM=64 \
	-DCONFIG_LKPI_SKB_HEAP_HDR_MAX=64

# The adapter's geometry.  The stage is the widest single transfer: the 8188
# line's 2328-byte receive URB here, and 8723BU's 4096-byte firmware page is
# written as 1024-byte control blocks (`writeN_block_size`), so 4096 covers
# both with room to spare.  The slots are the engine's concurrency, unchanged
# from the rtw89 image.
RTL8XXXU_STAGE ?= 4096
RTL8XXXU_STAGE_MIN := 2328	# IEEE80211_MAX_FRAME_LEN (2304) + rxdesc16/24 (24)
RTL8XXXU_USB_DEFS := -DCONFIG_LKPI_USB_CHERRYUSB_STAGE_SIZE=$(RTL8XXXU_STAGE) \
	-DCONFIG_LKPI_USB_CHERRYUSB_SLOTS=16

# The controller list, per arm.  3 = both xHCI roots (the lab's 480 M path),
# 1 = both USB2 roots (the panel's EHCI path), 4 = all four, for the one
# discovery boot that answers which controller carries the dongle.  The case
# prints the list and then the bus that actually attached.
RTL8XXXU_BUS := $(if $(CONFIG_RTL8XXXU_USB_BUS),$(CONFIG_RTL8XXXU_USB_BUS),3)
RTL8XXXU_BUS_DEFS := -DRTL8XXXU_USB_BUS=$(RTL8XXXU_BUS)

# The chip's identity, handed to the case: which id line is the claim anchor
# and which register function belongs to the blob this image carries.
RTL8XXXU_ID_DEFS := -DRTL8XXXU_FW_REGISTER=$(RTL8XXXU_FW_REGISTER) \
	-DRTL8XXXU_CLAIM_ID='"$(RTL8XXXU_CLAIM_ID)"'

RTL8XXXU_BSD_INCLUDES := -I$(ROOT) -I$(RTL8XXXU_NPORT)/compat \
	-I$(RTL8XXXU_NET)/compat/netbsd -I$(RTL8XXXU_NET) -I$(RTL8XXXU_NPORT)

RTL8XXXU_KPI_INCLUDES := -I$(ROOT) -I$(ROOT)/components/linuxkpi \
	-I$(RTL8XXXU_LKPI) -I$(RTL8XXXU_LKPI)/include -I$(USB_PORT) \
	-I$(RTL8XXXU_LKPI)/usb -I$(RTL8XXXU_NET) -I$(RTL8XXXU_NPORT) \
	-I$(RTL8XXXU_NPORT)/bridge $(USB_INCLUDES)

RTL8XXXU_BSD_CFLAGS := $(filter-out -Wextra,$(BASE_CFLAGS)) -std=gnu11 \
	-D_KERNEL -DWLAN_PORT_TARGET -Wno-unused-function -Wno-pointer-sign \
	$(RTL8XXXU_IMAGE_NAMES) $(RTL8XXXU_AP_DEFS) $(RTL8XXXU_MAC_DEFS) \
	-include $(RTL8XXXU_NPORT)/compat/prelude.h \
	$(RTL8XXXU_BSD_INCLUDES) $(RTL8XXXU_KPI_INCLUDES)

RTL8XXXU_KPI_CFLAGS := $(filter-out -Wextra,$(BASE_CFLAGS)) -std=gnu11 \
	-DWLAN_PORT_TARGET -DLKPI_ABS_PLATFORM_PROVIDED \
	$(RTL8XXXU_SKB_DEFS) $(RTL8XXXU_USB_DEFS) $(RTL8XXXU_AP_DEFS) \
	$(RTL8XXXU_MAC_DEFS) \
	-DCMSIS_device_header='"cmsis_os2.h"' \
	$(RTL8XXXU_KPI_INCLUDES)

# The driver's own flag world: the freestanding set the reference image compiled
# these nine units with (and the linux-ko arm builds them with the kernel's
# equivalent).  No Kconfig define: the chip table selects the fileops.
RTL8XXXU_DRV_CFLAGS := -std=c11 -O2 -Wall \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-march=armv8-a -mgeneral-regs-only -mstrict-align -mno-outline-atomics \
	-ffunction-sections -fdata-sections \
	$(RTL8XXXU_IMAGE_NAMES) $(RTL8XXXU_SKB_DEFS) \
	-DWLAN_PORT_TARGET -DLKPI_ABS_PLATFORM_PROVIDED \
	-I$(ROOT) -I$(RTL8XXXU_LKPI) -I$(RTL8XXXU_LKPI)/include \
	-I$(RTL8XXXU_DRV) -I$(THIRD_PARTY)/ulog/src \
	-I$(CMSIS)/CMSIS/RTOS2/Include \
	-Wno-unused-parameter \
	-include stdio.h -include string.h -include strings.h -include stdlib.h \
	-D_DEFAULT_SOURCE=1

# Diagnostic only: a high-priority thread that reports bridge/workqueue
# counters through the polled UART while the bring-up waits for attach.  Used
# to separate "the kernel/console is dead" from "the runner thread is blocked".
ifneq ($(CONFIG_RTL8XXXU_HB),)
RTL8XXXU_HB_DEFS := -DRTL8XXXU_HB=$(CONFIG_RTL8XXXU_HB)
endif

RTL8XXXU_CASE_CFLAGS := $(RTL8XXXU_KPI_CFLAGS) $(RTL8XXXU_BUS_DEFS) \
	$(RTL8XXXU_ID_DEFS) $(RTL8XXXU_HB_DEFS)

RTL8XXXU_NET80211_SRCS := \
	$(RTL8XXXU_NET)/net80211/ieee80211.c \
	$(RTL8XXXU_NET)/net80211/ieee80211_proto.c \
	$(RTL8XXXU_NET)/net80211/ieee80211_node.c \
	$(RTL8XXXU_NET)/net80211/ieee80211_output.c \
	$(RTL8XXXU_NET)/net80211/ieee80211_input.c \
	$(RTL8XXXU_NET)/net80211/ieee80211_amrr.c \
	$(RTL8XXXU_NET)/net80211/ieee80211_netbsd.c \
	$(RTL8XXXU_NET)/net80211/ieee80211_crypto.c \
	$(RTL8XXXU_NET)/net80211/ieee80211_crypto_none.c \
	$(RTL8XXXU_NET)/net80211/ieee80211_crypto_ccmp.c \
	$(RTL8XXXU_NET)/crypto/aes/aes_bear.c \
	$(RTL8XXXU_NET)/crypto/aes/aes_ct.c \
	$(RTL8XXXU_NET)/crypto/aes/aes_ct_enc.c \
	$(RTL8XXXU_NET)/crypto/aes/aes_ct_dec.c \
	$(RTL8XXXU_NET)/crypto/aes/aes_ccm.c \
	$(RTL8XXXU_NET)/crypto/aes/aes_ccm_mbuf.c \
	$(RTL8XXXU_NET)/port/aes_impl_compat.c

RTL8XXXU_NPORT_SRCS := \
	$(RTL8XXXU_NPORT)/osal/cmsis2/osal_memory_target.c \
	$(RTL8XXXU_NPORT)/osal/cmsis2/osal_cmsis2.c \
	$(RTL8XXXU_NPORT)/net/mbuf.c \
	$(RTL8XXXU_NPORT)/net/ifnet.c

RTL8XXXU_BRIDGE_SRCS := $(RTL8XXXU_NPORT)/bridge/wlan_bridge.c

RTL8XXXU_LKPI_SRCS := \
	$(RTL8XXXU_LKPI)/linuxkpi_core.c \
	$(RTL8XXXU_LKPI)/linuxkpi_compat.c \
	$(RTL8XXXU_LKPI)/linuxkpi_platform_target.c \
	$(RTL8XXXU_LKPI)/linuxkpi_80211.c \
	$(RTL8XXXU_LKPI)/linuxkpi_80211_helpers.c \
	$(RTL8XXXU_LKPI)/linuxkpi_wiphy_work.c \
	$(RTL8XXXU_LKPI)/linuxkpi_txq.c \
	$(RTL8XXXU_LKPI)/linuxkpi_netdev.c \
	$(RTL8XXXU_LKPI)/linuxkpi_cfg80211.c \
	$(RTL8XXXU_LKPI)/linuxkpi_osdep.c \
	$(RTL8XXXU_LKPI)/lkpi_tcp_trace.c \
	$(RTL8XXXU_LKPI)/fw_registry_rtos.c \
	$(RTL8XXXU_LKPI)/skbuff/skbuff_pool.c \
	$(RTL8XXXU_LKPI)/usb/linuxkpi_usb.c \
	$(RTL8XXXU_LKPI)/usb/lkpi_usbh_class.c \
	$(RTL8XXXU_LKPI)/usb/lkpi_usbh_modeswitch.c \
	$(RTL8XXXU_NPORT)/bridge/wlan_seam_adapter.c \
	$(USB_PORT)/lkpi_usb_cherryusb.c

# The nine translation units the driver's Makefile lists: core plus every
# chip's fileops (core.c's table references them, so the set is the link
# closure, not a menu).
RTL8XXXU_DRV_SRCS := \
	$(RTL8XXXU_DRV)/core.c \
	$(RTL8XXXU_DRV)/8188e.c \
	$(RTL8XXXU_DRV)/8188f.c \
	$(RTL8XXXU_DRV)/8192c.c \
	$(RTL8XXXU_DRV)/8192e.c \
	$(RTL8XXXU_DRV)/8192f.c \
	$(RTL8XXXU_DRV)/8710b.c \
	$(RTL8XXXU_DRV)/8723a.c \
	$(RTL8XXXU_DRV)/8723b.c

RTL8XXXU_FW_SRCS := $(RTL8XXXU_FW_BLOB)

RTL8XXXU_CASE_SRCS := \
	$(LINE)/app/cases/rtl8xxxu.c \
	$(LINE)/app/cases/rtl8xxxu_scan.c \
	$(LINE)/app/cases/rtl8xxxu_join.c \
	$(LINE)/app/cases/rtl8xxxu_dataplane.c

RTL8XXXU_ALL_SRCS := $(RTL8XXXU_NET80211_SRCS) $(RTL8XXXU_NPORT_SRCS) \
	$(RTL8XXXU_BRIDGE_SRCS) $(RTL8XXXU_LKPI_SRCS) $(RTL8XXXU_DRV_SRCS) \
	$(RTL8XXXU_FW_SRCS) $(RTL8XXXU_CASE_SRCS)

# The lwIP-over-bridge image (case rtl8xxxu_net): the stack's port glue and the
# lwIP core in the platform's flag world, plus the case itself.  The flags are
# taken from BASE_CFLAGS *before* the Linux face is appended, so the lwIP units
# never compile against <net/mac80211.h>; deriving them from BASE_CFLAGS also
# keeps them inside the existing flag stamp.
ifneq ($(filter rtl8xxxu_net,$(CASES)),)
RTL8XXXU_NET_SRCS := \
	$(THIRD_PARTY)/lwip/ports/sys_arch.c \
	$(THIRD_PARTY)/lwip/ports/platform_hooks.c \
	$(LWIP_STACK_SRCS) \
	$(LINE)/app/cases/rtl8xxxu_net.c
# The uplink on this line is rate-control limited (the same 1 Mbit/s shape the
# iwl7260 line measured), so an iperf2 client session can starve its send
# window and hit lwIP's 10-poll idle abort before its own 10 s duration check
# fires - the session then reports ABORTED_LOCAL with bytes already carried.
# The idle limit is the instrument's, not the link's: give the client room to
# finish and report DONE_CLIENT.
RTL8XXXU_NET_CFLAGS := $(filter-out -Wextra,$(BASE_CFLAGS)) -std=gnu11 \
	-DLWIPERF_TCP_MAX_IDLE_SEC=60 \
	$(RTL8XXXU_AP_DEFS) -I$(RTL8XXXU_NPORT)/bridge
else
RTL8XXXU_NET_SRCS :=
endif

# The WLAN NETIO image (case rtl8xxxu_netio): the same lwIP stack and
# presentation layer as rtl8xxxu_net, plus the shared netio server.  The server
# is compiled against the WLAN case symbol (RTOS_CASE_RTL8XXXU_NETIO), which
# selects its bridge-backed line-up and layer snapshot instead of the GMAC
# ones; the wired net_mdk_netio case is a different image and is untouched.
ifneq ($(filter rtl8xxxu_netio,$(CASES)),)
RTL8XXXU_NETIO_SRCS := \
	$(THIRD_PARTY)/lwip/ports/sys_arch.c \
	$(THIRD_PARTY)/lwip/ports/platform_hooks.c \
	$(LWIP_STACK_SRCS) \
	$(LINE)/app/cases/rtl8xxxu_net.c \
	$(LINE)/app/cases/net_mdk_netio.c \
	$(LINE)/app/net_prof.c
RTL8XXXU_NETIO_CFLAGS := $(filter-out -Wextra,$(BASE_CFLAGS)) -std=gnu11 \
	-DLWIPERF_TCP_MAX_IDLE_SEC=60 \
	$(RTL8XXXU_AP_DEFS) -I$(RTL8XXXU_NPORT)/bridge
else
RTL8XXXU_NETIO_SRCS :=
endif

# The symbols only the case reaches: with --gc-sections the walkers, the
# backend registration, the firmware registration and the bridge install have
# no other reference.
RTL8XXXU_LDFLAGS := \
	-Wl,-u,usb_board_init \
	-Wl,-u,lkpi_usb_init \
	-Wl,-u,lkpi_usb_cherryusb_init \
	-Wl,-u,$(RTL8XXXU_FW_REGISTER) \
	-Wl,-u,wlan_seam_adapter_init

# The driver's widest receive transfer must fit the adapter's stage, and the
# class table must be in the image (a class-info table that the linker dropped
# enumerates the device and then claims nothing).
RTL8XXXU_GATE_STAGE := $(RTL8XXXU_STAGE)

# Static object tables: the driver's delayed-work population plus the net
# case's thread.  Global, because the CMSIS2 backend's tables are built from
# them; appended like rtw89.mk does.
BASE_CFLAGS += -DCMSIS2_MAX_TIMERS=32 -DCMSIS2_MAX_THREADS=16
CFLAGS += -DCMSIS2_MAX_TIMERS=32 -DCMSIS2_MAX_THREADS=16
ASM_CFLAGS += -DCMSIS2_MAX_TIMERS=32 -DCMSIS2_MAX_THREADS=16
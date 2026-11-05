# @file    rtw88_usb.mk
# @brief   The RTL8821CU wireless world: the vendored rtw88 driver over the
#          linuxkpi USB engine and CherryUSB, the NetBSD net80211 stack and its
#          bridge, and the compiled-in firmware blob.
#
# Composition mirrors `rtw89.mk` (net80211 is the MLME, linuxkpi supplies the
# Linux face over USB) with the driver family swapped to rtw88's 8821CU arm.
# The SDIO sibling `rtw88_sdio.mk` keeps the same driver core, blob and bridge;
# only the port units, the probe path and the case ladder differ, and the two
# are never selected together (rtw88_sdio.mk compiles the MCI host glue under
# the imported stack's generic symbol names, which the USB world must not link).
#
# Included only when the case list carries `rtw88_usb`; `usb.mk` must be
# included before it (the engine compiles against CherryUSB's include roots and
# the stage geometry that file owns).

ifneq ($(filter rtw88_usb,$(CASES)),)

RTW88_DRV := $(ROOT)/wireless-drivers/rtw88
RTW88_NET := $(ROOT)/components/net80211
RTW88_NPORT := $(RTW88_NET)/ports
RTW88_LKPI := $(ROOT)/components/linuxkpi/rtos

# The image's own names for the bridge: the adapter name the driver line
# reports and the firmware name the driver asks the registry for.
RTW88_IMAGE_NAMES :=

# The skb pool geometry the receive path needs.  rtw88's USB receive path holds
# RTW_USB_RX_SKB_NUM = 8 buffers of up to RTW_USB_MAX_RECVBUF_SZ (32 KiB) for
# the driver's lifetime; the payloads come from the heap (larger than a pool
# data slot), so only the headers are pooled and the heap-header ceiling has to
# cover the ones the pool hands out.
RTW88_SKB_DEFS :=

# The net80211 bridge's port_* observation hooks ask the URB engine for its
# completion census; this line's transport is USB, so they stay compiled in
# (the SDIO arm compiles them out).
RTW88_SEAM_DEFS :=

# The controller the dongle is on, per arm: the xHCI profile lists both xHCI
# roots, the EHCI profile both USB2 roots.  The case starts each listed bus and
# waits once, so a device on the second root does not cost a wrong-controller
# timeout.
RTW88_BUS := $(CONFIG_RTW88_USB_BUS)
RTW88_BUS_DEFS :=

RTW88_BSD_INCLUDES := -I$(ROOT) -I$(RTW88_NPORT)/compat \
	-I$(RTW88_NET)/compat/netbsd -I$(RTW88_NET) -I$(RTW88_NPORT)

RTW88_KPI_INCLUDES := -I$(ROOT) -I$(ROOT)/components/linuxkpi \
	-I$(RTW88_LKPI) -I$(RTW88_LKPI)/include \
	-I$(ROOT)/components/linuxkpi/rtos/usb -I$(RTW88_LKPI)/usb \
	-I$(RTW88_LKPI)/skbuff -I$(RTW88_NET) -I$(RTW88_NPORT) \
	-I$(RTW88_NPORT)/bridge $(USB_INCLUDES)

RTW88_BSD_CFLAGS := $(filter-out -Wextra,$(BASE_CFLAGS)) -std=gnu11 \
	  -Wno-unused-function -Wno-pointer-sign \
	$(RTW88_IMAGE_NAMES) $(RTW88_SEAM_DEFS) \
	-include $(RTW88_NPORT)/compat/prelude.h \
	$(RTW88_BSD_INCLUDES) $(RTW88_KPI_INCLUDES)

RTW88_KPI_CFLAGS := $(filter-out -Wextra,$(BASE_CFLAGS)) -std=gnu11 \
	  \
	$(RTW88_SKB_DEFS) $(RTW88_SEAM_DEFS) \
	 \
	$(RTW88_KPI_INCLUDES)

# The chip and bus selection the kernel's Kconfig resolves for the 8821CU.  The
# source list (wireless-drivers/rtw88/sources.mk) carries the bus units, so the
# driver compiles with no -D: CONFIG_PM, CONFIG_RTW88_LEDS and
# CONFIG_RTW88_DEBUGFS stay absent - the kernel's Makefile gates wow.o, led.o
# and debug.c on them, and with none defined the driver's own headers supply
# empty stubs.
RTW88_DRV_CONFIG :=

# -fno-strict-aliasing: the imported driver is written under the kernel's own
# flag contract, and one place depends on it.  fill_txdesc_checksum_common()
# (wireless-drivers/rtw88/tx.h) zeroes a __le32 field and then XORs the same 32
# bytes through a __le16 pointer; at -O2 with strict aliasing the XOR reads the
# word from before the zeroing.  The aggregate head's TX descriptor checksum
# then comes out 0x0300 instead of 0x1f00 while every member's stays right, and
# the chip drops the whole multi-frame URB - the "aggregation works host-side
# but only 1 frame per URB is delivered" shape of RTOS-AGG-02/03.
RTW88_DRV_CFLAGS := -std=c11 -O2 -Wall \
	-ffreestanding -fno-builtin -fno-stack-protector -fno-strict-aliasing \
	-march=armv8-a -mgeneral-regs-only -mstrict-align -mno-outline-atomics \
	-ffunction-sections -fdata-sections \
	$(RTW88_DRV_CONFIG) $(RTW88_SKB_DEFS) \
	  \
	-I$(ROOT) -I$(RTW88_LKPI) -I$(RTW88_LKPI)/include -I$(RTW88_DRV) \
	-I$(THIRD_PARTY)/ulog/src \
	-I$(CMSIS)/CMSIS/RTOS2/Include -Wno-unused-parameter \
	-Wno-address-of-packed-member \
	-include stdio.h -include string.h -include strings.h -include stdlib.h \
	

# The cases read the bridge and the adapter's own entry points
# (lkpi_usb_cherryusb_stats, wlan_bridge_*), so they compile in the same flag
# world as the linuxkpi face.
RTW88_CASE_CFLAGS := $(RTW88_KPI_CFLAGS) $(RTW88_BUS_DEFS)

RTW88_NET80211_SRCS := $(NET80211_SRCS)

# The USB engine: the linuxkpi face, the net80211 glue and the CherryUSB
# backend units (usb.mk owns the CherryUSB stack itself).
RTW88_LKPI_SRCS := $(LINUXKPI_SRCS) $(NET80211_KPI_SRCS) \
	$(LINUXKPI_CHERRY_SRCS) $(NET80211_RXBA_SRCS)

RTW88_USB_PORT_SRCS :=

# The 8821CU translation units: the shared core, the chip pair and the two USB
# units, as wireless-drivers/rtw88/sources.mk lists them for the USB arm.
RTW88_DRV_SRCS := $(RTW88_IMPL_SRCS)

RTW88_FW_SRCS := $(RTW88_BLOB_SRCS)

RTW88_CASE_SRCS := $(RTW88_APP_SRCS)

# The netio arm (case rtw88_netio): the same lwIP core and presentation layer as
# rtw88_net, plus the shared netio server.  The server is compiled against the
# WLAN case symbol (RTOS_CASE_RTW88_NETIO), which selects its bridge-backed
# line-up and layer snapshot instead of the GMAC ones; the wired net_mdk_netio
# case is a different image and is untouched.
ifneq ($(filter rtw88_netio,$(CASES)),)
RTW88_NETIO_SRCS := \
	$(THIRD_PARTY)/lwip/ports/sys_arch.c \
	$(THIRD_PARTY)/lwip/ports/platform_hooks.c \
	$(LWIP_STACK_SRCS) \
	$(LINE)/app/cases/rtw88_net.c \
	$(LINE)/app/cases/net_mdk_netio.c \
	$(LINE)/app/net_prof.c
RTW88_NETIO_CFLAGS := $(filter-out -Wextra,$(BASE_CFLAGS)) -std=gnu11 \
	-I$(RTW88_NPORT)/bridge \
	-I$(ROOT)/components/linuxkpi -I$(RTW88_LKPI)/usb \
	-I$(ROOT)/components/linuxkpi/rtos/usb \
	-I$(ROOT)/components/linuxkpi/rtos/skbuff
else
RTW88_NETIO_SRCS :=
endif

# The first-packet arm (case rtw88_net) brings the lwIP core and its port glue;
# the case itself is in the component's lwIP list.  The stack is added here, the
# same shape rtl8xxxu.mk uses, because it is a third-party tree rather than part
# of the wireless component.
# A profile that selects both cases gets the stack from the netio list above:
# that list is this one plus the server and the case, and a source compiled
# into the same object path twice is a duplicate symbol at link time, not a
# second copy (2026-11-05, the AGG-02 arm is the first both-cases profile).
ifneq ($(filter rtw88_net,$(CASES)),)
ifeq ($(filter rtw88_netio,$(CASES)),)
RTW88_LWIP_SRCS := \
	$(THIRD_PARTY)/lwip/ports/sys_arch.c \
	$(THIRD_PARTY)/lwip/ports/platform_hooks.c \
	$(LWIP_STACK_SRCS)
RTW88_LWIP_CFLAGS := $(filter-out -Wextra,$(BASE_CFLAGS)) -std=gnu11 \
	-I$(RTW88_NPORT)/bridge
else
RTW88_LWIP_SRCS :=
endif
else
RTW88_LWIP_SRCS :=
endif

# The profile facts the cases cannot default: which AP the join arm associates
# with, and which address the netio arm binds to.  The netio arm is a static
# address by construction (the bench's server address is the profile's), so the
# static arm's three strings are required with it.
ifneq ($(filter rtw88_join,$(CASES)),)
ifeq ($(strip $(CONFIG_RTW88_AP_SSID)),)
$(error the rtw88_join case needs CONFIG_RTW88_AP_SSID in the profile's defconfig)
endif
endif
ifneq ($(filter rtw88_netio,$(CASES)),)
ifeq ($(strip $(CONFIG_RTW88_NET_ADDR_STATIC)),1)
else
$(error the rtw88_netio case needs CONFIG_RTW88_NET_ADDR_STATIC in the profile's defconfig)
endif
ifeq ($(strip $(CONFIG_RTW88_NET_IP)),)
$(error the rtw88_netio case needs CONFIG_RTW88_NET_IP in the profile's defconfig)
endif
endif

RTW88_ALL_SRCS := $(RTW88_NET80211_SRCS) $(RTW88_LKPI_SRCS) \
	$(RTW88_USB_PORT_SRCS) $(RTW88_DRV_SRCS) $(RTW88_FW_SRCS) \
	$(RTW88_CASE_SRCS) $(RTW88_NETIO_SRCS) $(RTW88_LWIP_SRCS)

# The symbols only the case reaches: with --gc-sections the walkers, the
# backend registration, the firmware registration and the bridge install have
# no other reference.
RTW88_LDFLAGS := \
	-Wl,-u,usb_board_init \
	-Wl,-u,lkpi_usb_init \
	-Wl,-u,lkpi_usb_cherryusb_init \
	-Wl,-u,lkpi_fw_rtw8821c_fw_register \
	-Wl,-u,wlan_seam_adapter_init

# The driver's widest receive transfer must fit the adapter's stage: every
# receive URB larger than the stage is refused for good.  The gate reads the
# driver's own number instead of trusting this comment.
RTW88_GATE_STAGE := $(shell sed -n 's/^#define[[:space:]]*RTW_USB_MAX_RECVBUF_SZ[[:space:]]*\([0-9][0-9]*\).*/\1/p' $(RTW88_DRV)/usb.h)

# Static object tables: the driver's delayed-work population is larger than the
# storage line's.  These are global (the CMSIS2 backend's tables are built from
# them), so they are appended to the global flag set like rtw89.mk does.

endif # rtw88_usb

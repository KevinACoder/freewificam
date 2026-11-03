# @file    rtw88_sdio.mk
# @brief   The RTL8821CS wireless world: the vendored rtw88 driver over the
#          linuxkpi SDIO engine and this board's MCI controller, the NetBSD
#          net80211 stack and its bridge, and the compiled-in firmware blob.
#
# Composition mirrors `rtw89.mk` (net80211 is the MLME, linuxkpi supplies the
# Linux face) with the bus swapped: SDIO over the ported Phytium MCI controller
# (`sdio_mci.mk`'s stack, one level up) instead of USB over CherryUSB.  The
# driver compiles from the pinned tree with the four Kconfig keys the kernel
# resolves for the 8821CS; its bytes are not edited.
#
# Included only when the case list carries `rtw88`.
#
# --- the one-port rule -------------------------------------------------------
# The imported SDIO stack exports its host glue under names that do not carry the
# port's identity (SDMMCHOST_Init, FDWMMCHOST_SDIOConfig, FDWMMCHOST_CardIntEnable
# exist in sdmmc_host_dwmmc.c, sdmmc_host_firefly_mci.c and sdmmc_host_dwcmshc.c
# alike), so one image may compile exactly one of them.  This world carries the
# MCI port and therefore must not be linked beside the 8189FTV image, which
# carries the dwmmc one.

ifneq ($(filter rtw88,$(CASES)),)

RTW88_DRV := $(ROOT)/wireless-drivers/rtw88
RTW88_NET := $(ROOT)/components/net80211
RTW88_NPORT := $(RTW88_NET)/ports
RTW88_LKPI := $(ROOT)/components/linuxkpi/rtos

# The image's own names for the bridge: the adapter name the driver line
# reports, and the firmware name the driver asks the registry for.
RTW88_IMAGE_NAMES :=

# The skb pool geometry the receive path needs: the driver allocates one buffer
# per received frame (rtw88/sdio.c:986) and its window can carry several frames,
# so the header pool has to be deeper than the default and the heap-header
# ceiling has to cover the ones the pool hands out on the heap.  The buffers are
# 16 KiB-class (the chip's rxff_size is 16384), larger than a pool data slot, so
# the payloads come from the heap and only the headers are pooled.
RTW88_SKB_DEFS :=

# The net80211 bridge is shared with the USB lines, and its port_* observation
# hooks read host-controller registers and ask the URB engine.  This line's
# transport is SDIO: there is no controller to read and no URB engine in the
# image, so the adapter compiles those four hooks out and the bridge's own
# null-checked dispatch reports the port as having no such state.
RTW88_SEAM_DEFS :=

RTW88_BSD_INCLUDES := -I$(ROOT) -I$(RTW88_NPORT)/compat \
	-I$(RTW88_NET)/compat/netbsd -I$(RTW88_NET) -I$(RTW88_NPORT)

RTW88_KPI_INCLUDES := -I$(ROOT) -I$(ROOT)/components/linuxkpi \
	-I$(RTW88_LKPI) -I$(RTW88_LKPI)/include -I$(RTW88_LKPI)/sdio \
	-I$(RTW88_LKPI)/skbuff -I$(RTW88_NET) -I$(RTW88_NPORT) \
	-I$(RTW88_NPORT)/bridge \
	-I$(THIRD_PARTY)/fsl_sdmmc/ports -I$(THIRD_PARTY)/fsl_sdmmc/sdio \
	-I$(PLATFORM)/board/firefly/drivers

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

# The chip and bus selection the kernel's Kconfig resolves for the 8821CS:
# CONFIG_RTW88_CORE + the chip + the SDIO bus + the module.  CONFIG_PM and
# CONFIG_RTW88_LEDS are deliberately absent - the kernel's Makefile gates wow.o
# on the first and led.o on the second, and with neither defined the driver's
# own headers supply empty stubs, so neither unit is in this source set.
# CONFIG_RTW88_DEBUGFS is absent for the same reason (debug.c would compile to
# an empty unit; the header's stub is what main.c calls).
RTW88_DRV_CONFIG :=

RTW88_DRV_CFLAGS := -std=c11 -O2 -Wall \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-march=armv8-a -mgeneral-regs-only -mstrict-align -mno-outline-atomics \
	-ffunction-sections -fdata-sections \
	$(RTW88_DRV_CONFIG) $(RTW88_SKB_DEFS) \
	  \
	-I$(ROOT) -I$(RTW88_LKPI) -I$(RTW88_LKPI)/include -I$(RTW88_DRV) \
	-I$(THIRD_PARTY)/ulog/src \
	-I$(CMSIS)/CMSIS/RTOS2/Include -Wno-unused-parameter \
	-Wno-address-of-packed-member \
	-include stdio.h -include string.h -include strings.h -include stdlib.h \
	

# The SDIO port units (the imported stack plus the seam's fsl adapter) need the
# stack's SDIO face selected and the board's controller driver on the include
# path; they are vendored C, so the platform warning set is relaxed for them the
# same way sdio_mci.mk relaxes it.
RTW88_SDIO_PORT_CFLAGS := $(filter-out -Wextra,$(BASE_CFLAGS)) -std=gnu11 \
	  \
	 \
	 \
	 \
	$(RTW88_SKB_DEFS) $(RTW88_SEAM_DEFS) \
	 \
	$(RTW88_KPI_INCLUDES)

# The cases read the port's own entry points (lkpi_sdio_fsl_install,
# sdmmc_port_mci_host) and the controller's state, so they compile with the same
# port selection the port units do: without CONFIG_FSL_SDMMC_ENABLE_SDIO the port
# header hides those declarations behind its SDIO key, the call sites degrade to
# implicit int declarations, and a returned pointer is truncated to 32 bits - a
# fault inside memcpy, far from the call that caused it.
RTW88_CASE_CFLAGS := $(RTW88_KPI_CFLAGS) \
	 \
	 \
	

RTW88_NET80211_SRCS := $(NET80211_SRCS)



# The SDIO transport: the imported stack's OSAL, card layer, common layer and
# SDIO card type, this board's MCI glue, the controller driver itself, and the
# seam adapter that maps the engine's seven operations onto them.
RTW88_SDIO_PORT_SRCS := \
	$(THIRD_PARTY)/fsl_sdmmc/ports/fsl_sdmmc_osa.c \
	$(THIRD_PARTY)/fsl_sdmmc/ports/sdmmc_card.c \
	$(THIRD_PARTY)/fsl_sdmmc/ports/sdmmc_host_firefly_mci.c \
	$(THIRD_PARTY)/fsl_sdmmc/ports/lkpi_sdio_fsl.c \
	$(THIRD_PARTY)/fsl_sdmmc/common/fsl_sdmmc_common.c \
	$(THIRD_PARTY)/fsl_sdmmc/sdio/fsl_sdio.c \
	$(PLATFORM)/board/firefly/drivers/mci.c

RTW88_LKPI_SRCS := $(LINUXKPI_SRCS) $(NET80211_KPI_SRCS)

# The 8821CS translation units: the twenty the chip selection resolves.  wow.c
# (CONFIG_PM), led.c (CONFIG_RTW88_LEDS) and debug.c (CONFIG_RTW88_DEBUGFS) are
# not among them, for the reason RTW88_DRV_CONFIG states.  The other chips' bus
# and chip units are not either - the linker's zero-undefined gate is the check.
RTW88_DRV_SRCS := $(RTW88_IMPL_SRCS)

RTW88_FW_SRCS := $(RTW88_BLOB_SRCS)

RTW88_CASE_SRCS := $(RTW88_APP_SRCS)

# The first-packet arm (case rtw88_net) brings the lwIP core and its port glue;
# the case itself is in the component's lwIP list.  The stack is added here, the
# same shape rtl8xxxu.mk uses, because it is a third-party tree rather than part
# of the wireless component.
ifneq ($(filter rtw88_net,$(CASES)),)
RTW88_LWIP_SRCS += \
	$(THIRD_PARTY)/lwip/ports/sys_arch.c \
	$(THIRD_PARTY)/lwip/ports/platform_hooks.c \
	$(LWIP_STACK_SRCS)
# Taken from BASE_CFLAGS *before* the Linux face is appended, so the lwIP units
# never compile against <net/mac80211.h>; the bridge's include root is all this
# world needs beyond the platform's own headers.
RTW88_LWIP_CFLAGS := $(filter-out -Wextra,$(BASE_CFLAGS)) -std=gnu11 \
	-I$(RTW88_NPORT)/bridge
else
RTW88_LWIP_SRCS :=
endif

# The two profile facts the two cases cannot default: which AP the join arm
# associates with, and which address the first-packet arm uses.  Both are named
# in the defconfig; an image that selects the case without them fails here, at
# build time, instead of on the board (docs/PLATFORM-FACTS.md).
ifneq ($(filter rtw88_join,$(CASES)),)
ifeq ($(strip $(CONFIG_RTW88_AP_SSID)),)
$(error the rtw88_join case needs CONFIG_RTW88_AP_SSID in the profile's defconfig)
endif
endif
ifneq ($(filter rtw88_net,$(CASES)),)
ifeq ($(words $(filter 1,$(CONFIG_RTW88_NET_ADDR_DHCP) $(CONFIG_RTW88_NET_ADDR_STATIC))),1)
else
$(error the rtw88_net case needs exactly one of CONFIG_RTW88_NET_ADDR_DHCP / CONFIG_RTW88_NET_ADDR_STATIC)
endif
endif

RTW88_ALL_SRCS := $(RTW88_NET80211_SRCS) $(RTW88_LKPI_SRCS) \
	$(RTW88_SDIO_PORT_SRCS) $(RTW88_DRV_SRCS) $(RTW88_FW_SRCS) \
	$(RTW88_CASE_SRCS) $(RTW88_LWIP_SRCS)

# The symbols only the case reaches: with --gc-sections the firmware
# registration, the backend installation and the bridge install have no other
# reference, and the SDIO driver registry's section is walked by address so its
# only entry would otherwise be dropped.
RTW88_LDFLAGS := \
	-Wl,-u,lkpi_fw_rtw8821c_fw_register \
	-Wl,-u,lkpi_sdio_fsl_install \
	-Wl,-u,wlan_seam_adapter_init

# Static object tables: the driver's delayed-work population is larger than the
# storage line's.  These are global (the CMSIS2 backend's tables are built from
# them), so they are appended to the global flag set like rtw89.mk does.

endif # rtw88

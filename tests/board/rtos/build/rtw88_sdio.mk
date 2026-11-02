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
RTW88_IMAGE_NAMES := -DWLAN_BRIDGE_ADAPTER_NAME='"rtw88"' \
	-DWLAN_BRIDGE_FIRMWARE_NAME='"rtw88/rtw8821c_fw.bin"'

# The skb pool geometry the receive path needs: the driver allocates one buffer
# per received frame (rtw88/sdio.c:986) and its window can carry several frames,
# so the header pool has to be deeper than the default and the heap-header
# ceiling has to cover the ones the pool hands out on the heap.  The buffers are
# 16 KiB-class (the chip's rxff_size is 16384), larger than a pool data slot, so
# the payloads come from the heap and only the headers are pooled.
RTW88_SKB_DEFS := -DCONFIG_LKPI_SKB_POOL_NUM=128 \
	-DCONFIG_LKPI_SKB_DATA_POOL_NUM=64 \
	-DCONFIG_LKPI_SKB_HEAP_HDR_MAX=64

# The net80211 bridge is shared with the USB lines, and its port_* observation
# hooks read host-controller registers and ask the URB engine.  This line's
# transport is SDIO: there is no controller to read and no URB engine in the
# image, so the adapter compiles those four hooks out and the bridge's own
# null-checked dispatch reports the port as having no such state.
RTW88_SEAM_DEFS := -DWLAN_SEAM_NO_USB_OBS=1

RTW88_BSD_INCLUDES := -I$(ROOT) -I$(RTW88_NPORT)/compat \
	-I$(RTW88_NET)/compat/netbsd -I$(RTW88_NET) -I$(RTW88_NPORT)

RTW88_KPI_INCLUDES := -I$(ROOT) -I$(ROOT)/components/linuxkpi \
	-I$(RTW88_LKPI) -I$(RTW88_LKPI)/include -I$(RTW88_LKPI)/sdio \
	-I$(RTW88_LKPI)/skbuff -I$(RTW88_NET) -I$(RTW88_NPORT) \
	-I$(RTW88_NPORT)/bridge \
	-I$(THIRD_PARTY)/fsl_sdmmc/ports -I$(THIRD_PARTY)/fsl_sdmmc/sdio \
	-I$(PLATFORM)/board/firefly/drivers

RTW88_BSD_CFLAGS := $(filter-out -Wextra,$(BASE_CFLAGS)) -std=gnu11 \
	-D_KERNEL -DWLAN_PORT_TARGET -Wno-unused-function -Wno-pointer-sign \
	$(RTW88_IMAGE_NAMES) $(RTW88_SEAM_DEFS) \
	-include $(RTW88_NPORT)/compat/prelude.h \
	$(RTW88_BSD_INCLUDES) $(RTW88_KPI_INCLUDES)

RTW88_KPI_CFLAGS := $(filter-out -Wextra,$(BASE_CFLAGS)) -std=gnu11 \
	-DWLAN_PORT_TARGET -DLKPI_ABS_PLATFORM_PROVIDED \
	$(RTW88_SKB_DEFS) $(RTW88_SEAM_DEFS) \
	-DCMSIS_device_header='"cmsis_os2.h"' \
	$(RTW88_KPI_INCLUDES)

# The chip and bus selection the kernel's Kconfig resolves for the 8821CS:
# CONFIG_RTW88_CORE + the chip + the SDIO bus + the module.  CONFIG_PM and
# CONFIG_RTW88_LEDS are deliberately absent - the kernel's Makefile gates wow.o
# on the first and led.o on the second, and with neither defined the driver's
# own headers supply empty stubs, so neither unit is in this source set.
# CONFIG_RTW88_DEBUGFS is absent for the same reason (debug.c would compile to
# an empty unit; the header's stub is what main.c calls).
RTW88_DRV_CONFIG := -DCONFIG_RTW88_CORE -DCONFIG_RTW88_8821C \
	-DCONFIG_RTW88_SDIO -DCONFIG_RTW88_8821CS

RTW88_DRV_CFLAGS := -std=c11 -O2 -Wall \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-march=armv8-a -mgeneral-regs-only -mstrict-align -mno-outline-atomics \
	-ffunction-sections -fdata-sections \
	$(RTW88_DRV_CONFIG) $(RTW88_SKB_DEFS) \
	-DWLAN_PORT_TARGET -DLKPI_ABS_PLATFORM_PROVIDED \
	-I$(ROOT) -I$(RTW88_LKPI) -I$(RTW88_LKPI)/include -I$(RTW88_DRV) \
	-I$(THIRD_PARTY)/ulog/src \
	-I$(CMSIS)/CMSIS/RTOS2/Include -Wno-unused-parameter \
	-Wno-address-of-packed-member \
	-include stdio.h -include string.h -include strings.h -include stdlib.h \
	-D_DEFAULT_SOURCE=1

# The SDIO port units (the imported stack plus the seam's fsl adapter) need the
# stack's SDIO face selected and the board's controller driver on the include
# path; they are vendored C, so the platform warning set is relaxed for them the
# same way sdio_mci.mk relaxes it.
RTW88_SDIO_PORT_CFLAGS := $(filter-out -Wextra,$(BASE_CFLAGS)) -std=gnu11 \
	-DWLAN_PORT_TARGET -DLKPI_ABS_PLATFORM_PROVIDED \
	-DCONFIG_FSL_SDMMC_ENABLE_SDIO=1 \
	-DSDMMC_PORT_SDIO_HOST_TYPE=kSDMMCHOST_TYPE_FIREFLY_MCI \
	-DSDMMC_PORT_SDIO_HOST_ID=1 \
	$(RTW88_SKB_DEFS) $(RTW88_SEAM_DEFS) \
	-DCMSIS_device_header='"cmsis_os2.h"' \
	$(RTW88_KPI_INCLUDES)

# The cases read the port's own entry points (lkpi_sdio_fsl_install,
# sdmmc_port_mci_host) and the controller's state, so they compile with the same
# port selection the port units do: without CONFIG_FSL_SDMMC_ENABLE_SDIO the port
# header hides those declarations behind its SDIO key, the call sites degrade to
# implicit int declarations, and a returned pointer is truncated to 32 bits - a
# fault inside memcpy, far from the call that caused it.
RTW88_CASE_CFLAGS := $(RTW88_KPI_CFLAGS) \
	-DCONFIG_FSL_SDMMC_ENABLE_SDIO=1 \
	-DSDMMC_PORT_SDIO_HOST_TYPE=kSDMMCHOST_TYPE_FIREFLY_MCI \
	-DSDMMC_PORT_SDIO_HOST_ID=1

RTW88_NET80211_SRCS := \
	$(RTW88_NET)/net80211/ieee80211.c \
	$(RTW88_NET)/net80211/ieee80211_proto.c \
	$(RTW88_NET)/net80211/ieee80211_node.c \
	$(RTW88_NET)/net80211/ieee80211_output.c \
	$(RTW88_NET)/net80211/ieee80211_input.c \
	$(RTW88_NET)/net80211/ieee80211_amrr.c \
	$(RTW88_NET)/net80211/ieee80211_netbsd.c \
	$(RTW88_NET)/net80211/ieee80211_crypto.c \
	$(RTW88_NET)/net80211/ieee80211_crypto_none.c \
	$(RTW88_NET)/net80211/ieee80211_crypto_ccmp.c \
	$(RTW88_NET)/crypto/aes/aes_bear.c \
	$(RTW88_NET)/crypto/aes/aes_ct.c \
	$(RTW88_NET)/crypto/aes/aes_ct_enc.c \
	$(RTW88_NET)/crypto/aes/aes_ct_dec.c \
	$(RTW88_NET)/crypto/aes/aes_ccm.c \
	$(RTW88_NET)/crypto/aes/aes_ccm_mbuf.c \
	$(RTW88_NET)/port/aes_impl_compat.c

RTW88_NPORT_SRCS := \
	$(RTW88_NPORT)/osal/cmsis2/osal_memory_target.c \
	$(RTW88_NPORT)/osal/cmsis2/osal_cmsis2.c \
	$(RTW88_NPORT)/net/mbuf.c \
	$(RTW88_NPORT)/net/ifnet.c

RTW88_BRIDGE_SRCS := $(RTW88_NPORT)/bridge/wlan_bridge.c

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

RTW88_LKPI_SRCS := \
	$(RTW88_LKPI)/linuxkpi_core.c \
	$(RTW88_LKPI)/linuxkpi_compat.c \
	$(RTW88_LKPI)/linuxkpi_platform_target.c \
	$(RTW88_LKPI)/linuxkpi_80211.c \
	$(RTW88_LKPI)/linuxkpi_80211_helpers.c \
	$(RTW88_LKPI)/linuxkpi_wiphy_work.c \
	$(RTW88_LKPI)/linuxkpi_txq.c \
	$(RTW88_LKPI)/linuxkpi_netdev.c \
	$(RTW88_LKPI)/linuxkpi_cfg80211.c \
	$(RTW88_LKPI)/linuxkpi_osdep.c \
	$(RTW88_LKPI)/lkpi_tcp_trace.c \
	$(RTW88_LKPI)/fw_registry_rtos.c \
	$(RTW88_LKPI)/skbuff/skbuff_pool.c \
	$(RTW88_LKPI)/sdio/linuxkpi_sdio.c \
	$(RTW88_LKPI)/sdio/lkpi_sdio_be.c \
	$(RTW88_NPORT)/bridge/wlan_seam_adapter.c

# The 8821CS translation units: the twenty the chip selection resolves.  wow.c
# (CONFIG_PM), led.c (CONFIG_RTW88_LEDS) and debug.c (CONFIG_RTW88_DEBUGFS) are
# not among them, for the reason RTW88_DRV_CONFIG states.  The other chips' bus
# and chip units are not either - the linker's zero-undefined gate is the check.
RTW88_DRV_SRCS := \
	$(RTW88_DRV)/main.c \
	$(RTW88_DRV)/mac80211.c \
	$(RTW88_DRV)/util.c \
	$(RTW88_DRV)/tx.c \
	$(RTW88_DRV)/rx.c \
	$(RTW88_DRV)/mac.c \
	$(RTW88_DRV)/phy.c \
	$(RTW88_DRV)/coex.c \
	$(RTW88_DRV)/efuse.c \
	$(RTW88_DRV)/fw.c \
	$(RTW88_DRV)/ps.c \
	$(RTW88_DRV)/sec.c \
	$(RTW88_DRV)/bf.c \
	$(RTW88_DRV)/sar.c \
	$(RTW88_DRV)/regd.c \
	$(RTW88_DRV)/rtw8821c.c \
	$(RTW88_DRV)/rtw8821c_table.c \
	$(RTW88_DRV)/sdio.c \
	$(RTW88_DRV)/rtw8821cs.c

RTW88_FW_SRCS := $(ROOT)/components/linuxkpi/fw/rtw8821c_fw_blob.c

RTW88_CASE_SRCS := \
	$(LINE)/app/cases/rtw88.c \
	$(LINE)/app/cases/rtw88_scan.c

RTW88_ALL_SRCS := $(RTW88_NET80211_SRCS) $(RTW88_NPORT_SRCS) \
	$(RTW88_BRIDGE_SRCS) $(RTW88_LKPI_SRCS) $(RTW88_SDIO_PORT_SRCS) \
	$(RTW88_DRV_SRCS) $(RTW88_FW_SRCS) $(RTW88_CASE_SRCS)

# The symbols only the case reaches: with --gc-sections the firmware
# registration, the backend installation and the bridge install have no other
# reference, and the SDIO driver registry's section is walked by address so its
# only entry would otherwise be dropped.
RTW88_LDFLAGS := \
	-Wl,-u,lkpi_fw_rtw8821c_register \
	-Wl,-u,lkpi_sdio_fsl_install \
	-Wl,-u,wlan_seam_adapter_init

# Static object tables: the driver's delayed-work population is larger than the
# storage line's.  These are global (the CMSIS2 backend's tables are built from
# them), so they are appended to the global flag set like rtw89.mk does.
BASE_CFLAGS += -DCMSIS2_MAX_TIMERS=32 -DCMSIS2_MAX_THREADS=16
CFLAGS += -DCMSIS2_MAX_TIMERS=32 -DCMSIS2_MAX_THREADS=16
ASM_CFLAGS += -DCMSIS2_MAX_TIMERS=32 -DCMSIS2_MAX_THREADS=16

endif # rtw88

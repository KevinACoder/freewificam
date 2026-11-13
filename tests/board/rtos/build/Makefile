# @file Makefile
# @brief The RTOS bin platform's build entry: one image per profile, plus the
# per-component archives `libs` produces.
#
# Products:
# build/rtos-freertos.bin flat raw image, loaded as rtos.bin at 0xa000000
# and entered with `go 0xa000000`
# build/lib/<profile>/ one archive per component group (libs target)
#
# Targets:
# image one image for BACKEND=freertos (the only backend in this line)
# gates the image's link-time gates (run by image)
# libs archive the image's object groups
# clean
#
# Knobs:
# CASES="tick cmsis2 net" which case groups the image carries
# OPT=-O2 optimisation level (normally from Kconfig)
# BUILD_TAG=<string> override the banner's build tag
#
# This file is the previous tree's platform Makefile, trimmed to the worlds this
# line carries: FreeRTOS + RK3568, the wired network, PCIe/NVMe, eMMC and the
# USB mass-storage port. The wireless worlds (net80211, iwlwifi, rtw88/89,
# rtl8xxxu, aic8800, uvc) and the second/third OS backends are not imported;
# their hooks are kept in the component/module shape so they land where the old
# line put them (see docs/BUILD.md).

ROOT := $(abspath $(CURDIR)/../../../..)
LINE := $(abspath $(CURDIR)/..)
PLATFORM := $(LINE)/platform
COMPONENTS := $(ROOT)/components
THIRD_PARTY := $(ROOT)/third-party

# The selected defconfig. config.mk is a selector only; the values live in the
# fragment generated from configs/<name>.defconfig.
include $(LINE)/config.mk
PROFILE ?= $(CONFIG)
CONFIG_FILE := $(ROOT)/build/kconfig/$(PROFILE)/freewificam_config
# `make clean` must not need the generated config (or Python). Every other
# goal includes it; make remakes the included file through the rule below and
# restarts, so the variables are always available where they are used.
ifneq ($(filter clean,$(MAKECMDGOALS)),)
else
-include $(CONFIG_FILE)
endif

# A direct make invocation regenerates a missing config. make.rtos regenerates
# before it calls make; this guard covers a hand-run make.
$(CONFIG_FILE):
	python3 $(ROOT)/tools/build/kconfig-gen.py --profile $(PROFILE)
image libs: $(CONFIG_FILE)

BACKEND := $(subst ",,$(CONFIG_BACKEND_NAME))
BOARD := $(subst ",,$(CONFIG_BOARD_NAME))
CASES := $(subst ",,$(CONFIG_CASES))
OPT := $(subst ",,$(CONFIG_OPT_FLAGS))
SWITCH_PROFILE := $(subst ",,$(CONFIG_PROFILE_NAME))
HEAP_SIZE := $(CONFIG_HEAP_SIZE)M
# A literal comma for function arguments whose value contains one (link flags).
comma := ,

# The cross toolchain: put an `aarch64-none-elf-` toolchain on PATH, or name
# its prefix directory in AARCH64_CROSS_PATH, or name the whole prefix in
# CROSS_COMPILE. No machine layout is assumed here.
ifneq ($(strip $(AARCH64_CROSS_PATH)),)
CROSS_COMPILE ?= $(AARCH64_CROSS_PATH)/bin/aarch64-none-elf-
else
CROSS_COMPILE ?= aarch64-none-elf-
endif
CC := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
LD := $(CROSS_COMPILE)ld
NM := $(CROSS_COMPILE)nm
AR := $(CROSS_COMPILE)ar
SIZE := $(CROSS_COMPILE)size
HOSTCC ?= cc
# Build-time helpers (the firmware generator, the library manifest).  The
# nested make does not inherit rtos.mk's variable, so it is defined here too.
PYTHON ?= python3

KERNEL := $(ROOT)/third-party/FreeRTOS-Kernel
ADAPTER := $(ROOT)/third-party/CMSIS-FreeRTOS
CMSIS := $(ROOT)/third-party/CMSIS_6
# The stack core is selectable so a version question can be answered by a
# one-line rebuild instead of an argument: LWIP_DIR names the tree.
LWIP_DIR ?= $(ROOT)/third-party/lwip
LWIP := $(LWIP_DIR)
PATCHES := $(notdir $(wildcard $(ROOT)/third-party/lwip/patches/*.patch))
ADAPTER_PATCHES := $(notdir $(wildcard $(ROOT)/third-party/CMSIS-FreeRTOS/patches/*.patch))

BUILD := $(CURDIR)/build
BUILD_TAG ?= $(shell git -C $(ROOT) describe --always --dirty 2>/dev/null || echo unknown)

ifneq ($(BACKEND),)

# --- board selection ---------------------------------------------------------
# One board per image. The board choice is a Kconfig symbol; the source set,
# the include flavours, the link window and the entry gate follow from it.
ifeq ($(BOARD),rk3568-itx)
BOARD_INCLUDES := -I$(PLATFORM)/board/rk3568-itx -I$(PLATFORM)/board/rk3568-itx/drivers
BOARD_LINKSCRIPT := $(PLATFORM)/arch/aarch64/baremetal.ld
BOARD_LOAD_BASE := 0xa000000
else
$(error unknown BOARD '$(BOARD)': this line carries rk3568-itx only)
endif

# --- backend selection -------------------------------------------------------
# FreeRTOS is the only backend carried here. The Kconfig tree still owns the
# selection so a second backend lands as a new branch, not as a rewrite.
ifneq ($(BACKEND),freertos)
$(error unknown BACKEND '$(BACKEND)': this line carries freertos only)
endif
KERNEL_NAME := FreeRTOS-Kernel V11.3.1
BACKEND_INCLUDES := -I$(PLATFORM)/os/freertos/board/rk3568-itx/cmsis2 -I$(PLATFORM)/os/freertos/board/rk3568-itx -I$(KERNEL)/include -I$(KERNEL)/portable/GCC/ARM_AARCH64_SRE -I$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Include
BACKEND_LDFLAGS := -Wl,-u,osCriticalEnter -Wl,-u,osCriticalExit
BACKEND_VECTOR := _freertos_vector_table
BACKEND_SYMBOLS := FreeRTOS_IRQ_Handler FreeRTOS_Tick_Handler xPortStartScheduler
BACKEND_FIRST_SRCS := $(PLATFORM)/os/freertos/board/rk3568-itx/freertos_board_glue.c $(PLATFORM)/os/freertos/board/rk3568-itx/freertos_board_vectors.S $(PLATFORM)/os/freertos/board/rk3568-itx/cmsis2_ext_board.c $(PLATFORM)/os/freertos/board/rk3568-itx/cmsis2_memory_board.c
BACKEND_VENDOR_SRCS := $(KERNEL)/tasks.c $(KERNEL)/list.c $(KERNEL)/queue.c $(KERNEL)/event_groups.c $(KERNEL)/timers.c $(KERNEL)/stream_buffer.c $(KERNEL)/portable/MemMang/heap_4.c $(KERNEL)/portable/GCC/ARM_AARCH64_SRE/port.c $(KERNEL)/portable/GCC/ARM_AARCH64_SRE/portASM.S $(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Source/cmsis_os2.c

INCLUDES := -I$(PLATFORM) -I$(LINE) -I$(LINE)/app -I$(PLATFORM)/os/cmsis2 -I$(PLATFORM)/os/cmsis2/board-compat -I$(PLATFORM)/arch/aarch64 $(BOARD_INCLUDES) -I$(PLATFORM)/os/baremetal -I$(THIRD_PARTY)/ulog/src -I$(THIRD_PARTY)/tlsf -I$(THIRD_PARTY)/lwip/ports -I$(THIRD_PARTY)/lwip/ports/include -I$(ROOT)/components/liblinux80211/include -I$(ROOT)/build/kconfig/$(PROFILE) -I$(ROOT)/build/kconfig/$(PROFILE)/include/generated -I$(LWIP)/src/include -I$(THIRD_PARTY)/fsl_sdmmc/ports -I$(THIRD_PARTY)/fsl_sdmmc/ports/shadow -I$(THIRD_PARTY)/fsl_sdmmc/common -I$(THIRD_PARTY)/fsl_sdmmc/mmc -I$(THIRD_PARTY)/fsl_sdmmc/osa -I$(CMSIS)/CMSIS/RTOS2/Include -I$(CMSIS)/CMSIS/Driver/Include -I$(CMSIS)/CMSIS/Core/Include/a-profile $(BACKEND_INCLUDES)

# The PCIe/NVMe cases include the board driver headers directly (they are not
# reachable through the generic drivers/ include root).
ifneq ($(filter pcie,$(CASES)),)
ifeq ($(BOARD),rk3568-itx)
INCLUDES += -I$(PLATFORM)/board/rk3568-itx/drivers/pcie -I$(PLATFORM)/board/rk3568-itx/drivers/nvme
endif
endif

# The kernel's compiler-semantics contract, in one place. Every imported tree
# in this build was written under Linux's KBUILD_CFLAGS, which switches these
# optimisations off for the whole tree; a tree compiled without them is a
# different language. So the list lives once, every C flags set below derives
# from it, and tools/build/check-build-contract.sh fails the image when a new
# set stops deriving from it or pins its own -O level.
KCONTRACT_CFLAGS := -fno-strict-aliasing -fno-strict-overflow -fno-delete-null-pointer-checks
BASE_CFLAGS := $(OPT) -g$(CONFIG_DEBUG_LEVEL_VALUE) -std=c11 -Wall -Wextra -ffreestanding -fno-builtin -fno-stack-protector $(KCONTRACT_CFLAGS) -march=armv8-a -mgeneral-regs-only -mstrict-align -mno-outline-atomics $(PROFILE_CFLAGS) -ffunction-sections -fdata-sections -include $(PLATFORM)/build_defs.h $(INCLUDES)

CFLAGS := $(BASE_CFLAGS) -include stdio.h -include string.h -include strings.h -include stdlib.h
ASM_CFLAGS := $(OPT) -g$(CONFIG_DEBUG_LEVEL_VALUE) -march=armv8-a -mgeneral-regs-only -mstrict-align -include $(PLATFORM)/build_defs.h $(INCLUDES)
VENDORED_CFLAGS := $(filter-out -Wextra,$(CFLAGS))
PRINTF_CFLAGS :=

# The lwIP core/protocol set: one list, because two images carry the stack (the
# GMAC net case and, later, the wireless egress). Only the port glue and the
# presentation layer differ per image.
LWIP_STACK_SRCS := $(LWIP)/src/core/init.c $(LWIP)/src/core/def.c $(LWIP)/src/core/dns.c \
	$(LWIP)/src/core/inet_chksum.c $(LWIP)/src/core/ip.c \
	$(LWIP)/src/core/mem.c $(LWIP)/src/core/memp.c \
	$(LWIP)/src/core/netif.c $(LWIP)/src/core/pbuf.c \
	$(LWIP)/src/core/raw.c $(LWIP)/src/core/stats.c $(LWIP)/src/core/sys.c \
	$(LWIP)/src/core/altcp.c $(LWIP)/src/core/altcp_alloc.c \
	$(LWIP)/src/core/altcp_tcp.c $(LWIP)/src/core/tcp.c \
	$(LWIP)/src/core/tcp_in.c $(LWIP)/src/core/tcp_out.c \
	$(LWIP)/src/core/timeouts.c $(LWIP)/src/core/udp.c \
	$(LWIP)/src/core/ipv4/acd.c $(LWIP)/src/core/ipv4/autoip.c \
	$(LWIP)/src/core/ipv4/dhcp.c $(LWIP)/src/core/ipv4/etharp.c \
	$(LWIP)/src/core/ipv4/icmp.c $(LWIP)/src/core/ipv4/igmp.c \
	$(LWIP)/src/core/ipv4/ip4_frag.c $(LWIP)/src/core/ipv4/ip4.c \
	$(LWIP)/src/core/ipv4/ip4_addr.c \
	$(LWIP)/src/api/api_lib.c $(LWIP)/src/api/api_msg.c \
	$(LWIP)/src/api/err.c $(LWIP)/src/api/if_api.c \
	$(LWIP)/src/api/netbuf.c $(LWIP)/src/api/netdb.c \
	$(LWIP)/src/api/netifapi.c $(LWIP)/src/api/sockets.c \
	$(LWIP)/src/api/tcpip.c \
	$(LWIP)/src/netif/ethernet.c \
	$(LWIP)/src/apps/lwiperf/lwiperf.c

# ---------------------------------------------------------------------------
# Component source lists: each component owns one sources.mk; the helper in
# tools/build/sources.mk turns its kbuild-style obj- lines into the per-flag-
# world lists the fragments below consume.
include $(ROOT)/tools/build/sources.mk

$(eval $(call component_sources,$(PLATFORM)/arch,base,ARCH_BASE))
$(eval $(call component_sources,$(PLATFORM)/arch,pcie,ARCH_PCIE))
$(eval $(call component_sources,$(PLATFORM)/board,base,BOARD_BASE))
$(eval $(call component_sources,$(PLATFORM)/board,pcie,BOARD_PCIE))
$(eval $(call component_sources,$(PLATFORM)/board,nvme,BOARD_NVME))
$(eval $(call component_sources,$(PLATFORM)/os,baremetal,OS_BASE))

# liblinux80211: the component owns its sources; this build only says which
# world each group is compiled in.
ifeq ($(CONFIG_LIBLINUX80211),1)
$(eval $(call component_sources,$(ROOT)/components/liblinux80211,kpi,LIBLINUX80211_KPI))
$(eval $(call component_sources,$(ROOT)/components/liblinux80211,bus-pci,LIBLINUX80211_BUS_PCI))
$(eval $(call component_sources,$(ROOT)/components/liblinux80211,bus-sdio,LIBLINUX80211_BUS_SDIO))
$(eval $(call component_sources,$(ROOT)/components/liblinux80211,bus-usb,LIBLINUX80211_BUS_USB))
$(eval $(call component_sources,$(ROOT)/components/liblinux80211,port-pci,LIBLINUX80211_PCIE))
# The NAME is the list family, not the file family: `port-obj-` publishes
# <NAME>_PORT_SRCS, so the name must be LIBLINUX80211_USB (whose _PORT_SRCS the
# kpi world links) - naming it ..._USB_PORT would silently publish an empty list.
$(eval $(call component_sources,$(ROOT)/components/liblinux80211,port-usb,LIBLINUX80211_USB))
$(eval $(call component_sources,$(ROOT)/components/liblinux80211,stack,LIBLINUX80211_STACK))
$(eval $(call component_sources,$(ROOT)/components/liblinux80211,hostap,LIBLINUX80211_HOSTAP))
endif

# The wired-network cases each bring the lwIP port face and the stack core; the
# per-case group below adds that NIC's own netif bridge and driver. A boot may
# carry more than one (they share one stack bring-up).
LWIP_CASES := net net_xmac net_mdk_netio net_loop net_pps
ifneq ($(filter $(LWIP_CASES),$(CASES)),)
NET_APP_SRCS := \
	$(THIRD_PARTY)/lwip/ports/sys_arch.c \
	$(THIRD_PARTY)/lwip/ports/platform_hooks.c \
	$(LINE)/app/cases/net_stack.c
NET_STACK_SRCS := $(LWIP_STACK_SRCS)
else
NET_APP_SRCS :=
NET_STACK_SRCS :=
endif

# The wireless egress (C6): the same lwIP core, with the lkpinetif halves in
# place of a wired NIC bridge.  It may coexist with a wired case (both share
# the one tcpip_init); the two blocks add the common port glue twice, so the
# lists are deduplicated after both have spoken.
ifeq ($(CONFIG_LIBLINUX80211_NET_LWIP),1)
NET_APP_SRCS += \
	$(THIRD_PARTY)/lwip/ports/sys_arch.c \
	$(THIRD_PARTY)/lwip/ports/platform_hooks.c \
	$(THIRD_PARTY)/lwip/ports/lkpinetif_lwip.c \
	$(THIRD_PARTY)/lwip/ports/lkpinetif_case.c
NET_STACK_SRCS += $(LWIP_STACK_SRCS)
# The netdev half is the component's world, not lwIP's: it sees the Linux face.
WL_LWIP_KPI_SRCS := $(THIRD_PARTY)/lwip/ports/lkpinetif.c
endif
NET_APP_SRCS := $(sort $(NET_APP_SRCS))
NET_STACK_SRCS := $(sort $(NET_STACK_SRCS))

# The per-NIC groups: the rk3568 wired case keeps the eqos line.
EQOS_SRCS :=
ifneq ($(filter net net_mdk_netio net_pps,$(CASES)),)
NET_APP_SRCS += $(THIRD_PARTY)/lwip/ports/ethnetif.c
EQOS_SRCS += \
	$(PLATFORM)/board/rk3568-itx/drivers/dwc_eqos.c \
	$(PLATFORM)/board/rk3568-itx/drivers/eqos_rk3568.c \
	$(PLATFORM)/board/rk3568-itx/drivers/rtl8211f.c
endif

# The eMMC case: the imported fsl_sdmmc stack on the board's SDHCI-compatible
# controller. The stack's card type is a compile-time selection; the
# case-derived CONFIG_FSL_SDMMC_ENABLE_MMC lives in platform/build_defs.h.
ifneq ($(filter emmc,$(CASES)),)
MMC_PORT_SRCS := \
	$(THIRD_PARTY)/fsl_sdmmc/ports/fsl_sdmmc_osa.c \
	$(THIRD_PARTY)/fsl_sdmmc/ports/sdmmc_mmc_card.c \
	$(THIRD_PARTY)/fsl_sdmmc/ports/sdmmc_host_dwcmshc.c \
	$(PLATFORM)/board/rk3568-itx/drivers/dwc_mshc.c
MMC_VENDOR_SRCS := \
	$(THIRD_PARTY)/fsl_sdmmc/common/fsl_sdmmc_common.c \
	$(THIRD_PARTY)/fsl_sdmmc/mmc/fsl_mmc.c
else
MMC_PORT_SRCS :=
MMC_VENDOR_SRCS :=
endif

# The USB host domain (empty unless a USB consumer is selected): the imported
# CherryUSB stack, the board's USB power/PHY/reset layer, the CMSIS-RTOS2 OSAL,
# the first-party glue and - for storage images only - the mass-storage class
# with its test case.  The rtw89 cases are consumers too: their linuxkpi USB
# engine rides the same controllers, while the engine and its adapter belong to
# the liblinux80211 world (kpi.mk), not to this file.
USB_CONSUMER_CASES := usb_xhci usb_ehci rtw89_c3 rtw89_c4 rtw89_c5
ifneq ($(filter $(USB_CONSUMER_CASES),$(CASES)),)
include $(CURDIR)/usb.mk
endif

# The liblinux80211 world (empty unless the component is selected).
ifeq ($(CONFIG_LIBLINUX80211),1)
include $(CURDIR)/kpi.mk
KPI_ALL_SRCS += $(WL_LWIP_KPI_SRCS)
endif

# S0: the imported stack, compiled one translation unit at a time against the
# component's Linux face - no link, no object group, no image.  The flags are
# the kpi world's (same language, same face) plus the stack's own include roots
# and the three diagnostics that turn a missing API into a first-class error
# instead of an implicit-int warning.  The measurement lives in
# tools/build/s0-compile.sh; this file only owns the flags.
ifeq ($(CONFIG_LIBLINUX80211),1)
S0_CFLAGS := $(KPI_CFLAGS) \
	-I$(LIB8)/stack/mac80211 -I$(LIB8)/stack/wireless \
	-include $(LIB8)/include/linux80211/stack_config.h \
	-Werror=implicit-function-declaration -Werror=incompatible-pointer-types \
	-Werror=int-conversion -Werror=return-type
S0_OUT ?= $(abspath $(ROOT)/../runs/S0)
S0_RUN ?= 1
S0_DIR := $(S0_OUT)/run-$(S0_RUN)
S0_TU_LIST := $(BUILD)/s0-tus.txt

s0-compile: $(CONFIG_FILE)
	@mkdir -p $(S0_DIR)
	@printf '%s\n' $(LIBLINUX80211_STACK_SRCS) > $(S0_TU_LIST)
	@sh $(ROOT)/tools/build/s0-compile.sh \
		$(S0_DIR) $(CC) '$(S0_CFLAGS)' $(S0_TU_LIST)
	@cp $(S0_DIR)/summary.tsv $(S0_OUT)/summary.tsv
	@printf 's0-compile: run %s -> %s\n' '$(S0_RUN)' '$(S0_DIR)'
endif

# S1b: the vendored iwlwifi driver (AC 7260 / mvm, 72 TUs) compiled one
# translation unit at a time against the same face, plus the driver's own
# include root and its feature face.  Same measurement contract as S0: a
# failed TU is data (tools/build/s0-compile.sh does not stop), the product is
# runs/S1/driver/run-<n>/summary.tsv.  The source list comes from the driver's
# own sources.mk (tools/build/import-driver.py re-derives it from the upstream
# Makefiles), so the 72 is not a hand-count.
ifeq ($(CONFIG_LIBLINUX80211),1)
IWL_DRV := $(ROOT)/wireless-drivers/iwlwifi
$(eval $(call component_sources,$(IWL_DRV),drv,IWLWIFI_IMPL))

# The second vendored driver: the 8189FTV vendor stack (rtl8189fs, SDIO).  Its
# file set and feature face are stated here rather than in wl80211.mk because
# the S1b measurement below consumes both before the wireless world is
# included.  The SDIO group is the board's host path; it compiles in the kpi
# world (platform + Linux face), the same split the component's own port group
# uses.
RTL8189_DIR := $(ROOT)/wireless-drivers/rtl8189fs
$(eval $(call component_sources,$(RTL8189_DIR),drv,RTL8189FS_IMPL))
$(eval $(call component_sources,$(RTL8189_DIR),sdio,RTL8189FS))
$(eval $(call component_sources,$(RTL8189_DIR),port,RTL8189FS_OBS))

# The vendor tree's include roots and its warning set.  It is kernel code: the
# same relaxation the iwlwifi world states, plus the vendor tree's own habits
# (packed-member addresses, maybe-uninitialized, switch fallthrough).
RTL8189FS_CFLAGS := $(KPI_CFLAGS) \
	-I$(RTL8189_DIR)/src -I$(RTL8189_DIR)/src/include \
	-I$(RTL8189_DIR)/src/platform -I$(RTL8189_DIR)/src/hal/phydm \
	-I$(RTL8189_DIR)/src/hal/btc \
	-include $(RTL8189_DIR)/driver_config.h \
	-Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
	-Wno-address-of-packed-member -Wno-type-limits -Wno-unused-but-set-variable \
	-Wno-override-init -Wno-maybe-uninitialized -Wno-array-bounds \
	-Wno-pointer-sign -Wno-unused-variable -Wno-unused-function

# The SDIO port group is the platform's world plus the Linux face: the board's
# host driver and the imported card service see the platform's C library, and
# only the seam adapter (lkpi_sdio_fsl.c) also includes the component's headers.
# This is the split the previous line compiled the same files under
# (wlan.mk, WLAN_SDIO_CFLAGS); compiling them in the kpi world instead puts the
# Linux face ahead of the platform's forced stdio/stdlib includes and the two
# headers' spelling of the same attribute macros collide.
SDIO_PORT_SRCS := $(RTL8189FS_SDIO_SRCS)
RTL8189FS_SDIO_CFLAGS := $(filter-out -Wextra,$(CFLAGS)) -std=gnu11 \
	-I$(LIB8)/include -I$(LIB8)/include/linux80211 -I$(LIB8)/bus -I$(LIB8) \
	-I$(THIRD_PARTY)/fsl_sdmmc/sdio \
	-Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
	-Wno-pointer-sign -Wno-unused-function -Wno-unused-variable

# The third vendored driver: rtw89 (RTL8851BU, USB).  Its own include root and
# feature face, and it is a wireless driver like iwlwifi (same stack, same
# bring-up shape), so its sources compile in the driver flag world below rather
# than in the kpi world.
RTW89_DIR := $(ROOT)/wireless-drivers/rtw89
$(eval $(call component_sources,$(RTW89_DIR),drv,RTW89_IMPL))

# The driver's single bulk-in transfer, read from the driver's own header: the
# image gate compares it with the adapter's stage slot instead of trusting a
# comment (the previous line's rtw89.mk did the same).
RTW89_RECVBUF_SZ := $(shell sed -n 's/^#define[[:space:]]*RTW89_USB_RECVBUF_SZ[[:space:]]*\([0-9][0-9]*\).*/\1/p' $(RTW89_DIR)/usb.h)

RTW89_CFLAGS := $(S0_CFLAGS) \
	-I$(RTW89_DIR) \
	-include $(RTW89_DIR)/driver_config.h \
	-Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
	-Wno-address-of-packed-member -Wno-type-limits -Wno-unused-but-set-variable \
	-Wno-override-init

S1_CFLAGS := $(S0_CFLAGS) \
	-I$(IWL_DRV) \
	-include $(LIB8)/include/linux80211/driver_config.h \
	-Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
	-Wno-address-of-packed-member -Wno-type-limits -Wno-unused-but-set-variable \
	-Wno-override-init

# Which driver S1b/S1c measures.  The two sets have their own include roots and
# feature faces, so the selector carries the flag world with the file list; the
# default keeps the previous line's measurement unchanged.
S1_DRIVER ?= iwlwifi
ifeq ($(S1_DRIVER),rtl8189fs)
S1_TU_SET := $(RTL8189FS_IMPL_SRCS)
S1_DRIVER_CFLAGS := $(RTL8189FS_CFLAGS)
S1_DRIVER_TAG := rtl8189fs
else ifeq ($(S1_DRIVER),rtw89)
S1_TU_SET := $(RTW89_IMPL_SRCS)
S1_DRIVER_CFLAGS := $(RTW89_CFLAGS)
S1_DRIVER_TAG := rtw89
else
S1_TU_SET := $(IWLWIFI_IMPL_SRCS)
S1_DRIVER_CFLAGS := $(S1_CFLAGS)
S1_DRIVER_TAG := iwlwifi
endif

S1_OUT ?= $(abspath $(ROOT)/../runs/S1)
S1_RUN ?= 1
S1_DIR := $(S1_OUT)/driver-$(S1_DRIVER_TAG)/run-$(S1_RUN)
S1_TU_LIST := $(BUILD)/s1-tus-$(S1_DRIVER_TAG).txt

s1-driver-compile: $(CONFIG_FILE)
	@mkdir -p $(S1_DIR)
	@printf '%s\n' $(S1_TU_SET) > $(S1_TU_LIST)
	@sh $(ROOT)/tools/build/s0-compile.sh \
		$(S1_DIR) $(CC) '$(S1_DRIVER_CFLAGS)' $(S1_TU_LIST)
	@cp $(S1_DIR)/summary.tsv $(S1_OUT)/driver-$(S1_DRIVER_TAG)/summary.tsv
	@printf 's1-driver-compile: %s run %s -> %s\n' '$(S1_DRIVER_TAG)' '$(S1_RUN)' '$(S1_DIR)'
endif

# S1c: the partial link and its classified undefined list.  The stack and
# driver objects are rebuilt here (same two flag worlds as S0/S1b) so a run is
# one self-contained measurement; the component's own bodies are compiled too
# because "which names does the component already define" is half the answer.
# tools/build/s1-link.sh owns the `ld -r` and the classification; this file
# owns the object sets and their flags.
ifeq ($(CONFIG_LIBLINUX80211),1)
S1_LINK_OUT ?= $(abspath $(ROOT)/../runs/S1/link)
S1_LINK_RUN ?= 1
S1_LINK_DIR := $(S1_LINK_OUT)/$(S1_DRIVER_TAG)-run-$(S1_LINK_RUN)
S1_LINK_STACK_LIST := $(BUILD)/s1-link-stack.txt
S1_LINK_DRV_LIST := $(BUILD)/s1-link-drv.txt
S1_LINK_KPI_LIST := $(BUILD)/s1-link-kpi.txt

s1-link: $(CONFIG_FILE)
	@mkdir -p $(S1_LINK_DIR)
	@printf '%s\n' $(LIBLINUX80211_STACK_SRCS) > $(S1_LINK_STACK_LIST)
	@printf '%s\n' $(S1_TU_SET) > $(S1_LINK_DRV_LIST)
	@printf '%s\n' $(LIBLINUX80211_KPI_SRCS) $(LIBLINUX80211_BUS_PCI_SRCS) \
		$(LIBLINUX80211_BUS_SDIO_SRCS) $(LIBLINUX80211_BUS_USB_SRCS) \
		$(LIBLINUX80211_PCIE_PORT_SRCS) $(LIBLINUX80211_USB_PORT_SRCS) \
		$(RTL8189FS_SDIO_SRCS) > $(S1_LINK_KPI_LIST)
	@sh $(ROOT)/tools/build/s0-compile.sh $(S1_LINK_DIR)/stack $(CC) '$(S0_CFLAGS)' $(S1_LINK_STACK_LIST)
	@sh $(ROOT)/tools/build/s0-compile.sh $(S1_LINK_DIR)/driver $(CC) '$(S1_DRIVER_CFLAGS)' $(S1_LINK_DRV_LIST)
	@sh $(ROOT)/tools/build/s0-compile.sh $(S1_LINK_DIR)/kpi $(CC) '$(KPI_CFLAGS)' $(S1_LINK_KPI_LIST)
	@awk -F'\t' 'NR > 1 && $$2 != "pass" { bad = 1 } END { exit bad ? 1 : 0 }' \
		$(S1_LINK_DIR)/kpi/summary.tsv || { \
		echo 's1-link: the component bodies did not all compile; fix those first' >&2; exit 1; }
	@sh $(ROOT)/tools/build/s1-link.sh $(S1_LINK_DIR) $(LD) $(NM)
	@printf 's1-link: run %s -> %s\n' '$(S1_LINK_RUN)' '$(S1_LINK_DIR)'
endif

# The wireless world (stack + optional driver).  Empty unless the profile
# selects CONFIG_LIBLINUX80211_WIRELESS; see wl80211.mk for the flag worlds and
# the link-order contract.
include $(CURDIR)/wl80211.mk

BASE_SRCS := $(ARCH_BASE_SRCS) $(BOARD_BASE_SRCS) $(OS_BASE_SRCS) \
	$(THIRD_PARTY)/ulog/src/ulog.c

# usb_xhci/usb_ehci are compiled in the CherryUSB flag world (usb.mk); every
# other case is a plain application translation unit.
USB_CASES := usb_xhci usb_ehci
KPI_CASES := kpi_live kpi_mock
# The wireless cases compile in the stack's flag world (wl80211.mk), not the
# plain application world, so they are kept out of the generic list.  The list
# itself is wl80211.mk's (WL80211_CASES), defined before this point.
CASE_SRCS := $(foreach c,$(filter-out $(USB_CASES) $(KPI_CASES) $(WL80211_CASES),$(CASES)),$(LINE)/app/cases/$(c).c)

APP_SRCS := \
	$(LINE)/app/main.c \
	$(LINE)/app/runner.c \
	$(LINE)/app/port/clock_board.c \
	$(CASE_SRCS)

# The PCIe domain's source groups live in platform/{arch,board}/sources.mk;
# these names are the flag-world handles the link order uses.
PCIE_BOARD_SRCS := $(BOARD_PCIE_SRCS)
NVME_BOARD_SRCS := $(BOARD_NVME_SRCS)

PRINTF_SRCS := $(THIRD_PARTY)/printf/printf.c

FIRST_SRCS := $(BASE_SRCS) $(APP_SRCS) $(NET_APP_SRCS) $(EQOS_SRCS) \
	$(MMC_PORT_SRCS) $(ARCH_PCIE_SRCS) $(PCIE_BOARD_SRCS) $(NVME_BOARD_SRCS) \
	$(BACKEND_FIRST_SRCS)
VENDOR_SRCS := $(NET_STACK_SRCS) $(MMC_VENDOR_SRCS) $(BACKEND_VENDOR_SRCS)
USB_ALL_SRCS := $(USB_STACK_SRCS) $(USB_BOARD_SRCS) $(USB_CLASS_SRCS) $(USB_CASE_SRCS)

TARGET := $(BUILD)/rtos-$(BACKEND)
OBJDIR := $(BUILD)/obj/$(BACKEND)
TAG_STAMP := $(BUILD)/build-tag.stamp
FLAG_STAMP := $(BUILD)/flags-$(BACKEND).stamp

ALL_SRCS := $(FIRST_SRCS) $(VENDOR_SRCS) $(PRINTF_SRCS) $(USB_ALL_SRCS) \
	$(KPI_ALL_SRCS) $(WL80211_ALL_SRCS) $(SDIO_PORT_SRCS) \
	$(RTL8189FS_OBS_PORT_SRCS)
OBJS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(patsubst $(ROOT)/%,%,$(ALL_SRCS))))

LDFLAGS := -nostdlib -static \
	-Wl,-T,$(BOARD_LINKSCRIPT) \
	$(BACKEND_LDFLAGS) \
	$(WL80211_LDFLAGS) \
	-Wl,--gc-sections -Wl,--build-id=none \
	-Wl,-Map,$(TARGET).map \
	-lc -lgcc -lnosys

# The memcmp caller census is a link-time wrap, so it lives beside the link
# flags and is selected by the one diagnostic profile that names it.
ifeq ($(CONFIG_LKPI_MEMCMP_TRACE),1)
LDFLAGS += -Wl,--wrap=memcmp
endif

.PHONY: image gates libs clean FORCE

image: $(TARGET).bin gates

$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

gates: $(TARGET).elf
	@echo "== gates $(BACKEND) =="
	@set -e; \
	grep -q '^#define GUEST 1' $(PLATFORM)/build_defs.h || \
		{ echo "FAIL: GUEST missing (the port would build for EL3)"; exit 1; }; \
	grep -q '^#define CONFIG_BACKEND_FREERTOS 1' $(ROOT)/build/kconfig/$(PROFILE)/freewificam_config.h || \
		{ echo "FAIL: FreeRTOS backend not selected"; exit 1; }; \
	if grep -rn 'FreeRTOS\.h\|tx_api\.h' $(PLATFORM)/arch $(PLATFORM)/board >/dev/null 2>&1; then \
		echo "FAIL: a kernel header leaked into arch/ or board/"; exit 1; fi; \
	start=$$($(NM) $(TARGET).elf | awk '$$3=="_start"{print $$1}'); \
	[ -n "$$start" ] || { echo "FAIL: _start not found"; exit 1; }; \
	[ $$(( 0x$$start )) -eq $$(( $(BOARD_LOAD_BASE) )) ] || \
		{ echo "FAIL: _start=0x$$start (want $(BOARD_LOAD_BASE))"; exit 1; }; \
	vec=$$($(NM) $(TARGET).elf | awk '$$3=="$(BACKEND_VECTOR)"{print $$1}'); \
	[ -n "$$vec" ] || { echo "FAIL: $(BACKEND_VECTOR) not found"; exit 1; }; \
	[ $$(( 0x$$vec % 2048 )) -eq 0 ] || \
		{ echo "FAIL: vectors at 0x$$vec are not 2K aligned"; exit 1; }; \
	for s in $(BACKEND_SYMBOLS); do \
		$(NM) $(TARGET).elf | awk '{print $$3}' | grep -qx "$$s" || \
			{ echo "FAIL: $$s not in the image"; exit 1; }; \
	done; \
	undef=$$($(NM) -u $(TARGET).elf); \
	[ -z "$$undef" ] || { echo "FAIL: undefined symbols:"; echo "$$undef"; exit 1; }; \
	echo "PASS: _start=0x$$start vectors=0x$$vec backend=$(BACKEND) 0 undefined"
ifneq ($(filter usb_xhci usb_ehci,$(CASES)),)
	@set -e; \
	$(NM) $(TARGET).elf | awk '{print $$3}' | grep -qx msc_class_info || \
		{ echo "FAIL: msc_class_info not in the image (the class table would claim nothing)"; exit 1; }; \
	echo "PASS: usb msc class table present"
endif
# The rtw89 image's two resource gates.  A stage slot smaller than the driver's
# single bulk-in transfer refuses every receive urb for good (the adapter names
# it once, but the image would still boot into a deaf driver), and a class
# table the linker dropped enumerates the device and claims nothing.
ifeq ($(CONFIG_LIBLINUX80211_RTW89),1)
	@set -e; \
	stage=$(CONFIG_LKPI_USB_CHERRYUSB_STAGE_SIZE); \
	want=$(RTW89_RECVBUF_SZ); \
	[ -n "$$want" ] || { echo "FAIL: cannot read RTW89_USB_RECVBUF_SZ from $(RTW89_DIR)/usb.h"; exit 1; }; \
	[ "$$stage" -ge "$$want" ] || { \
		echo "FAIL: CONFIG_LKPI_USB_CHERRYUSB_STAGE_SIZE=$$stage < rtw89's bulk-in $$want"; exit 1; }; \
	$(NM) $(TARGET).elf | awk '{print $$3}' | grep -qx lkpi_wlan_class_info || \
		{ echo "FAIL: lkpi_wlan_class_info not in the image (no wireless class hook)"; exit 1; }; \
	echo "PASS: rtw89 stage $$stage >= recvbuf $$want, wlan class table present"
endif
	@$(SIZE) $(TARGET).elf
	@printf 'opt: %s\ndefconfig: %s\nprofile: %s\nlwip: %s\nprof: %s\n' '$(OPT)' '$(PROFILE)' '$(SWITCH_PROFILE)' '$(LWIP)' '$(CONFIG_GMAC_PROF)' | tee $(BUILD)/build-info-$(BACKEND).txt
	@printf 'cflags: %s\npatches: %s\n' '$(if $(strip $(PROFILE_CFLAGS)),$(PROFILE_CFLAGS),(none))' '$(if $(strip $(PATCHES)),$(PATCHES),(none))' | tee -a $(BUILD)/build-info-$(BACKEND).txt
	@printf 'build-tag: %s\n' '$(BUILD_TAG)'
	@printf 'sha256: '; sha256sum $(TARGET).bin | cut -d' ' -f1
	@printf 'size: '; stat -c '%s' $(TARGET).bin

# ---------------------------------------------------------------------------
# Component archives: the object groups that already make up this image. The
# archives are a delivery shape, not a second build: the image above stays the
# behavioural reference.
LIB_DIR := $(ROOT)/build/lib/$(PROFILE)
LIB_ARCH_OBJS := $(filter $(OBJDIR)/tests/board/rtos/platform/arch/%,$(OBJS))
LIB_BOARD_OBJS := $(filter $(OBJDIR)/tests/board/rtos/platform/board/%,$(OBJS))
LIB_OSAL_OBJS := $(filter $(OBJDIR)/tests/board/rtos/platform/os/cmsis2/% $(OBJDIR)/tests/board/rtos/platform/os/freertos/% $(OBJDIR)/tests/board/rtos/platform/os/baremetal/%,$(OBJS))
LIB_SUPPORT_OBJS := $(filter $(OBJDIR)/third-party/ulog/% $(OBJDIR)/third-party/printf/%,$(OBJS))
LIB_DRV_OBJS := $(filter $(OBJDIR)/third-party/fsl_sdmmc/%,$(OBJS))
LIB_USBHOST_OBJS := $(filter $(OBJDIR)/third-party/cherryusb/%,$(OBJS))
LIB_LINUX80211_OBJS := $(filter $(OBJDIR)/components/liblinux80211/%,$(OBJS))
LIB_APP_OBJS := $(filter $(OBJDIR)/tests/board/rtos/app/%,$(OBJS))

# Static archives can be mutually dependent (log needs uart, board log calls
# need log), so the link order wraps them in --start-group downstream.
LIB_LINK_ORDER := fw_app fw_linux80211 fw_usbhost fw_drivers fw_board_$(BOARD) \
	fw_osal_cmsis2 fw_support fw_arch

libs: image
	@mkdir -p $(LIB_DIR)
	@rm -f $(LIB_DIR)/*.a
	@if [ -n "$(strip $(LIB_ARCH_OBJS))" ]; then \
		$(AR) rcs $(LIB_DIR)/libfw_arch.a $(LIB_ARCH_OBJS); fi
	@if [ -n "$(strip $(LIB_BOARD_OBJS))" ]; then \
		$(AR) rcs $(LIB_DIR)/libfw_board_$(BOARD).a $(LIB_BOARD_OBJS); fi
	@if [ -n "$(strip $(LIB_OSAL_OBJS))" ]; then \
		$(AR) rcs $(LIB_DIR)/libfw_osal_cmsis2.a $(LIB_OSAL_OBJS); fi
	@if [ -n "$(strip $(LIB_SUPPORT_OBJS))" ]; then \
		$(AR) rcs $(LIB_DIR)/libfw_support.a $(LIB_SUPPORT_OBJS); fi
	@if [ -n "$(strip $(LIB_USBHOST_OBJS))" ]; then \
		$(AR) rcs $(LIB_DIR)/libfw_usbhost.a $(LIB_USBHOST_OBJS); fi
	@if [ -n "$(strip $(LIB_LINUX80211_OBJS))" ]; then \
		$(AR) rcs $(LIB_DIR)/liblinux80211.a $(LIB_LINUX80211_OBJS); fi
	@if [ -n "$(strip $(LIB_DRV_OBJS))" ]; then \
		$(AR) rcs $(LIB_DIR)/libfw_drivers.a $(LIB_DRV_OBJS); fi
	@if [ -n "$(strip $(LIB_APP_OBJS))" ]; then \
		$(AR) rcs $(LIB_DIR)/libfw_app.a $(LIB_APP_OBJS); fi
	@$(PYTHON) $(ROOT)/tools/build/gen_lib_manifest.py --dir $(LIB_DIR) \
		--profile $(PROFILE) --nm $(NM) --ar $(AR)

# ---------------------------------------------------------------------------
# Compile rules.

define COMPILE_C
$(OBJDIR)/$(patsubst $(ROOT)/%,%,$(1)).o: $(1) $(TAG_STAMP) $(FLAG_STAMP)
	mkdir -p $$(dir $$@)
	$(CC) $(filter-out -D%,$(2)) -I$(ROOT)/build/kconfig/$(PROFILE) -include $(PLATFORM)/build_defs.h -MMD -MP -c $$< -o $$@
endef

# The rtl8189fs vendor tree and the imported hostapd/supplicant tree are two
# independent code bases that were never meant to share a link unit: both define
# `ap_free_sta` (a 5-argument vendor function and hostapd's 2-argument teardown,
# in src/ap/sta_info.c), `hexstr2bin`, `omac1_aes_128` and the standard AES
# T-tables.  Letting the linker pick one is not an option: hostapd's own 40-odd
# `ap_free_sta(hapd, sta)` calls would bind to the vendor's signature.  The
# vendor sources are byte-identical to the GT pin and must stay so, therefore
# the rename happens on the *object*, after the compile, with one map applied to
# every vendor unit so its internal references move with it.  The map is in
# wl80211.mk (WL80211_RTL8189FS_RENAME) and needs no link flag at all.
define COMPILE_C_RENAME
$(OBJDIR)/$(patsubst $(ROOT)/%,%,$(1)).o: $(1) $(TAG_STAMP) $(FLAG_STAMP)
	mkdir -p $$(dir $$@)
	$(CC) $(filter-out -D%,$(2)) -I$(ROOT)/build/kconfig/$(PROFILE) -include $(PLATFORM)/build_defs.h -MMD -MP -c $$< -o $$@
	$(OBJCOPY) $(WL80211_RTL8189FS_RENAME) $$@
endef

define COMPILE_S
$(OBJDIR)/$(patsubst $(ROOT)/%,%,$(1)).o: $(1) $(TAG_STAMP) $(FLAG_STAMP)
	mkdir -p $$(dir $$@)
	$(CC) $(filter-out -D%,$(ASM_CFLAGS)) -c $$< -o $$@
endef

# Generated firmware units: the blob is a build product, produced from the
# tracked vendor image (components/liblinux80211/fw/) the first time a driver
# image needs it.
$(LIB8)/fw/iwl7260_fw_blob.c:
	@$(PYTHON) $(ROOT)/tools/rtos/fw-blobs.py

# Every object depends on the tag stamp: a commit changes the banner and must
# rebuild the image, or a stale banner is certified as the committed build.
$(TAG_STAMP): FORCE
	mkdir -p $(BUILD)
	printf '%s' '$(BUILD_TAG)' | cmp -s - $@ 2>/dev/null || printf '%s' '$(BUILD_TAG)' > $@

$(FLAG_STAMP): FORCE $(CONFIG_FILE)
	mkdir -p $(BUILD)
	printf '%s\n' '$(BASE_CFLAGS) $(CFLAGS)' 'asm: $(ASM_CFLAGS)' 'config: $(shell sha256sum $(CONFIG_FILE) 2>/dev/null | cut -d' ' -f1)' \
		'usb: $(USB_CFLAGS)' 'kpi: $(KPI_CFLAGS)' \
		'wl: $(WL80211_STACK_CFLAGS) | $(WL80211_DRIVER_CFLAGS)' \
		'patches: $(PATCHES) $(ADAPTER_PATCHES)' > $@.tmp
	cmp -s $@.tmp $@ || mv $@.tmp $@

$(foreach s,$(filter-out %.S,$(FIRST_SRCS)),$(eval $(call COMPILE_C,$(s),$(CFLAGS))))
$(foreach s,$(filter %.S,$(FIRST_SRCS)),$(eval $(call COMPILE_S,$(s))))
$(foreach s,$(filter-out %.S,$(VENDOR_SRCS)),$(eval $(call COMPILE_C,$(s),$(VENDORED_CFLAGS))))
$(foreach s,$(filter %.S,$(VENDOR_SRCS)),$(eval $(call COMPILE_S,$(s))))
$(foreach s,$(PRINTF_SRCS),$(eval $(call COMPILE_C,$(s),$(VENDORED_CFLAGS) $(PRINTF_CFLAGS))))

# The USB domain's compile rules follow the same consumer list as the include
# (USB_CONSUMER_CASES): a wireless image that carries the linuxkpi USB engine
# needs CherryUSB and the board glue but no mass-storage class.
ifneq ($(filter $(USB_CONSUMER_CASES),$(CASES)),)
$(foreach s,$(filter-out %.S,$(USB_STACK_SRCS) $(USB_BOARD_SRCS) $(USB_CLASS_SRCS) $(USB_CASE_SRCS)),$(eval $(call COMPILE_C,$(s),$(USB_CFLAGS))))
endif

ifneq ($(strip $(KPI_ALL_SRCS)),)
$(foreach s,$(filter-out %.S,$(KPI_ALL_SRCS)),$(eval $(call COMPILE_C,$(s),$(KPI_CFLAGS))))
endif

ifneq ($(strip $(WL80211_ALL_SRCS)),)
$(foreach s,$(filter-out %.S,$(WL80211_STACK_SRCS) $(WL80211_CASE_SRCS) $(WL80211_PROBE_SRCS)),$(eval $(call COMPILE_C,$(s),$(WL80211_STACK_CFLAGS))))
$(foreach s,$(filter-out %.S,$(WL80211_DRIVER_SRCS)),$(eval $(call COMPILE_C,$(s),$(WL80211_DRIVER_CFLAGS))))
$(foreach s,$(filter-out %.S,$(WL80211_RTW89_SRCS)),$(eval $(call COMPILE_C,$(s),$(RTW89_CFLAGS))))
$(foreach s,$(filter-out %.S,$(WL80211_RTL8189FS_SRCS)),$(eval $(call COMPILE_C_RENAME,$(s),$(RTL8189FS_CFLAGS))))
$(foreach s,$(filter-out %.S,$(SDIO_PORT_SRCS)),$(eval $(call COMPILE_C,$(s),$(RTL8189FS_SDIO_CFLAGS))))
$(foreach s,$(filter-out %.S,$(RTL8189FS_OBS_PORT_SRCS)),$(eval $(call COMPILE_C,$(s),$(RTL8189FS_CFLAGS))))
$(foreach s,$(filter-out %.S,$(WL80211_HOSTAP_SRCS)),$(eval $(call COMPILE_C,$(s),$(WL80211_HOSTAP_CFLAGS))))
$(foreach s,$(filter-out %.S,$(WL80211_HOSTAP_FP_SRCS)),$(eval $(call COMPILE_C,$(s),$(WL80211_HOSTAP_FP_CFLAGS))))
endif

-include $(shell find $(OBJDIR) -name '*.d' 2>/dev/null)

endif # BACKEND

clean:
	rm -rf $(BUILD)

# @file    Makefile
# @brief   The RTOS bin platform's only build entry: two board images and the
#          host conformance tier from one source set.
#
# Products:
#   build/rtos-freertos.bin   flat raw image, loaded as rtos.bin at 0xa000000
#                             and entered with `go 0xa000000` (the lab's `rtos`
#                             profile deploys it under that name and checks the
#                             board-side crc32 before the jump)
#   build/rtos-threadx.bin    same shape, the other kernel behind CMSIS-RTOS2
#   build/host/cmsis2-{freertos,threadx}   the conformance case on the host,
#                             one per kernel's own host port
#
# Same application sources for both kernels: only the backend source set, the
# backend defines and the link window differ (the round's V5: "the two RTOS
# differ only in os/<rtos>, the backend defines and the link script").
#
# Targets:
#   all         both board images and their gates (default)
#   images      both board images
#   check       the build-side gates of both images (alias of all)
#   image       one image; requires BACKEND=<freertos|threadx> (used by images)
#   host        the two host binaries
#   host-test   run the two host binaries and check their summary lines
#   digest      verify every third-party manifest (patches/<tree>/MANIFEST.sha256)
#   clean
#
# Knobs:
#   CASES="tick cmsis2 net emmc"  which case groups the image carries
#   OPT=-O2                       optimisation level
#   BUILD_TAG=<string>            override the banner's build tag

ROOT := $(abspath $(CURDIR)/../../../..)
LINE := $(abspath $(CURDIR)/..)
PLATFORM := $(LINE)/platform
COMPONENTS := $(ROOT)/components
LKPI := $(COMPONENTS)/linuxkpi/rtos
THIRD_PARTY := $(ROOT)/third-party

# The selected defconfig.  config.mk is a selector only; the values it used to
# include now live in the fragment generated from configs/<name>.defconfig.
include $(LINE)/config.mk
PROFILE ?= $(CONFIG)
CONFIG_FILE := $(ROOT)/build/kconfig/$(PROFILE)/freewificam_config
# `make clean` must not need the generated config (or Python).  Every other
# goal includes it; make remakes the included file through the rule below and
# restarts, so the variables are always available where they are used.
ifneq ($(filter clean,$(MAKECMDGOALS)),)
else
-include $(CONFIG_FILE)
endif

# A direct make invocation regenerates a missing config.  This is a
# target-time prerequisite, not a parse-time $(shell), so `make clean` still
# works without Python.  make.rtos regenerates before it calls make; this
# guard covers the compat path (tools/rtos/build.sh) and a hand-run make.
$(CONFIG_FILE):
	@python3 $(ROOT)/tools/build/kconfig-gen.py --profile $(PROFILE)
image images host host-test: $(CONFIG_FILE)

BACKEND := $(subst ",,$(CONFIG_BACKEND_NAME))
BOARD := $(subst ",,$(CONFIG_BOARD_NAME))
CASES := $(subst ",,$(CONFIG_CASES))
OPT := $(subst ",,$(CONFIG_OPT_FLAGS))
SWITCH_PROFILE := $(subst ",,$(CONFIG_PROFILE_NAME))
HEAP_SIZE := $(CONFIG_HEAP_SIZE)
# Configuration is data, not flags: the generated header is force-included and
# the build lines are empty of -D.
# Per-frame service-time instrumentation (dwc_eqos prof counters and the
# netio application bucket). Default on so a measurement boot is the same
# image as before; a cost A/B builds with CONFIG_GMAC_PROF=0 and then the
# 24 MHz counter is never read. It is not a tuning knob and it is not part
# of CONFIG_PROFILE - the instrument's own cost has to be measurable on
# every arm, including the ones that carry it.
CONFIG_GMAC_PROF ?= 1
WLAN_OBS := $(CONFIG_WLAN_OBS)
WLAN_TCP_SINK := $(CONFIG_WLAN_TCP_SINK)
WLAN_TCP_SINK_SECONDS := $(CONFIG_WLAN_TCP_SINK_SECONDS)
WLAN_UDP_SINK := $(CONFIG_WLAN_UDP_SINK)
WLAN_UDP_SINK_SECONDS := $(CONFIG_WLAN_UDP_SINK_SECONDS)
WLAN_UDP_PORT := $(CONFIG_WLAN_UDP_PORT)
WLAN_PING_COUNT := $(CONFIG_WLAN_PING_COUNT)
WLAN_PING_INTERVAL_MS := $(CONFIG_WLAN_PING_INTERVAL_MS)
WLAN_PING_TRACE := $(CONFIG_WLAN_PING_TRACE)
WLAN_SCAN_DIAG := $(CONFIG_WLAN_SCAN_DIAG)
WLAN_SCAN_BATCH := $(CONFIG_WLAN_SCAN_BATCH)
WLAN_PUMP_PRIO := $(CONFIG_WLAN_PUMP_PRIO)
WLAN_PUMP_DRAIN := $(CONFIG_WLAN_PUMP_DRAIN)

# The cross toolchain: put an `aarch64-none-elf-` toolchain on PATH, or name
# its prefix directory in AARCH64_CROSS_PATH, or name the whole prefix in
# CROSS_COMPILE.  No machine layout is assumed here.
ifneq ($(strip $(AARCH64_CROSS_PATH)),)
CROSS_COMPILE ?= $(AARCH64_CROSS_PATH)/bin/aarch64-none-elf-
else
CROSS_COMPILE ?= aarch64-none-elf-
endif
CC := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
NM := $(CROSS_COMPILE)nm
AR := $(CROSS_COMPILE)ar
SIZE := $(CROSS_COMPILE)size
HOSTCC ?= cc

KERNEL  := $(ROOT)/third-party/FreeRTOS-Kernel
# The imported trees carry their recorded patches in place; the build compiles
# the tree directly and the patches/ directory is the record, not a replay.
ADAPTER := $(ROOT)/third-party/CMSIS-FreeRTOS
TX      := $(ROOT)/third-party/threadx
TX_PORT := $(TX)/ports/cortex_a55/gnu
TX_HOST := $(TX)/ports/linux/gnu
CMSIS   := $(ROOT)/third-party/CMSIS_6
# The stack core is selectable so a version question can be answered by a
# one-line rebuild instead of an argument: LWIP_DIR names the tree, and the
# default is the tree this repo carries. Set LWIP_DIR to a checkout of a
# different lwIP when isolating a stack behaviour from a driver behaviour.
LWIP_DIR ?= $(ROOT)/third-party/lwip
LWIP    := $(LWIP_DIR)
PATCHES := $(notdir $(wildcard $(ROOT)/third-party/lwip/patches/*.patch))
ADAPTER_PATCHES := $(notdir $(wildcard $(ROOT)/third-party/CMSIS-FreeRTOS/patches/*.patch))

BUILD := $(CURDIR)/build
BUILD_TAG ?= $(shell git -C $(ROOT) describe --always --dirty 2>/dev/null || echo unknown)

# --- board selection ---------------------------------------------------------
# One board per image. rk3568-itx keeps every number the pre-board-makefile
# build had; a firefly config swaps the platform sources, the include
# flavours (the version directory leads, so board_conf.h resolves to
# board/firefly/<version>), the ThreadX board glue directory, the link window
# and the entry-address gate.
ifeq ($(BOARD),rk3568-itx)
BOARD_INCLUDES := \
	-I$(PLATFORM)/board/rk3568-itx \
	-I$(PLATFORM)/board/rk3568-itx/drivers
BOARD_TX_DIR := rk3568-itx
BOARD_TX_EXTRA_SRCS :=
BOARD_LINKSCRIPT := $(PLATFORM)/arch/aarch64/baremetal.ld
BOARD_LOAD_BASE := 0xa000000
else ifeq ($(BOARD),firefly-v1)
BOARD_INCLUDES := \
	-I$(PLATFORM)/board/firefly/v1 \
	-I$(PLATFORM)/board/firefly \
	-I$(PLATFORM)/board/firefly/drivers \
	-I$(PLATFORM)/board/firefly/drivers/net \
	-I$(PLATFORM)/board/firefly/drivers/nvme
BOARD_TX_DIR := firefly
# The firefly board image's imported world (NVMe, lwIP) calls the C library
# names; the rk3568 images answer them only when the wlan case pulls them in.
BOARD_TX_EXTRA_SRCS := $(PLATFORM)/os/threadx/board/firefly/libc_bindings.c
# The board's wrapper script assigns board_ram_base/board_ram_top before
# DEFINED() reads them (command-line defsym does not satisfy DEFINED()).
BOARD_LINKSCRIPT := $(PLATFORM)/board/firefly/v1/link.ld
BOARD_LOAD_BASE := 0x90000000
# This board's images reset themselves (PSCI) once the case table's verdict
# line is out, and a failed assertion resets instead of parking - the bench
# never needs a manual power cycle.
else ifeq ($(BOARD),firefly-v2)
# Same family sources as v1; only the version directory (register map) and
# the two hardware facts v2 changed - the console clock and the Ethernet
# block - differ, and both live in v2's board_conf.h.
BOARD_INCLUDES := \
	-I$(PLATFORM)/board/firefly/v2 \
	-I$(PLATFORM)/board/firefly \
	-I$(PLATFORM)/board/firefly/drivers \
	-I$(PLATFORM)/board/firefly/drivers/net \
	-I$(PLATFORM)/board/firefly/drivers/nvme
BOARD_TX_DIR := firefly
BOARD_TX_EXTRA_SRCS := $(PLATFORM)/os/threadx/board/firefly/libc_bindings.c
BOARD_LINKSCRIPT := $(PLATFORM)/board/firefly/v2/link.ld
BOARD_LOAD_BASE := 0x90000000
else
$(error unknown BOARD '$(BOARD)': use rk3568-itx, firefly-v1 or firefly-v2)
endif
# Optimisation level. -O2 is the adopted default since the performance round
# (the performance round's profile decision): +17% uplink, -73% ping RTT,
# +16% board-local loopback, no change on the storage path and no suite
# regression. The 2026-10-25 lwIP round measured -O3 neutral on both legs
# (down 480.9 vs 483.2, up 690.7 vs 687.3), so there is nothing to re-certify
# for. `OPT=-Og` builds the pre-round image for debug work.
# ThreadX's kernel heap (linker symbol).  The storage/network images use the
# default 4M; the 8189FTV vendor stack starts at the reference image's 16M.

# Compiler channel, the same idea one level down: the flags an arm changes in
# the code generator rather than in the preprocessor (the core to schedule for,
# whether unaligned accesses may be emitted). Kept apart from LEVERS because a
# -D and a -m are different kinds of claim, and because these belong to the
# profile rather than to a single option: `PROFILE_CFLAGS=` gets the pre-round
# code generator back, exactly as `SWITCH_PROFILE=base` does for the switches.
#
#   PROFILE_CFLAGS="-mcpu=cortex-a55 -mno-strict-align" tools/rtos/build.sh
#
# It has to sit AFTER the base -m flags: gcc takes the last of a conflicting
# pair, so a -mno-strict-align placed before -mstrict-align is silently
# overridden - which is exactly how the first compiler arm of the 2026-10-25
# round came to measure only -O3.

# Case selection: the source set and the runner's table are trimmed together,
# so a short arm builds a short image (`CASES=tick,cmsis2`).
CASES ?= tick cmsis2 net emmc
# The wireless cases are compiled in their own flag worlds (wlan.mk /
# iwlwifi.mk), so they are excluded here.
CASE_SRCS := $(foreach c,$(filter-out wlan iwl7260 iwl7260_net usb_xhci usb_ehci rtw88 rtw88_scan rtw89 rtw89_scan rtw89_join rtw89_dataplane rtl8xxxu rtl8xxxu_scan rtl8xxxu_join rtl8xxxu_dataplane rtl8xxxu_net rtl8xxxu_netio uvc_detect uvc_open uvc_iso_smoke uvc_stream sdio_mci,$(CASES)),$(LINE)/app/cases/$(c).c)

.DEFAULT_GOAL := all

.PHONY: all images check image print-config host host-test digest clean FORCE

# A rebuild must follow a flag change: the objs depend on a stamp whose
# content is the flag set (a stale object built with other defines is the
# classic way to certify the wrong bytes).
FORCE:

all: image
check: image

print-config:
	@printf '%s\n' '$(BACKEND)'

images:
	@$(MAKE) --no-print-directory image BACKEND=freertos
	@$(MAKE) --no-print-directory image BACKEND=threadx

digest:
	@set -e; for t in CMSIS_6 CMSIS-FreeRTOS FreeRTOS-Kernel lwip threadx \
		cherryusb fsl_sdmmc printf tlsf ulog; do \
		( cd $(ROOT)/third-party/$$t && \
		  sha256sum -c --quiet patches/MANIFEST.sha256 ); \
		echo "digest: $$t ok ($$(wc -l < $(ROOT)/third-party/$$t/patches/MANIFEST.sha256) files)"; \
	done
	@set -e; for d in iwlwifi rtl8xxxu rtw88 rtw89; do \
		( cd $(ROOT)/wireless-drivers/$$d && \
		  sha256sum -c --quiet patches/MANIFEST.sha256 ); \
		echo "digest: $$d ok ($$(wc -l < $(ROOT)/wireless-drivers/$$d/patches/MANIFEST.sha256) files)"; \
	done
	@set -e; ( cd $(ROOT)/components/net80211 && \
		sha256sum -c --quiet patches/MANIFEST.sha256 ); \
		echo "digest: net80211 ok ($$(wc -l < $(ROOT)/components/net80211/patches/MANIFEST.sha256) files)"
	@set -e; ( cd $(ROOT)/tools/kconfig && sha256sum -c --quiet MANIFEST.sha256 ); \
		echo "digest: kconfig ok (2 files)"
	@set -e; ( cd $(ROOT)/third-party/cherrysh && sha256sum -c --quiet MANIFEST.sha256 ); \
		echo "digest: cherrysh ok (14 files)"

clean:
	rm -rf $(BUILD)

# ---------------------------------------------------------------------------
# Host tier (host compiler; independent of BACKEND).

HOST_BUILD := $(BUILD)/host
HOST_CASE_SRCS := \
	$(LINE)/app/cases/cmsis2.c \
	$(LINE)/app/runner.c \
	$(LINE)/app/port/clock_host.c

HOST_FRTOS_SRCS := \
	$(HOST_CASE_SRCS) \
	$(LINE)/tests/cmsis2-host/main.c \
	$(PLATFORM)/os/freertos/freertos_hooks.c \
	$(PLATFORM)/os/freertos/cmsis2_host.c \
	$(PLATFORM)/os/freertos/cmsis2_ext.c \
	$(KERNEL)/croutine.c $(KERNEL)/event_groups.c $(KERNEL)/list.c \
	$(KERNEL)/queue.c $(KERNEL)/stream_buffer.c $(KERNEL)/tasks.c \
	$(KERNEL)/timers.c $(KERNEL)/portable/MemMang/heap_4.c \
	$(KERNEL)/portable/ThirdParty/GCC/Posix/port.c \
	$(KERNEL)/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c \
	$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Source/cmsis_os2.c

HOST_TX_SRCS := \
	$(HOST_CASE_SRCS) \
	$(LINE)/tests/cmsis2-host/main.c \
	$(PLATFORM)/os/threadx/tx_glue.c \
	$(PLATFORM)/os/threadx/heap.c \
	$(PLATFORM)/os/threadx/cmsis2_threadx.c \
	$(PLATFORM)/os/threadx/cmsis2_event.c \
	$(PLATFORM)/os/threadx/cmsis2_ext.c \
	$(PLATFORM)/os/threadx/cmsis2_host.c \
	$(wildcard $(TX_HOST)/src/*.c) \
	$(wildcard $(TX)/common/src/*.c)

HOST_COMMON_CFLAGS := -ggdb3 -O1 -std=c11 -Wall -Wextra -ffunction-sections \
	-fdata-sections -include $(PLATFORM)/host_common_defs.h \
	-I$(PLATFORM) -I$(LINE) -I$(LINE)/app -I$(PLATFORM)/os/cmsis2 \
	-I$(PLATFORM)/os/cmsis2/host-compat -I$(THIRD_PARTY)/ulog/src \
	-I$(ROOT)/build/kconfig/$(PROFILE) \
	-I$(ROOT)/build/kconfig/$(PROFILE)/include/generated \
	-I$(CMSIS)/CMSIS/RTOS2/Include

HOST_FRTOS_CFLAGS := $(HOST_COMMON_CFLAGS) -include $(PLATFORM)/host_freertos_defs.h \
	-I$(PLATFORM)/os/freertos -I$(KERNEL)/include \
	-I$(KERNEL)/portable/ThirdParty/GCC/Posix \
	-I$(KERNEL)/portable/ThirdParty/GCC/Posix/utils \
	-I$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Include

HOST_TX_CFLAGS := $(HOST_COMMON_CFLAGS) -include $(PLATFORM)/host_threadx_defs.h \
	-I$(PLATFORM)/os/threadx -I$(TX)/common/inc -I$(TX_HOST)/inc

HOST_FRTOS_OBJS := $(addprefix $(HOST_BUILD)/freertos/,$(addsuffix .o,$(patsubst $(ROOT)/%,%,$(HOST_FRTOS_SRCS))))
HOST_TX_OBJS := $(addprefix $(HOST_BUILD)/threadx/,$(addsuffix .o,$(patsubst $(ROOT)/%,%,$(HOST_TX_SRCS))))

HOST_STAMP := $(HOST_BUILD)/flags.stamp

host: $(HOST_BUILD)/cmsis2-freertos $(HOST_BUILD)/cmsis2-threadx

$(HOST_STAMP): FORCE
	@mkdir -p $(dir $@)
	@printf '%s\n%s\n' '$(HOST_FRTOS_CFLAGS)' '$(HOST_TX_CFLAGS)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@

$(HOST_BUILD)/freertos/%.o: $(ROOT)/% $(HOST_STAMP)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_FRTOS_CFLAGS) -MMD -MP -c $< -o $@

$(HOST_BUILD)/threadx/%.o: $(ROOT)/% $(HOST_STAMP)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_TX_CFLAGS) -MMD -MP -c $< -o $@

$(HOST_BUILD)/cmsis2-freertos: $(HOST_FRTOS_OBJS)
	$(HOSTCC) $^ -pthread -Wl,--gc-sections -o $@

$(HOST_BUILD)/cmsis2-threadx: $(HOST_TX_OBJS)
	$(HOSTCC) $^ -pthread -lrt -Wl,--gc-sections -o $@

host-test: host
	@set -e; for b in cmsis2-freertos cmsis2-threadx; do \
		echo "== host $$b"; \
		$(HOST_BUILD)/$$b > $(HOST_BUILD)/$$b.log 2>&1 || true; \
		cat $(HOST_BUILD)/$$b.log; \
		grep -q "^rtos: result pass" $(HOST_BUILD)/$$b.log || \
			{ echo "FAIL: $$b did not report a pass"; exit 1; }; \
	done
	@echo "host-test: both backends passed"

-include $(shell find $(HOST_BUILD) -name '*.d' 2>/dev/null)

# ---------------------------------------------------------------------------
# Board image.  One backend per sub-make invocation; the whole section is
# skipped when BACKEND is unset so `make`, `make host` and `make digest` work.

ifneq ($(BACKEND),)

ifeq ($(wildcard $(ADAPTER)),)
$(error the CMSIS-FreeRTOS tree is missing)
endif
ifeq ($(wildcard $(LWIP)),)
$(error the lwIP tree is missing)
endif

TARGET := $(BUILD)/rtos-$(BACKEND)
OBJDIR := $(BUILD)/obj/$(BACKEND)
TAG_STAMP := $(BUILD)/build-tag.stamp
FLAG_STAMP := $(BUILD)/flags-$(BACKEND).stamp

ifeq ($(BACKEND),freertos)
KERNEL_NAME := FreeRTOS-Kernel V11.3.1
BACKEND_INCLUDES := \
	-I$(PLATFORM)/os/freertos/board/rk3568-itx/cmsis2 \
	-I$(PLATFORM)/os/freertos/board/rk3568-itx \
	-I$(KERNEL)/include -I$(KERNEL)/portable/GCC/ARM_AARCH64_SRE \
	-I$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Include
BACKEND_LDFLAGS := -Wl,-u,osCriticalEnter -Wl,-u,osCriticalExit
BACKEND_VECTOR := _freertos_vector_table
BACKEND_SYMBOLS := FreeRTOS_IRQ_Handler FreeRTOS_Tick_Handler xPortStartScheduler
BACKEND_FIRST_SRCS := \
	$(PLATFORM)/os/freertos/board/rk3568-itx/freertos_board_glue.c \
	$(PLATFORM)/os/freertos/board/rk3568-itx/freertos_board_vectors.S \
	$(PLATFORM)/os/freertos/board/rk3568-itx/cmsis2_ext_board.c \
	$(PLATFORM)/os/freertos/board/rk3568-itx/cmsis2_memory_board.c
BACKEND_VENDOR_SRCS := \
	$(KERNEL)/tasks.c $(KERNEL)/list.c $(KERNEL)/queue.c \
	$(KERNEL)/event_groups.c $(KERNEL)/timers.c $(KERNEL)/stream_buffer.c \
	$(KERNEL)/portable/MemMang/heap_4.c \
	$(KERNEL)/portable/GCC/ARM_AARCH64_SRE/port.c \
	$(KERNEL)/portable/GCC/ARM_AARCH64_SRE/portASM.S \
	$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Source/cmsis_os2.c
else ifeq ($(BACKEND),threadx)
KERNEL_NAME := ThreadX 6.5.1.202602a
BACKEND_INCLUDES := \
	-I$(PLATFORM)/os/threadx/board/$(BOARD_TX_DIR)/cmsis2 \
	-I$(PLATFORM)/os/threadx \
	-I$(TX)/common/inc -I$(TX_PORT)/inc
BACKEND_LDFLAGS := -Wl,--defsym=HEAP_SIZE=$(HEAP_SIZE) \
	-Wl,-u,osCriticalEnter -Wl,-u,osCriticalExit
BACKEND_VECTOR := _threadx_vector_table
BACKEND_SYMBOLS := _tx_initialize_kernel_enter _tx_thread_schedule _tx_timer_interrupt __top_of_ram
BACKEND_FIRST_SRCS := \
	$(PLATFORM)/os/threadx/board/$(BOARD_TX_DIR)/tx_glue.c \
	$(PLATFORM)/os/threadx/board/$(BOARD_TX_DIR)/tx_vectors.S \
	$(PLATFORM)/os/threadx/board/$(BOARD_TX_DIR)/heap.c \
	$(PLATFORM)/os/threadx/board/$(BOARD_TX_DIR)/cmsis2/cmsis2_board.c \
	$(BOARD_TX_EXTRA_SRCS) \
	$(PLATFORM)/os/threadx/cmsis2_threadx.c \
	$(PLATFORM)/os/threadx/cmsis2_event.c \
	$(PLATFORM)/os/threadx/cmsis2_ext.c
BACKEND_VENDOR_SRCS := \
	$(wildcard $(TX)/common/src/*.c) \
	$(wildcard $(TX_PORT)/src/*.S) \
	$(THIRD_PARTY)/tlsf/tlsf.c
else
$(error unknown BACKEND '$(BACKEND)': use freertos or threadx)
endif

INCLUDES := \
	-I$(PLATFORM) -I$(LINE) \
	-I$(LINE)/app \
	-I$(PLATFORM)/os/cmsis2 \
	-I$(PLATFORM)/os/cmsis2/board-compat \
	-I$(PLATFORM)/arch/aarch64 \
	$(BOARD_INCLUDES) \
	-I$(PLATFORM)/os/baremetal \
	-I$(THIRD_PARTY)/ulog/src \
	-I$(THIRD_PARTY)/tlsf \
	-I$(THIRD_PARTY)/lwip/ports -I$(THIRD_PARTY)/lwip/ports/include \
	-I$(ROOT)/build/kconfig/$(PROFILE) \
	-I$(ROOT)/build/kconfig/$(PROFILE)/include/generated \
	-I$(LWIP)/src/include \
	-I$(THIRD_PARTY)/fsl_sdmmc/ports -I$(THIRD_PARTY)/fsl_sdmmc/ports/shadow \
	-I$(THIRD_PARTY)/fsl_sdmmc/common \
	-I$(THIRD_PARTY)/fsl_sdmmc/mmc \
	-I$(THIRD_PARTY)/fsl_sdmmc/osa \
	-I$(CMSIS)/CMSIS/RTOS2/Include \
	-I$(CMSIS)/CMSIS/Driver/Include \
	-I$(CMSIS)/CMSIS/Core/Include/a-profile \
	$(BACKEND_INCLUDES)

# The PCIe/NVMe cases include the board driver headers directly (they are not
# reachable through the generic drivers/ include root).
ifneq ($(filter pcie,$(CASES)),)
ifeq ($(BOARD),rk3568-itx)
INCLUDES += -I$(PLATFORM)/board/rk3568-itx/drivers/pcie \
	-I$(PLATFORM)/board/rk3568-itx/drivers/nvme
endif
endif

# The CherrySH console layer: vendored core + the board port + the command
# table.  The generated csh_config.h carries the Kconfig choices; the core has
# no Kconfig of its own.  Off means no shell sources and no include roots.
ifneq ($(filter 1,$(CONFIG_SHELL)),)
INCLUDES += -I$(THIRD_PARTY)/cherrysh -I$(COMPONENTS)/shell \
	-I$(ROOT)/build/kconfig/$(PROFILE)/include/generated
SHELL_APP_SRCS := \
	$(COMPONENTS)/shell/shell_port.c \
	$(COMPONENTS)/shell/shell_console.c \
	$(COMPONENTS)/shell/shell_cmds.c
CHERRYSH_SRCS := \
	$(THIRD_PARTY)/cherrysh/chry_shell.c \
	$(THIRD_PARTY)/cherrysh/cherryrl/chry_readline.c \
	$(THIRD_PARTY)/cherrysh/builtin/clear.c \
	$(THIRD_PARTY)/cherrysh/builtin/help.c \
	$(THIRD_PARTY)/cherrysh/builtin/shsize.c
else
SHELL_APP_SRCS :=
CHERRYSH_SRCS :=
endif

BASE_CFLAGS := $(OPT) -g$(CONFIG_DEBUG_LEVEL_VALUE) -std=c11 -Wall -Wextra \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-march=armv8-a -mgeneral-regs-only -mstrict-align -mno-outline-atomics \
	$(PROFILE_CFLAGS) \
	-ffunction-sections -fdata-sections \
	-include $(PLATFORM)/build_defs.h \
	$(INCLUDES)

CFLAGS := $(BASE_CFLAGS) -include stdio.h -include string.h -include strings.h \
	-include stdlib.h
ASM_CFLAGS := $(OPT) -g$(CONFIG_DEBUG_LEVEL_VALUE) -march=armv8-a \
	-mgeneral-regs-only -mstrict-align \
	-include $(PLATFORM)/build_defs.h $(INCLUDES)
VENDORED_CFLAGS := $(filter-out -Wextra,$(CFLAGS))
PRINTF_CFLAGS :=

# The 8189FTV vendor-stack world (empty unless the wlan case is selected).
# The lwIP core/protocol set: one list, because two images now carry the
# stack (the GMAC net case and the 7260 net case over the bridge).  Only
# the port glue and the presentation layer differ per image.
LWIP_STACK_SRCS :=	$(LWIP)/src/core/init.c $(LWIP)/src/core/def.c $(LWIP)/src/core/dns.c \
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
# Component source lists (BUILD-01 M4).  Each component owns one sources.mk;
# the helper in tools/build/sources.mk turns its kbuild-style obj- lines into
# the per-flag-world lists the fragments below consume.  The lists are built
# from the generated configuration, not from the case string.
include $(ROOT)/tools/build/sources.mk

$(eval $(call component_sources,$(ROOT)/components/net80211,core,NET80211_CORE))
$(eval $(call component_sources,$(ROOT)/components/net80211,nport,NET80211_NPORT))
$(eval $(call component_sources,$(ROOT)/components/net80211,bridge,NET80211_BRIDGE))
$(eval $(call component_sources,$(ROOT)/components/net80211,kpi,NET80211_KPI))
$(eval $(call component_sources,$(ROOT)/components/net80211,rxba,NET80211_RXBA))
NET80211_SRCS := $(NET80211_CORE_SRCS) $(NET80211_NPORT_SRCS) \
	$(NET80211_BRIDGE_SRCS)

$(eval $(call component_sources,$(COMPONENTS)/linuxkpi,net80211,LINUXKPI))
$(eval $(call component_sources,$(COMPONENTS)/linuxkpi,cherry,LINUXKPI_CHERRY))
$(eval $(call component_sources,$(COMPONENTS)/linuxkpi,wlan,LINUXKPI_WLAN))

$(eval $(call component_sources,$(ROOT)/wireless-drivers/rtw89,drv,RTW89_IMPL))
$(eval $(call component_sources,$(ROOT)/wireless-drivers/rtw89,kpi,RTW89_BLOB))
$(eval $(call component_sources,$(ROOT)/wireless-drivers/rtw89,case,RTW89_APP))

$(eval $(call component_sources,$(ROOT)/wireless-drivers/rtl8xxxu,drv,RTL8XXXU_IMPL))
$(eval $(call component_sources,$(ROOT)/wireless-drivers/rtl8xxxu,kpi,RTL8XXXU_BLOB))
$(eval $(call component_sources,$(ROOT)/wireless-drivers/rtl8xxxu,case,RTL8XXXU_APP))

$(eval $(call component_sources,$(ROOT)/wireless-drivers/rtw88,drv,RTW88_IMPL))
$(eval $(call component_sources,$(ROOT)/wireless-drivers/rtw88,kpi,RTW88_BLOB))
$(eval $(call component_sources,$(ROOT)/wireless-drivers/rtw88,case,RTW88_APP))

$(eval $(call component_sources,$(ROOT)/wireless-drivers/iwlwifi,drv,IWLWIFI_IMPL))
$(eval $(call component_sources,$(ROOT)/wireless-drivers/iwlwifi,kpi,IWLWIFI_BLOB))
$(eval $(call component_sources,$(ROOT)/wireless-drivers/iwlwifi,case,IWLWIFI_APP))

$(eval $(call component_sources,$(ROOT)/wireless-drivers/rtl8189fs,app,RTL8189FS_APP))
$(eval $(call component_sources,$(ROOT)/wireless-drivers/rtl8189fs,sdio,RTL8189FS_SDIO))
$(eval $(call component_sources,$(ROOT)/wireless-drivers/rtl8189fs,drv,RTL8189FS_IMPL))
$(eval $(call component_sources,$(ROOT)/wireless-drivers/rtl8189fs,lwip,RTL8189FS_LWIP))
$(eval $(call component_sources,$(ROOT)/wireless-drivers/rtl8189fs,obs,RTL8189FS_OBS))

$(eval $(call component_sources,$(PLATFORM)/arch,base,ARCH_BASE))
$(eval $(call component_sources,$(PLATFORM)/arch,pcie,ARCH_PCIE))
$(eval $(call component_sources,$(PLATFORM)/board,base,BOARD_BASE))
$(eval $(call component_sources,$(PLATFORM)/board,pcie,BOARD_PCIE))
$(eval $(call component_sources,$(PLATFORM)/board,nvme,BOARD_NVME))
$(eval $(call component_sources,$(PLATFORM)/board,fnvme,BOARD_FNVME))
$(eval $(call component_sources,$(PLATFORM)/board,ecam,BOARD_ECAM))
$(eval $(call component_sources,$(PLATFORM)/os,baremetal,OS_BASE))

include $(CURDIR)/wlan.mk

# The B3 wireless world (empty unless the iwlwifi case is selected).
ifneq ($(filter iwl7260,$(CASES)),)
include $(CURDIR)/iwlwifi.mk
endif

# The USB host domain (empty unless a USB consumer is selected).  It must be
# included before rtw89.mk: the wireless world's linuxkpi USB engine compiles
# against the same CherryUSB include roots and stage geometry.
ifneq ($(filter usb_xhci usb_ehci rtw89 rtl8xxxu uvc_detect uvc_open uvc_iso_smoke uvc_stream,$(CASES)),)
include $(CURDIR)/usb.mk
endif

# The camera image (empty unless a camera case is selected): the video class,
# its running-stream half and the EHCI isochronous engine, on top of usb.mk.
ifneq ($(filter uvc_detect uvc_open uvc_iso_smoke uvc_stream,$(CASES)),)
include $(CURDIR)/uvc.mk
endif

# The RTL8851BU wireless world (empty unless the rtw89 case is selected).
ifneq ($(filter rtw89,$(CASES)),)
include $(CURDIR)/rtw89.mk
endif

# The RTL8188EUS wireless world (empty unless the rtl8xxxu case is selected).
ifneq ($(filter rtl8xxxu,$(CASES)),)
include $(CURDIR)/rtl8xxxu.mk
endif

# The board's SDIO socket over the ported Phytium MCI controller (empty unless
# the sdio_mci case is selected).
ifneq ($(filter sdio_mci,$(CASES)),)
include $(CURDIR)/sdio_mci.mk
endif

# The RTL8821CS wireless world over that same SDIO socket (empty unless the
# rtw88 case is selected).  It carries its own copy of the stack's port units -
# one image may compile exactly one SDIO host port - so it is never selected
# together with a USB wireless world.
ifneq ($(filter rtw88,$(CASES)),)
include $(CURDIR)/rtw88_sdio.mk
endif

BASE_SRCS := $(ARCH_BASE_SRCS) $(BOARD_BASE_SRCS) $(OS_BASE_SRCS) \
	$(THIRD_PARTY)/ulog/src/ulog.c

APP_SRCS := \
	$(LINE)/app/main.c \
	$(LINE)/app/runner.c \
	$(LINE)/app/port/clock_board.c \
	$(SHELL_APP_SRCS) \
	$(CASE_SRCS)

# The wired-network cases each bring the lwIP port face and the stack core;
# the per-case group below adds that NIC's own netif bridge and driver. A boot
# may carry more than one (they share one stack bring-up).
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

# The per-NIC groups: the rk3568 wired cases keep the eqos line; the firefly v2
# case brings its own board driver and netif bridge. The per-frame cost
# instrument rides with the netio arm only - it is a measurement aid, not part
# of the driver, and a plain `net` arm must not pay for it.
EQOS_SRCS :=
ifneq ($(filter net net_mdk_netio net_pps,$(CASES)),)
NET_APP_SRCS += $(THIRD_PARTY)/lwip/ports/ethnetif.c
EQOS_SRCS += \
	$(PLATFORM)/board/rk3568-itx/drivers/dwc_eqos.c \
	$(PLATFORM)/board/rk3568-itx/drivers/eqos_rk3568.c \
	$(PLATFORM)/board/rk3568-itx/drivers/rtl8211f.c
endif
ifneq ($(filter net_mdk_netio net_pps,$(CASES)),)
NET_APP_SRCS += \
	$(LINE)/app/net_prof.c \
	$(LINE)/app/net_prof_gmac.c
endif
XMAC_SRCS :=
ifneq ($(filter net_xmac,$(CASES)),)
NET_APP_SRCS += $(THIRD_PARTY)/lwip/ports/xmac_netif.c
XMAC_SRCS += $(PLATFORM)/board/firefly/drivers/net/xmac.c
endif

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

# The PCIe domain's source groups live in platform/{arch,board}/sources.mk;
# these names are the flag-world handles the link order uses.
PCIE_BOARD_SRCS := $(BOARD_PCIE_SRCS)
NVME_BOARD_SRCS := $(BOARD_NVME_SRCS)

# The firefly board carries its own ECAM root complex and NVMe driver instead
# of the RK3568 DesignWare pair; both groups live in platform/board/sources.mk.
FIREFLY_PCIE_SRCS := $(BOARD_ECAM_SRCS)
FIREFLY_NVME_SRCS := $(BOARD_FNVME_SRCS)

PRINTF_SRCS := $(THIRD_PARTY)/printf/printf.c

FIRST_SRCS := $(BASE_SRCS) $(APP_SRCS) $(NET_APP_SRCS) $(EQOS_SRCS) \
	$(XMAC_SRCS) \
	$(MMC_PORT_SRCS) $(ARCH_PCIE_SRCS) $(PCIE_BOARD_SRCS) $(NVME_BOARD_SRCS) \
	$(FIREFLY_PCIE_SRCS) $(FIREFLY_NVME_SRCS) \
	$(BACKEND_FIRST_SRCS)
VENDOR_SRCS := $(NET_STACK_SRCS) $(MMC_VENDOR_SRCS) $(BACKEND_VENDOR_SRCS) \
	$(CHERRYSH_SRCS)

ALL_SRCS := $(FIRST_SRCS) $(VENDOR_SRCS) $(PRINTF_SRCS) $(IWL_ALL_SRCS) \
	$(IWL_NET_SRCS) $(USB_ALL_SRCS) $(UVC_ALL_SRCS) $(RTW89_ALL_SRCS) \
	$(RTL8XXXU_ALL_SRCS) $(RTL8XXXU_NET_SRCS) $(RTL8XXXU_NETIO_SRCS) \
	$(SDIO_MCI_ALL_SRCS) $(RTW88_ALL_SRCS)
OBJS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(patsubst $(ROOT)/%,%,$(ALL_SRCS)))) \
	$(WLAN_OBJS)

LDFLAGS := -nostdlib -static \
	-Wl,-T,$(BOARD_LINKSCRIPT) \
	$(BACKEND_LDFLAGS) $(WLAN_LDFLAGS) $(RTW89_LDFLAGS) $(RTL8XXXU_LDFLAGS) \
	$(RTW88_LDFLAGS) \
	-Wl,--gc-sections -Wl,--build-id=none \
	-Wl,-Map,$(TARGET).map \
	-lc -lgcc -lnosys

.PHONY: image gates FORCE
image: $(TARGET).bin gates

# M5 library split: archive the object groups that already make up this image.
# The archives are a delivery shape, not a second build: the image above stays
# the behavioural reference.  `libfw_support.a` closes the console closure that
# the image used to get for free by linking every object: log.c needs uLog and
# the printf component, and neither was in any archive before.
LIB_DIR := $(ROOT)/build/lib/$(PROFILE)
SDK_DIR := $(ROOT)/dist/$(PROFILE)
SDK_VERSION := $(shell cat $(ROOT)/VERSION)
LIB_ARCH_OBJS := $(filter $(OBJDIR)/tests/board/rtos/platform/arch/%,$(OBJS))
LIB_BOARD_OBJS := $(filter $(OBJDIR)/tests/board/rtos/platform/board/%,$(OBJS))
LIB_OSAL_OBJS := $(filter $(OBJDIR)/tests/board/rtos/platform/os/cmsis2/% $(OBJDIR)/tests/board/rtos/platform/os/baremetal/%,$(OBJS))
LIB_SUPPORT_OBJS := $(filter $(OBJDIR)/third-party/ulog/% $(OBJDIR)/third-party/printf/%,$(OBJS))
LIB_NET80211_OBJS := $(filter $(OBJDIR)/components/net80211/%,$(OBJS))
LIB_LINUXKPI_OBJS := $(filter $(OBJDIR)/components/linuxkpi/%,$(OBJS))
LIB_DRV_OBJS := $(filter $(OBJDIR)/wireless-drivers/%,$(OBJS))
LIB_USBHOST_OBJS := $(filter $(OBJDIR)/third-party/cherryusb/% $(OBJDIR)/ports/cherryusb/%,$(OBJS))
LIB_APP_OBJS := $(filter $(OBJDIR)/tests/board/rtos/app/%,$(OBJS))

# Static archives can be mutually dependent (log needs uart, board log calls
# need log).  The .pc wraps the group in --start-group/--end-group, so this list
# is a naming order, not a one-pass link order.
LIB_LINK_ORDER := fw_app fw_net80211 fw_linuxkpi fw_usbhost fw_drivers \
	fw_board_$(BOARD) fw_osal_cmsis2 fw_support fw_arch
SDK_LIBS = $(strip $(foreach l,$(LIB_LINK_ORDER),$(if $(wildcard $(LIB_DIR)/lib$(l).a),-l$(l))))

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
	@if [ -n "$(strip $(LIB_NET80211_OBJS))" ]; then \
		$(AR) rcs $(LIB_DIR)/libfw_net80211.a $(LIB_NET80211_OBJS); fi
	@if [ -n "$(strip $(LIB_LINUXKPI_OBJS))" ]; then \
		$(AR) rcs $(LIB_DIR)/libfw_linuxkpi.a $(LIB_LINUXKPI_OBJS); fi
	@if [ -n "$(strip $(LIB_USBHOST_OBJS))" ]; then \
		$(AR) rcs $(LIB_DIR)/libfw_usbhost.a $(LIB_USBHOST_OBJS); fi
	@if [ -n "$(strip $(LIB_DRV_OBJS))" ]; then \
		$(AR) rcs $(LIB_DIR)/libfw_drivers.a $(LIB_DRV_OBJS); fi
	@if [ -n "$(strip $(LIB_APP_OBJS))" ]; then \
		$(AR) rcs $(LIB_DIR)/libfw_app.a $(LIB_APP_OBJS); fi
	@$(PYTHON) $(ROOT)/tools/build/gen_lib_manifest.py --dir $(LIB_DIR) \
		--profile $(PROFILE) --nm $(NM) --ar $(AR)

sdk: libs
	@rm -rf $(SDK_DIR)
	@mkdir -p $(SDK_DIR)/lib $(SDK_DIR)/include/freewificam/deps
	@cp $(LIB_DIR)/*.a $(LIB_DIR)/manifest.json $(SDK_DIR)/lib/
	@cp $(LIB_DIR)/manifest.json $(SDK_DIR)/manifest.json
	@cp $(ROOT)/include/freewificam/*.h $(SDK_DIR)/include/freewificam/
	@cp $(ROOT)/build/kconfig/$(PROFILE)/freewificam_config.h \
		$(SDK_DIR)/include/freewificam/freewificam_config.h
	@if [ -d $(ROOT)/include/freewificam/deps ]; then \
		cp -r $(ROOT)/include/freewificam/deps/. \
			$(SDK_DIR)/include/freewificam/deps/; fi
	@cp $(PLATFORM)/os/baremetal/log.h $(SDK_DIR)/include/freewificam/deps/log.h
	@cp $(THIRD_PARTY)/ulog/src/ulog.h $(SDK_DIR)/include/freewificam/deps/ulog.h
	@cp $(COMPONENTS)/net80211/port/port.h $(SDK_DIR)/include/freewificam/deps/port.h
	@cp $(THIRD_PARTY)/CMSIS_6/CMSIS/RTOS2/Include/cmsis_os2.h \
		$(SDK_DIR)/include/freewificam/deps/cmsis_os2.h
	@cp $(PLATFORM)/os/cmsis2/cmsis_os2_ext.h \
		$(SDK_DIR)/include/freewificam/deps/cmsis_os2_ext.h
	@cp $(ROOT)/docs/PORTING.md $(SDK_DIR)/PORTING.md
	@printf '%s\n' \
		'prefix=$(SDK_DIR)' \
		'includedir=$${prefix}/include' \
		'libdir=$${prefix}/lib' \
		'' \
		'Name: freewificam' \
		'Description: freewificam RTOS wifi core (profile $(PROFILE))' \
		'Version: $(SDK_VERSION)' \
		'Cflags: -I$${includedir}' \
		'Libs: -L$${libdir} -Wl,--start-group $(SDK_LIBS) -Wl,--end-group' \
		'Libs.private: -Wl,--gc-sections -lc -lgcc -lnosys' \
		> $(SDK_DIR)/freewificam.pc
	@printf 'sdk: %s\n' '$(SDK_DIR)'

.PHONY: libs sdk

define COMPILE_C
$(OBJDIR)/$(patsubst $(ROOT)/%,%,$(1)).o: $(1) $(TAG_STAMP) $(FLAG_STAMP)
	@mkdir -p $$(dir $$@)
	$(CC) $(filter-out -D%,$(2)) -I$(ROOT)/build/kconfig/$(PROFILE) -include $(PLATFORM)/build_defs.h -MMD -MP -c $$< -o $$@
endef

define COMPILE_S
$(OBJDIR)/$(patsubst $(ROOT)/%,%,$(1)).o: $(1) $(TAG_STAMP) $(FLAG_STAMP)
	@mkdir -p $$(dir $$@)
	$(CC) $(filter-out -D%,$(ASM_CFLAGS)) -c $$< -o $$@
endef

# Every object depends on the tag stamp: a commit changes the banner and must
# rebuild the image, or a stale banner is certified as the committed build.
$(TAG_STAMP): FORCE
	@mkdir -p $(BUILD)
	@printf '%s' '$(BUILD_TAG)' | cmp -s - $@ 2>/dev/null || printf '%s' '$(BUILD_TAG)' > $@

# The stamp is what forces a rebuild, so it has to carry everything that changes
# the compiled bytes and nothing that does not: the flags, and the patch set the
# trees were replayed with (a patch edits headers, so a patch-only change must
# rebuild the world exactly like a flag change does).
ifneq ($(WLAN_STAMP),)
# The $(file ...) below is expanded while this makefile is read, before any
# recipe line runs: the build directory has to exist by then, not after the
# first recipe's mkdir.
$(shell mkdir -p $(BUILD))
$(WLAN_STAMP): FORCE
	@mkdir -p $(BUILD)
	@$(file >$@.tmp,$(WLAN_FLAGS_STAMP))
	@cmp -s $@.tmp $@ || mv $@.tmp $@
endif

$(FLAG_STAMP): FORCE $(WLAN_STAMP) $(CONFIG_FILE)
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(BASE_CFLAGS) $(CFLAGS)' 'asm: $(ASM_CFLAGS)' 'config: $(shell sha256sum $(CONFIG_FILE) 2>/dev/null | cut -d' ' -f1)' 'wlan: $(WLAN_FLAGS_STAMP)' \
		'usb: $(USB_CFLAGS)' 'uvc: $(UVC_CFLAGS)' \
		'rtw89-net80211: $(RTW89_BSD_CFLAGS)' \
		'rtw89-kpi: $(RTW89_KPI_CFLAGS)' 'rtw89-drv: $(RTW89_DRV_CFLAGS)' \
		'rtw89-case: $(RTW89_CASE_CFLAGS)' \
		'rtl8xxxu-net80211: $(RTL8XXXU_BSD_CFLAGS)' \
		'rtl8xxxu-kpi: $(RTL8XXXU_KPI_CFLAGS)' \
		'rtl8xxxu-drv: $(RTL8XXXU_DRV_CFLAGS)' \
		'rtl8xxxu-case: $(RTL8XXXU_CASE_CFLAGS)' \
		'rtl8xxxu-net: $(RTL8XXXU_NET_CFLAGS)' \
		'sdio-mci: $(SDIO_MCI_FLAGS_STAMP)' \
		'rtw88-net80211: $(RTW88_BSD_CFLAGS)' \
		'rtw88-kpi: $(RTW88_KPI_CFLAGS)' \
		'rtw88-sdio-port: $(RTW88_SDIO_PORT_CFLAGS)' \
		'rtw88-drv: $(RTW88_DRV_CFLAGS)' \
		'rtw88-case: $(RTW88_CASE_CFLAGS)' \
		'patches: $(PATCHES) $(ADAPTER_PATCHES)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@

$(foreach s,$(filter-out %.S,$(FIRST_SRCS)),$(eval $(call COMPILE_C,$(s),$(CFLAGS))))
$(foreach s,$(filter %.S,$(FIRST_SRCS)),$(eval $(call COMPILE_S,$(s))))
$(foreach s,$(filter-out %.S,$(VENDOR_SRCS)),$(eval $(call COMPILE_C,$(s),$(VENDORED_CFLAGS))))
$(foreach s,$(filter %.S,$(VENDOR_SRCS)),$(eval $(call COMPILE_S,$(s))))
$(foreach s,$(PRINTF_SRCS),$(eval $(call COMPILE_C,$(s),$(VENDORED_CFLAGS) $(PRINTF_CFLAGS))))
$(foreach s,$(filter-out %.S,$(WLAN_APP_SRCS)),$(eval $(call COMPILE_C,$(s),$(WLAN_APP_CFLAGS))))
$(foreach s,$(filter-out %.S,$(WLAN_SDIO_SRCS)),$(eval $(call COMPILE_C,$(s),$(WLAN_SDIO_CFLAGS))))
$(foreach s,$(filter-out %.S,$(WLAN_LKPI_SRCS)),$(eval $(call COMPILE_C,$(s),$(WLAN_LKPI_CFLAGS))))
$(foreach s,$(filter-out %.S,$(WLAN_DRV_SRCS)),$(eval $(call COMPILE_C,$(s),$(WLAN_DRV_CFLAGS))))
$(foreach s,$(filter-out %.S,$(WLAN_OBS_SRCS)),$(eval $(call COMPILE_C,$(s),$(WLAN_DRV_CFLAGS))))
$(foreach s,$(filter-out %.S,$(WLAN_LWIP_SRCS)),$(eval $(call COMPILE_C,$(s),$(WLAN_LWIP_CFLAGS))))

ifneq ($(filter iwl7260,$(CASES)),)
$(foreach s,$(filter-out %.S,$(IWL_NET80211_SRCS) $(IWL_NPORT_SRCS) $(IWL_BRIDGE_SRCS)),$(eval $(call COMPILE_C,$(s),$(IWL_BSD_CFLAGS))))
$(foreach s,$(filter-out %.S,$(IWL_LKPI_SRCS) $(IWL_FW_SRCS)),$(eval $(call COMPILE_C,$(s),$(IWL_KPI_CFLAGS))))
$(foreach s,$(filter-out %.S,$(IWL_DRV_SRCS)),$(eval $(call COMPILE_C,$(s),$(IWL_DRV_CFLAGS))))
$(foreach s,$(filter-out %.S,$(IWL_PCIE_PORT_SRCS)),$(eval $(call COMPILE_C,$(s),$(IWL_PCIE_PORT_CFLAGS))))
$(foreach s,$(filter-out %.S,$(IWL_CASE_SRCS)),$(eval $(call COMPILE_C,$(s),$(IWL_KPI_CFLAGS))))
endif

ifneq ($(filter iwl7260_net,$(CASES)),)
$(foreach s,$(filter-out %.S,$(IWL_NET_SRCS)),$(eval $(call COMPILE_C,$(s),$(IWL_NET_CFLAGS))))
endif

# The stack, the board glue and the OSAL are compiled here for every image
# that links them: the wireless images ride the same controllers, and an image
# whose USB objects have no rule would silently reuse whatever object the last
# profile left in the shared build tree.
ifneq ($(filter usb_xhci usb_ehci rtw89 rtl8xxxu,$(CASES)),)
$(foreach s,$(filter-out %.S,$(USB_STACK_SRCS) $(USB_BOARD_SRCS) $(USB_CLASS_SRCS) $(USB_CASE_SRCS)),$(eval $(call COMPILE_C,$(s),$(USB_CFLAGS))))
endif

ifneq ($(filter uvc_detect uvc_open uvc_iso_smoke uvc_stream,$(CASES)),)
$(foreach s,$(filter-out %.S,$(USB_STACK_SRCS) $(USB_BOARD_SRCS) $(UVC_ALL_SRCS)),$(eval $(call COMPILE_C,$(s),$(UVC_CFLAGS))))
endif

ifneq ($(filter rtw89,$(CASES)),)
$(foreach s,$(filter-out %.S,$(RTW89_NET80211_SRCS)),$(eval $(call COMPILE_C,$(s),$(RTW89_BSD_CFLAGS))))
$(foreach s,$(filter-out %.S,$(RTW89_LKPI_SRCS) $(RTW89_FW_SRCS)),$(eval $(call COMPILE_C,$(s),$(RTW89_KPI_CFLAGS))))
$(foreach s,$(filter-out %.S,$(RTW89_DRV_SRCS)),$(eval $(call COMPILE_C,$(s),$(RTW89_DRV_CFLAGS))))
$(foreach s,$(filter-out %.S,$(RTW89_CASE_SRCS)),$(eval $(call COMPILE_C,$(s),$(RTW89_CASE_CFLAGS))))
endif

ifneq ($(filter rtl8xxxu,$(CASES)),)
$(foreach s,$(filter-out %.S,$(RTL8XXXU_NET80211_SRCS)),$(eval $(call COMPILE_C,$(s),$(RTL8XXXU_BSD_CFLAGS))))
$(foreach s,$(filter-out %.S,$(RTL8XXXU_LKPI_SRCS) $(RTL8XXXU_FW_SRCS)),$(eval $(call COMPILE_C,$(s),$(RTL8XXXU_KPI_CFLAGS))))
$(foreach s,$(filter-out %.S,$(RTL8XXXU_DRV_SRCS)),$(eval $(call COMPILE_C,$(s),$(RTL8XXXU_DRV_CFLAGS))))
$(foreach s,$(filter-out %.S,$(RTL8XXXU_CASE_SRCS)),$(eval $(call COMPILE_C,$(s),$(RTL8XXXU_CASE_CFLAGS))))
endif

ifneq ($(filter rtl8xxxu_net,$(CASES)),)
$(foreach s,$(filter-out %.S,$(RTL8XXXU_NET_SRCS)),$(eval $(call COMPILE_C,$(s),$(RTL8XXXU_NET_CFLAGS))))
endif

# The board's SDIO socket (the imported stack over the ported MCI controller).
ifneq ($(filter sdio_mci,$(CASES)),)
$(foreach s,$(filter-out %.S,$(SDIO_MCI_APP_SRCS) $(SDIO_MCI_PORT_SRCS) $(SDIO_MCI_BOARD_SRCS)),$(eval $(call COMPILE_C,$(s),$(SDIO_MCI_CFLAGS))))
$(foreach s,$(filter-out %.S,$(SDIO_MCI_VENDOR_SRCS)),$(eval $(call COMPILE_C,$(s),$(SDIO_MCI_VENDOR_CFLAGS))))

$(SDIO_MCI_STAMP): FORCE
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(SDIO_MCI_FLAGS_STAMP)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
endif

# The RTL8821CS wireless world over that same SDIO socket: the net80211 half and
# the bridge, the linuxkpi face plus the firmware blob, the SDIO port and
# controller, the driver, then the two cases.  The port units are vendored C, so
# they get their own relaxed flag world.
ifneq ($(filter rtw88,$(CASES)),)
$(foreach s,$(filter-out %.S,$(RTW88_NET80211_SRCS)),$(eval $(call COMPILE_C,$(s),$(RTW88_BSD_CFLAGS))))
$(foreach s,$(filter-out %.S,$(RTW88_LKPI_SRCS) $(RTW88_FW_SRCS)),$(eval $(call COMPILE_C,$(s),$(RTW88_KPI_CFLAGS))))
$(foreach s,$(filter-out %.S,$(RTW88_SDIO_PORT_SRCS)),$(eval $(call COMPILE_C,$(s),$(RTW88_SDIO_PORT_CFLAGS))))
$(foreach s,$(filter-out %.S,$(RTW88_DRV_SRCS)),$(eval $(call COMPILE_C,$(s),$(RTW88_DRV_CFLAGS))))
$(foreach s,$(filter-out %.S,$(RTW88_CASE_SRCS)),$(eval $(call COMPILE_C,$(s),$(RTW88_CASE_CFLAGS))))

endif

ifneq ($(filter rtl8xxxu_netio,$(CASES)),)
$(foreach s,$(filter-out %.S,$(RTL8XXXU_NETIO_SRCS)),$(eval $(call COMPILE_C,$(s),$(RTL8XXXU_NETIO_CFLAGS))))
endif

$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

gates: $(TARGET).elf
	@echo "== gates $(BACKEND) =="
	@set -e; \
	grep -q '^#define GUEST 1' $(PLATFORM)/build_defs.h || \
		{ echo "FAIL: GUEST missing (the port would build for EL3)"; exit 1; }; \
	case "$(BACKEND)" in \
	freertos) grep -q '^#define CONFIG_BACKEND_FREERTOS 1' $(ROOT)/build/kconfig/$(PROFILE)/freewificam_config.h || \
		{ echo "FAIL: FreeRTOS backend not selected"; exit 1; } ;; \
	threadx) grep -q '^#define CONFIG_BACKEND_THREADX 1' $(ROOT)/build/kconfig/$(PROFILE)/freewificam_config.h || \
		{ echo "FAIL: ThreadX backend not selected"; exit 1; } ;; \
	esac; \
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
ifneq ($(filter rtw89,$(CASES)),)
	@set -e; \
	[ -n "$(RTW89_GATE_STAGE)" ] || \
		{ echo "FAIL: RTW89_USB_RECVBUF_SZ not found in $(RTW89_DRV)/usb.h"; exit 1; }; \
	[ "$(RTW89_GATE_STAGE)" -le "$(USB_STAGE_SIZE)" ] || \
		{ echo "FAIL: rtw89 RX $(RTW89_GATE_STAGE) > stage $(USB_STAGE_SIZE): every RX URB would be refused for good"; exit 1; }; \
	$(NM) $(TARGET).elf | awk '{print $$3}' | grep -qx lkpi_wlan_class_info || \
		{ echo "FAIL: lkpi_wlan_class_info not in the image (no vendor class hook)"; exit 1; }; \
	$(NM) $(TARGET).elf | awk '{print $$3}' | grep -qx __lkpi_module_rtw_8851bu_driver || \
		{ echo "FAIL: rtw_8851bu_driver not in .lkpi_module (registry walk would see no driver)"; exit 1; }; \
	echo "PASS: usb stage=$(RTW89_GATE_STAGE)<=$(USB_STAGE_SIZE) driver registered"
endif
ifneq ($(filter rtl8xxxu,$(CASES)),)
	@set -e; \
	[ -n "$(RTL8XXXU_GATE_STAGE)" ] || \
		{ echo "FAIL: RTL8XXXU_STAGE is empty"; exit 1; }; \
	[ "$(RTL8XXXU_GATE_STAGE)" -ge "$(RTL8XXXU_STAGE_MIN)" ] || \
		{ echo "FAIL: stage $(RTL8XXXU_GATE_STAGE) < RX URB $(RTL8XXXU_STAGE_MIN): every receive URB would be refused"; exit 1; }; \
	$(NM) $(TARGET).elf | awk '{print $$3}' | grep -qx lkpi_wlan_class_info || \
		{ echo "FAIL: lkpi_wlan_class_info not in the image (no vendor class hook)"; exit 1; }; \
	$(NM) $(TARGET).elf | awk '{print $$3}' | grep -qx __lkpi_module_rtl8xxxu_driver || \
		{ echo "FAIL: rtl8xxxu_driver not in .lkpi_module (registry walk would see no driver)"; exit 1; }; \
	$(NM) $(TARGET).elf | awk '{print $$3}' | grep -qx $(RTL8XXXU_FW_REGISTER) || \
		{ echo "FAIL: the firmware blob unit was garbage-collected"; exit 1; }; \
	echo "PASS: stage=$(RTL8XXXU_GATE_STAGE)>=rx=$(RTL8XXXU_STAGE_MIN) driver+firmware registered"
endif
ifneq ($(filter uvc_detect uvc_open uvc_iso_smoke uvc_stream,$(CASES)),)
	@set -e; \
	for s in $(UVC_GATE_SYMBOLS); do \
		$(NM) $(TARGET).elf | awk '{print $$3}' | grep -qx "$$s" || \
			{ echo "FAIL: $$s not in the image"; exit 1; }; \
	done; \
	echo "PASS: uvc engine, stream engine and video class table present"
endif
	@$(SIZE) $(TARGET).elf
	@printf 'opt: %s\ndefconfig: %s\nprofile: %s\nlwip: %s\nlevers: %s\nprof: %s\n' '$(OPT)' '$(PROFILE)' '$(SWITCH_PROFILE)' '$(LWIP)' '$(if $(strip $(LEVERS)),$(LEVERS),(none))' '$(CONFIG_GMAC_PROF)' | tee $(BUILD)/build-info-$(BACKEND).txt
	@printf 'cflags: %s\npatches: %s\n' '$(if $(strip $(PROFILE_CFLAGS)),$(PROFILE_CFLAGS),(none))' '$(if $(strip $(PATCHES)),$(PATCHES),(none))' | tee -a $(BUILD)/build-info-$(BACKEND).txt
	@printf 'build-tag: %s\n' '$(BUILD_TAG)'
	@printf 'sha256: '; sha256sum $(TARGET).bin | cut -d' ' -f1
	@printf 'size: '; stat -c '%s' $(TARGET).bin

-include $(shell find $(OBJDIR) -name '*.d' 2>/dev/null)

endif # BACKEND

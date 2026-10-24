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

ROOT := $(abspath $(CURDIR)/../..)
RTOS := $(ROOT)/rtos

AARCH64_CROSS_PATH ?= $(HOME)/workspace/env/src/xpack-aarch64-none-elf-gcc-13.2.1-1.1
CROSS_COMPILE ?= $(AARCH64_CROSS_PATH)/bin/aarch64-none-elf-
CC := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
NM := $(CROSS_COMPILE)nm
SIZE := $(CROSS_COMPILE)size
HOSTCC ?= cc

KERNEL  := $(ROOT)/third-party/FreeRTOS-Kernel
# The CMSIS-FreeRTOS adapter to compile from: the vendored pin, or the replayed
# copy when patches are declared for it (tools/patches/replay.sh).
ADAPTER := $(shell sh $(ROOT)/tools/patches/replay.sh CMSIS-FreeRTOS 2>/dev/null)
TX      := $(ROOT)/third-party/threadx
TX_PORT := $(TX)/ports/cortex_a55/gnu
TX_HOST := $(TX)/ports/linux/gnu
CMSIS   := $(ROOT)/third-party/CMSIS_6
LWIP    := $(ROOT)/third-party/lwip

BUILD := $(CURDIR)/build
BUILD_TAG ?= $(shell git -C $(ROOT) describe --always --dirty 2>/dev/null || echo unknown)
# Optimisation level. -O2 is the adopted default since the performance round
# (decisions/2026-10-25-rtos-bin-perf-profile.md): +17% uplink, -73% ping RTT,
# +16% board-local loopback, no change on the storage path and no suite
# regression. `OPT=-Og` builds the pre-round image for debug work.
OPT ?= -O2

# The adopted performance profile: the two switches that measured net-positive
# on both kernels. `PROFILE=base` builds the pre-round configuration, which is
# what an A/B re-run of the round's arms needs.
PROFILE ?= perf
ifeq ($(PROFILE),perf)
PROFILE_DEFS := -DCONFIG_GMAC_TX_CSUM=1 -DCONFIG_GMAC_RX_HWCSUM=1 \
	-DLWIP_TCPIP_CORE_LOCKING_INPUT=1
else
PROFILE_DEFS :=
endif

# Lever channel (performance rounds): a build-time -D that has to reach both the
# project's own units and the vendored trees. It exists because the stack's
# switches live in an #ifndef-guarded option table (ports/lwip/include/
# lwipopts.h) and the driver's in CONFIG_GMAC_*: a switch only exists for a
# translation unit compiled with the define. LEVERS is part of BASE_CFLAGS,
# hence of the flag stamp, so a changed lever always forces a rebuild - a lever
# can never be certified against an object built with a different set.
#
#   OPT=-O2 tools/rtos/build.sh
#   LEVERS="-DTCP_WND=131072 -DCONFIG_GMAC_RX_SCAN=5" tools/rtos/build.sh
LEVERS ?=

# Case selection: the source set and the runner's table are trimmed together,
# so a short arm builds a short image (`CASES=tick,cmsis2`).
CASES ?= tick cmsis2 net emmc
CASE_DEFS := $(foreach c,$(CASES),-DRTOS_CASE_$(shell echo $(c) | tr a-z A-Z))
CASE_SRCS := $(foreach c,$(CASES),$(RTOS)/app/cases/$(c).c)

# The storage stack's MMC card type is a compile-time key of the imported
# component; it rides on the case selection.
ifneq ($(filter emmc,$(CASES)),)
EXTRA_DEFS := -DCONFIG_FSL_SDMMC_ENABLE_MMC=1
endif

.DEFAULT_GOAL := all

.PHONY: all images check image host host-test digest clean FORCE

# A rebuild must follow a flag change: the objs depend on a stamp whose
# content is the flag set (a stale object built with other defines is the
# classic way to certify the wrong bytes).
FORCE:

all: images
check: images

images:
	@$(MAKE) --no-print-directory image BACKEND=freertos
	@$(MAKE) --no-print-directory image BACKEND=threadx

digest:
	@set -e; for t in CMSIS_6 CMSIS-FreeRTOS FreeRTOS-Kernel lwip threadx; do \
		( cd $(ROOT)/third-party/$$t && \
		  sha256sum -c --quiet ../../patches/$$t/MANIFEST.sha256 ); \
		echo "digest: $$t ok ($$(wc -l < $(ROOT)/patches/$$t/MANIFEST.sha256) files)"; \
	done

clean:
	rm -rf $(BUILD)

# ---------------------------------------------------------------------------
# Host tier (host compiler; independent of BACKEND).

HOST_BUILD := $(BUILD)/host
HOST_CASE_SRCS := \
	$(RTOS)/app/cases/cmsis2.c \
	$(RTOS)/app/runner.c \
	$(RTOS)/app/port/clock_host.c

HOST_FRTOS_SRCS := \
	$(HOST_CASE_SRCS) \
	$(RTOS)/tests/cmsis2-host/main.c \
	$(RTOS)/os/freertos/freertos_hooks.c \
	$(RTOS)/os/freertos/cmsis2_host.c \
	$(RTOS)/os/freertos/cmsis2_ext.c \
	$(KERNEL)/croutine.c $(KERNEL)/event_groups.c $(KERNEL)/list.c \
	$(KERNEL)/queue.c $(KERNEL)/stream_buffer.c $(KERNEL)/tasks.c \
	$(KERNEL)/timers.c $(KERNEL)/portable/MemMang/heap_4.c \
	$(KERNEL)/portable/ThirdParty/GCC/Posix/port.c \
	$(KERNEL)/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c \
	$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Source/cmsis_os2.c

HOST_TX_SRCS := \
	$(HOST_CASE_SRCS) \
	$(RTOS)/tests/cmsis2-host/main.c \
	$(RTOS)/os/threadx/tx_glue.c \
	$(RTOS)/os/threadx/heap.c \
	$(RTOS)/os/threadx/cmsis2_threadx.c \
	$(RTOS)/os/threadx/cmsis2_event.c \
	$(RTOS)/os/threadx/cmsis2_ext.c \
	$(RTOS)/os/threadx/cmsis2_host.c \
	$(wildcard $(TX_HOST)/src/*.c) \
	$(wildcard $(TX)/common/src/*.c)

HOST_COMMON_CFLAGS := -ggdb3 -O1 -std=c11 -Wall -Wextra -ffunction-sections \
	-fdata-sections -D_GNU_SOURCE -DRTOS_HOST_TIER -DULOG_ENABLED \
	-DRTOS_CASE_CMSIS2 \
	-DFW_BUILD_TAG='"$(BUILD_TAG)"' \
	-I$(RTOS) -I$(RTOS)/app -I$(RTOS)/os/cmsis2 \
	-I$(RTOS)/os/cmsis2/host-compat -I$(RTOS)/components/ulog/src \
	-I$(CMSIS)/CMSIS/RTOS2/Include

HOST_FRTOS_CFLAGS := $(HOST_COMMON_CFLAGS) \
	-DCMSIS2_BACKEND_FREERTOS -DFREERTOS_HOOKS_TAG='"cmsis2-conformance"' \
	-DCMSIS_device_header='"cmsis_host_device.h"' \
	-DRTOS_BACKEND_NAME='"freertos"' \
	-DRTOS_KERNEL_NAME='"FreeRTOS-Kernel V11.3.1"' \
	-I$(RTOS)/os/freertos -I$(KERNEL)/include \
	-I$(KERNEL)/portable/ThirdParty/GCC/Posix \
	-I$(KERNEL)/portable/ThirdParty/GCC/Posix/utils \
	-I$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Include

HOST_TX_CFLAGS := $(HOST_COMMON_CFLAGS) \
	-D_GNU_SOURCE -DTX_LINUX_MULTI_CORE -DTX_INCLUDE_USER_DEFINE_FILE \
	-DCMSIS2_BACKEND_THREADX -DCMSIS2_MAX_TIMERS=8 \
	-DRTOS_BACKEND_NAME='"threadx"' \
	-DRTOS_KERNEL_NAME='"ThreadX 6.5.1.202602a"' \
	-I$(RTOS)/os/threadx -I$(TX)/common/inc -I$(TX_HOST)/inc

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

ifeq ($(ADAPTER),)
$(error tools/patches/replay.sh returned no source root for CMSIS-FreeRTOS)
endif

TARGET := $(BUILD)/rtos-$(BACKEND)
OBJDIR := $(BUILD)/obj/$(BACKEND)
TAG_STAMP := $(BUILD)/build-tag.stamp
FLAG_STAMP := $(BUILD)/flags-$(BACKEND).stamp

ifeq ($(BACKEND),freertos)
KERNEL_NAME := FreeRTOS-Kernel V11.3.1
BACKEND_DEFS := -DGUEST -DCMSIS2_BACKEND_FREERTOS \
	-DCMSIS_device_header='"cmsis_board_device.h"' \
	-DEQOS_BOARD_IRQ_PRIORITY_RAW=0xd0u
BACKEND_INCLUDES := \
	-I$(RTOS)/os/freertos/board/rk3568-itx/cmsis2 \
	-I$(RTOS)/os/freertos/board/rk3568-itx \
	-I$(KERNEL)/include -I$(KERNEL)/portable/GCC/ARM_AARCH64_SRE \
	-I$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Include
BACKEND_LDFLAGS := -Wl,-u,osCriticalEnter -Wl,-u,osCriticalExit
BACKEND_VECTOR := _freertos_vector_table
BACKEND_SYMBOLS := FreeRTOS_IRQ_Handler FreeRTOS_Tick_Handler xPortStartScheduler
BACKEND_FIRST_SRCS := \
	$(RTOS)/os/freertos/board/rk3568-itx/freertos_board_glue.c \
	$(RTOS)/os/freertos/board/rk3568-itx/freertos_board_vectors.S \
	$(RTOS)/os/freertos/board/rk3568-itx/cmsis2_ext_board.c \
	$(RTOS)/os/freertos/board/rk3568-itx/cmsis2_memory_board.c
BACKEND_VENDOR_SRCS := \
	$(KERNEL)/tasks.c $(KERNEL)/list.c $(KERNEL)/queue.c \
	$(KERNEL)/event_groups.c $(KERNEL)/timers.c $(KERNEL)/stream_buffer.c \
	$(KERNEL)/portable/MemMang/heap_4.c \
	$(KERNEL)/portable/GCC/ARM_AARCH64_SRE/port.c \
	$(KERNEL)/portable/GCC/ARM_AARCH64_SRE/portASM.S \
	$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Source/cmsis_os2.c
else ifeq ($(BACKEND),threadx)
KERNEL_NAME := ThreadX 6.5.1.202602a
BACKEND_DEFS := -DEL1=1 -DTX_INCLUDE_USER_DEFINE_FILE \
	-DCONFIG_TX_TICKS_PER_SECOND=1000 -DCONFIG_TX_TIMER_STACK=4096 \
	-DCMSIS2_BACKEND_THREADX
BACKEND_INCLUDES := \
	-I$(RTOS)/os/threadx/board/rk3568-itx/cmsis2 \
	-I$(RTOS)/os/threadx \
	-I$(TX)/common/inc -I$(TX_PORT)/inc
BACKEND_LDFLAGS := -Wl,--defsym=HEAP_SIZE=4M \
	-Wl,-u,osCriticalEnter -Wl,-u,osCriticalExit
BACKEND_VECTOR := _threadx_vector_table
BACKEND_SYMBOLS := _tx_initialize_kernel_enter _tx_thread_schedule _tx_timer_interrupt __top_of_ram
BACKEND_FIRST_SRCS := \
	$(RTOS)/os/threadx/board/rk3568-itx/tx_glue.c \
	$(RTOS)/os/threadx/board/rk3568-itx/tx_vectors.S \
	$(RTOS)/os/threadx/board/rk3568-itx/heap.c \
	$(RTOS)/os/threadx/board/rk3568-itx/cmsis2/cmsis2_board.c \
	$(RTOS)/os/threadx/cmsis2_threadx.c \
	$(RTOS)/os/threadx/cmsis2_event.c \
	$(RTOS)/os/threadx/cmsis2_ext.c
BACKEND_VENDOR_SRCS := \
	$(wildcard $(TX)/common/src/*.c) \
	$(wildcard $(TX_PORT)/src/*.S) \
	$(RTOS)/components/tlsf/tlsf.c
else
$(error unknown BACKEND '$(BACKEND)': use freertos or threadx)
endif

INCLUDES := \
	-I$(RTOS) \
	-I$(RTOS)/app \
	-I$(RTOS)/os/cmsis2 \
	-I$(RTOS)/os/cmsis2/board-compat \
	-I$(RTOS)/arch/aarch64 \
	-I$(RTOS)/board/rk3568-itx \
	-I$(RTOS)/board/rk3568-itx/drivers \
	-I$(RTOS)/os/baremetal \
	-I$(RTOS)/components/ulog/src \
	-I$(RTOS)/components/tlsf \
	-I$(RTOS)/ports/lwip -I$(RTOS)/ports/lwip/include \
	-I$(LWIP)/src/include \
	-I$(RTOS)/ports/fsl_sdmmc -I$(RTOS)/ports/fsl_sdmmc/shadow \
	-I$(RTOS)/components/fsl_sdmmc/common \
	-I$(RTOS)/components/fsl_sdmmc/mmc \
	-I$(RTOS)/components/fsl_sdmmc/osa \
	-I$(CMSIS)/CMSIS/RTOS2/Include \
	-I$(CMSIS)/CMSIS/Driver/Include \
	-I$(CMSIS)/CMSIS/Core/Include/a-profile \
	$(BACKEND_INCLUDES)

BASE_CFLAGS := $(OPT) -g3 -std=c11 -Wall -Wextra \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-march=armv8-a -mgeneral-regs-only -mstrict-align -mno-outline-atomics \
	-ffunction-sections -fdata-sections \
	-DFW_BUILD_TAG='"$(BUILD_TAG)"' \
	-DRTOS_BACKEND_NAME='"$(BACKEND)"' \
	-DRTOS_KERNEL_NAME='"$(KERNEL_NAME)"' \
	-DULOG_ENABLED -DULOG_MAX_MESSAGE_LENGTH=256 \
	$(CASE_DEFS) $(EXTRA_DEFS) $(PROFILE_DEFS) $(LEVERS) $(BACKEND_DEFS) \
	$(INCLUDES)

CFLAGS := $(BASE_CFLAGS) -include stdio.h -include string.h -include strings.h \
	-include stdlib.h -D_DEFAULT_SOURCE=1
ASM_CFLAGS := $(BASE_CFLAGS)
VENDORED_CFLAGS := $(filter-out -Wextra,$(CFLAGS))
PRINTF_CFLAGS := -DPRINTF_DISABLE_SUPPORT_FLOAT -DPRINTF_DISABLE_SUPPORT_EXPONENTIAL

BASE_SRCS := \
	$(RTOS)/arch/aarch64/start.S \
	$(RTOS)/arch/aarch64/gicv3.c \
	$(RTOS)/arch/aarch64/generic_timer.c \
	$(RTOS)/arch/aarch64/mmu.c \
	$(RTOS)/arch/aarch64/cache.c \
	$(RTOS)/board/rk3568-itx/drivers/uart.c \
	$(RTOS)/board/rk3568-itx/param.c \
	$(RTOS)/board/rk3568-itx/reset.c \
	$(RTOS)/os/baremetal/printf_bind.c \
	$(RTOS)/os/baremetal/libc_stubs.c \
	$(RTOS)/os/baremetal/log.c \
	$(RTOS)/os/baremetal/tick.c \
	$(RTOS)/components/ulog/src/ulog.c

APP_SRCS := \
	$(RTOS)/app/main.c \
	$(RTOS)/app/runner.c \
	$(RTOS)/app/port/clock_board.c \
	$(CASE_SRCS)

# The net case brings the lwIP port and the GMAC drivers; the eMMC case brings
# the storage stack and the eMMC controller.
ifneq ($(filter net,$(CASES)),)
NET_APP_SRCS := \
	$(RTOS)/ports/lwip/sys_arch.c \
	$(RTOS)/ports/lwip/platform_hooks.c \
	$(RTOS)/ports/lwip/ethnetif.c
NET_STACK_SRCS := \
	$(LWIP)/src/core/init.c $(LWIP)/src/core/def.c $(LWIP)/src/core/dns.c \
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
EQOS_SRCS := \
	$(RTOS)/board/rk3568-itx/drivers/dwc_eqos.c \
	$(RTOS)/board/rk3568-itx/drivers/eqos_rk3568.c \
	$(RTOS)/board/rk3568-itx/drivers/rtl8211f.c
else
NET_APP_SRCS :=
NET_STACK_SRCS :=
EQOS_SRCS :=
endif

ifneq ($(filter emmc,$(CASES)),)
MMC_PORT_SRCS := \
	$(RTOS)/ports/fsl_sdmmc/fsl_sdmmc_osa.c \
	$(RTOS)/ports/fsl_sdmmc/sdmmc_mmc_card.c \
	$(RTOS)/ports/fsl_sdmmc/sdmmc_host_dwcmshc.c \
	$(RTOS)/board/rk3568-itx/drivers/dwc_mshc.c
MMC_VENDOR_SRCS := \
	$(RTOS)/components/fsl_sdmmc/common/fsl_sdmmc_common.c \
	$(RTOS)/components/fsl_sdmmc/mmc/fsl_mmc.c
else
MMC_PORT_SRCS :=
MMC_VENDOR_SRCS :=
endif

PRINTF_SRCS := $(RTOS)/components/printf/printf.c

FIRST_SRCS := $(BASE_SRCS) $(APP_SRCS) $(NET_APP_SRCS) $(EQOS_SRCS) \
	$(MMC_PORT_SRCS) $(BACKEND_FIRST_SRCS)
VENDOR_SRCS := $(NET_STACK_SRCS) $(MMC_VENDOR_SRCS) $(BACKEND_VENDOR_SRCS)

ALL_SRCS := $(FIRST_SRCS) $(VENDOR_SRCS) $(PRINTF_SRCS)
OBJS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(patsubst $(ROOT)/%,%,$(ALL_SRCS))))

LDFLAGS := -nostdlib -static \
	-Wl,-T,$(RTOS)/arch/aarch64/baremetal.ld \
	$(BACKEND_LDFLAGS) \
	-Wl,--gc-sections -Wl,--build-id=none \
	-Wl,-Map,$(TARGET).map \
	-lc -lgcc -lnosys

.PHONY: image gates FORCE
image: $(TARGET).bin gates

define COMPILE_C
$(OBJDIR)/$(patsubst $(ROOT)/%,%,$(1)).o: $(1) $(TAG_STAMP) $(FLAG_STAMP)
	@mkdir -p $$(dir $$@)
	$(CC) $(2) -MMD -MP -c $$< -o $$@
endef

define COMPILE_S
$(OBJDIR)/$(patsubst $(ROOT)/%,%,$(1)).o: $(1) $(TAG_STAMP) $(FLAG_STAMP)
	@mkdir -p $$(dir $$@)
	$(CC) $(ASM_CFLAGS) -c $$< -o $$@
endef

# Every object depends on the tag stamp: a commit changes the banner and must
# rebuild the image, or a stale banner is certified as the committed build.
$(TAG_STAMP): FORCE
	@mkdir -p $(BUILD)
	@printf '%s' '$(BUILD_TAG)' | cmp -s - $@ 2>/dev/null || printf '%s' '$(BUILD_TAG)' > $@

$(FLAG_STAMP): FORCE
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(BASE_CFLAGS) $(CFLAGS)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@

$(foreach s,$(filter-out %.S,$(FIRST_SRCS)),$(eval $(call COMPILE_C,$(s),$(CFLAGS))))
$(foreach s,$(filter %.S,$(FIRST_SRCS)),$(eval $(call COMPILE_S,$(s))))
$(foreach s,$(filter-out %.S,$(VENDOR_SRCS)),$(eval $(call COMPILE_C,$(s),$(VENDORED_CFLAGS))))
$(foreach s,$(filter %.S,$(VENDOR_SRCS)),$(eval $(call COMPILE_S,$(s))))
$(foreach s,$(PRINTF_SRCS),$(eval $(call COMPILE_C,$(s),$(VENDORED_CFLAGS) $(PRINTF_CFLAGS))))

$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

gates: $(TARGET).elf
	@echo "== gates $(BACKEND) =="
	@set -e; \
	case "$(BACKEND)" in \
	freertos) echo "$(BASE_CFLAGS)" | grep -q -- '-DGUEST' || \
		{ echo "FAIL: -DGUEST missing (the port would build for EL3)"; exit 1; } ;; \
	threadx) echo "$(BASE_CFLAGS)" | grep -q -- '-DEL1=1' || \
		{ echo "FAIL: -DEL1=1 missing (the port would build for EL3)"; exit 1; } ;; \
	esac; \
	n=$$(echo "$(BASE_CFLAGS)" | grep -o -- '-DCMSIS2_BACKEND_[A-Z]*' | sort -u | wc -l); \
	[ "$$n" -eq 1 ] || { echo "FAIL: $$n CMSIS2 backends selected"; exit 1; }; \
	if grep -rn 'FreeRTOS\.h\|tx_api\.h' $(RTOS)/arch $(RTOS)/board >/dev/null 2>&1; then \
		echo "FAIL: a kernel header leaked into arch/ or board/"; exit 1; fi; \
	start=$$($(NM) $(TARGET).elf | awk '$$3=="_start"{print $$1}'); \
	[ -n "$$start" ] || { echo "FAIL: _start not found"; exit 1; }; \
	[ $$(( 0x$$start )) -eq $$(( 0xa000000 )) ] || \
		{ echo "FAIL: _start=0x$$start (want 0xa000000)"; exit 1; }; \
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
	@$(SIZE) $(TARGET).elf
	@printf 'opt: %s\nprofile: %s\nlevers: %s\n' '$(OPT)' '$(PROFILE)' '$(if $(strip $(LEVERS)),$(LEVERS),(none))' | tee $(BUILD)/build-info-$(BACKEND).txt
	@printf 'build-tag: %s\n' '$(BUILD_TAG)'
	@printf 'sha256: '; sha256sum $(TARGET).bin | cut -d' ' -f1
	@printf 'size: '; stat -c '%s' $(TARGET).bin

-include $(shell find $(OBJDIR) -name '*.d' 2>/dev/null)

endif # BACKEND
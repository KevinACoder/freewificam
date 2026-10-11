# @file    Makefile
# @brief   Build the ThreadX board SDIO image for RK3568: the imported SDIO
#          protocol layer, this project's card service and host glue, the
#          controller driver, and the suite's own cases and console command.
# @author  zhugengyu
# @date    12.10.2026
# @design  self-contained project makefile - the WLAN image's structure
#          (tests/board/threadx-rk3568-rtw88/Makefile) with the wireless,
#          supplicant, TCP/IP and USB domains removed and one added: the SDIO
#          line.  The image exists on its own because the SDIO line is the
#          comparison the wireless lines are read against - a different bus,
#          the same kernel, the same shell - and because a failure here must
#          leave every other image's board rounds untouched.
#
# Product: build/threadx-rk3568-sdio.bin - the flat raw binary staged as
# rtos.bin (the lab profile's deploy name) and loaded with
# `tftp 0xa000000 rtos.bin`, entered with `go 0xa000000`.
#
# Test execution order = link order = the TESTS_SRCS list below.

ROOT := $(abspath $(CURDIR)/../../..)
TX := $(ROOT)/third-party/threadx
TX_PORT := $(TX)/ports/cortex_a55/gnu
BOARD_CMSIS2 := $(ROOT)/os/threadx/board/rk3568-itx/cmsis2
CMSIS := $(ROOT)/third-party/CMSIS_6
CHERRYSH := $(ROOT)/components/cherrysh
PORT := $(ROOT)/ports/cherrysh
COMMON := $(ROOT)/tests/board/common

# The SDIO domain: the imported protocol layer (byte-faithful), this project's
# OS abstraction, card service, host glue and seam implementation, the seam's
# own dispatchers, and the board controller driver.
FSL := $(ROOT)/components/fsl_sdmmc
SDIOPORT := $(ROOT)/ports/fsl_sdmmc
LKPI := $(ROOT)/components/linuxkpi
BOARD_DRV := $(ROOT)/board/rk3568-itx/drivers

SUITE_NAME := threadx-rk3568-sdio

AARCH64_CROSS_PATH ?= $(HOME)/workspace/env/src/xpack-aarch64-none-elf-gcc-13.2.1-1.1
CROSS_COMPILE ?= $(AARCH64_CROSS_PATH)/bin/aarch64-none-elf-
CC := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
NM := $(CROSS_COMPILE)nm
READELF := $(CROSS_COMPILE)readelf
SIZE := $(CROSS_COMPILE)size
HOSTCC ?= cc

BUILD_TAG ?= $(shell git -C $(ROOT) describe --always --dirty 2>/dev/null || echo unknown)

BUILD := build
TARGET := $(BUILD)/$(SUITE_NAME)
TAG_STAMP := $(BUILD)/build-tag.stamp

INCLUDES := \
	-I$(ROOT) \
	-I$(ROOT)/os/cmsis2 \
	-I$(ROOT)/arch/aarch64 \
	-I$(ROOT)/board/rk3568-itx \
	-I$(BOARD_DRV) \
	-I$(ROOT)/os/baremetal \
	-I$(ROOT)/components/ulog/src \
	-DULOG_ENABLED -DULOG_MAX_MESSAGE_LENGTH=256 \
	-I$(CHERRYSH) \
	-I$(PORT) \
	-I$(ROOT)/components/tlsf \
	-I$(COMMON) \
	-I$(CMSIS)/CMSIS/RTOS2/Include

TX_CFLAGS := -I$(BOARD_CMSIS2) \
	-I$(ROOT)/os/threadx \
	-I$(TX)/common/inc \
	-I$(TX_PORT)/inc \
	-DTX_INCLUDE_USER_DEFINE_FILE -DEL1=1 \
	-DCONFIG_TX_TICKS_PER_SECOND=1000 -DCONFIG_TX_TIMER_STACK=4096

OPT ?= -Og
BASE_CFLAGS := $(OPT) -g3 -std=c11 -Wall -Wextra \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-march=armv8-a -mgeneral-regs-only -mstrict-align -mno-outline-atomics \
	-ffunction-sections -fdata-sections \
	-DFW_BUILD_TAG='"$(BUILD_TAG)"' \
	-DBOARD_SUITE_NAME='"$(SUITE_NAME)"' \
	-DCMSIS2_BACKEND_THREADX \
	-DSHELL_PORT_HOST='"rk3568"'

CFLAGS := $(BASE_CFLAGS) -include $(ROOT)/os/baremetal/minilibc.h
ASM_CFLAGS := $(BASE_CFLAGS)

# The imported stacks' sources compile at the -Wall level the upstream
# projects themselves use (this project does not own their bytes).
VENDORED_CFLAGS := $(filter-out -Wextra,$(CFLAGS))

# --- SDIO domain flag sets ---------------------------------------------------
#
# The imported protocol layer's include roots, plus the seam's. The port files
# are this project's and stay warning-honest; the imported units keep their own
# level.
SDIO_INCLUDES := \
	-I$(FSL)/common -I$(FSL)/sdio -I$(FSL)/osa \
	-I$(SDIOPORT) -I$(SDIOPORT)/shadow \
	-I$(LKPI)/sdio

SDIO_VENDOR_CFLAGS := $(filter-out -Wextra,$(CFLAGS)) \
	-DCONFIG_FSL_SDMMC_ENABLE_SDIO=1

SDIO_PORT_CFLAGS := $(CFLAGS) \
	-DCONFIG_FSL_SDMMC_ENABLE_SDIO=1 \
	-DLKPI_ABS_PLATFORM_PROVIDED \
	-DCMSIS_device_header='"cmsis_os2.h"' \
	-I$(LKPI) -I$(LKPI)/include

# The suite's own cases and console command reach the card through the seam, so
# they carry the seam's include roots and nothing of the imported stack.
SDIO_CASE_SRCS := \
	$(CURDIR)/tests/sdio_enum.c \
	$(CURDIR)/tests/sdio_card_enable.c \
	$(CURDIR)/tests/sdio_datapath.c \
	$(CURDIR)/tests/sdio_pump.c \
	$(CURDIR)/tests/sdio_cmds.c

SDIO_CMD_CFLAGS := $(CFLAGS) \
	-DLKPI_ABS_PLATFORM_PROVIDED \
	-DCMSIS_device_header='"cmsis_os2.h"' \
	-I$(LKPI)/sdio -I$(LKPI) -I$(LKPI)/include

# --gc-sections drops everything nothing references. The pins below are the
# entry points the start-up order calls that the linker cannot see
# transitively (the module walker, the board backend registration).
HEAP_SIZE ?= 4M

LDFLAGS := -nostdlib -static \
	-Wl,-T,$(ROOT)/arch/aarch64/baremetal.ld \
	-Wl,--defsym=HEAP_SIZE=$(HEAP_SIZE) \
	-Wl,--gc-sections \
	-Wl,-u,osCriticalEnter \
	-Wl,-u,osCriticalExit \
	-Wl,-u,lkpi_sdio_fsl_install \
	-Wl,-u,lkpi_sdio_be_card_init \
	-Wl,--build-id=none \
	-Wl,-Map,$(TARGET).map

# --- compile set -------------------------------------------------------------

BASE_SRCS := \
	$(ROOT)/arch/aarch64/start.S \
	$(ROOT)/arch/aarch64/gicv3.c \
	$(ROOT)/arch/aarch64/generic_timer.c \
	$(ROOT)/arch/aarch64/mmu.c \
	$(ROOT)/arch/aarch64/cache.c \
	$(ROOT)/board/rk3568-itx/drivers/uart.c \
	$(ROOT)/board/rk3568-itx/param.c \
	$(ROOT)/board/rk3568-itx/reset.c \
	$(ROOT)/os/baremetal/memops.c \
	$(ROOT)/os/baremetal/minilibc.c \
	$(ROOT)/os/baremetal/log.c \
	$(ROOT)/components/ulog/src/ulog.c \
	$(ROOT)/os/baremetal/tick.c

TX_SRCS := \
	$(wildcard $(TX)/common/src/*.c) \
	$(wildcard $(TX_PORT)/src/*.S) \
	$(ROOT)/os/threadx/board/rk3568-itx/tx_glue.c \
	$(ROOT)/os/threadx/board/rk3568-itx/tx_vectors.S \
	$(ROOT)/os/threadx/board/rk3568-itx/heap.c \
	$(ROOT)/os/threadx/board/rk3568-itx/cmsis2/cmsis2_board.c \
	$(ROOT)/os/threadx/cmsis2_threadx.c \
	$(ROOT)/os/threadx/cmsis2_ext.c

COMMON_SRCS := \
	$(COMMON)/board_suite.c \
	$(COMMON)/board_select.c

SHELL_SRCS := \
	$(CURDIR)/main.c \
	$(PORT)/shell_port.c \
	$(PORT)/console_board.c \
	$(PORT)/board_cmds.c

# Execution order = this list's order (link order for the BOARD_TESTS section).
TESTS_SRCS := \
	$(COMMON)/cases/shell_up.c \
	$(CURDIR)/tests/sdio_enum.c \
	$(CURDIR)/tests/sdio_card_enable.c \
	$(CURDIR)/tests/sdio_datapath.c \
	$(CURDIR)/tests/sdio_pump.c \
	$(CURDIR)/tests/sdio_cmds.c

# The imported protocol layer and the board controller driver: the two units the
# SDIO build set compiles, and the controller.
SDIO_VENDOR_SRCS := \
	$(FSL)/common/fsl_sdmmc_common.c \
	$(FSL)/sdio/fsl_sdio.c \
	$(BOARD_DRV)/dwc_mmc.c

# This project's side of the line.
SDIO_PORT_SRCS := \
	$(SDIOPORT)/fsl_sdmmc_osa.c \
	$(SDIOPORT)/sdmmc_card.c \
	$(SDIOPORT)/sdmmc_host_dwmmc.c \
	$(SDIOPORT)/lkpi_sdio_fsl.c \
	$(LKPI)/sdio/lkpi_sdio_be.c

VENDORED_SRCS := \
	$(CHERRYSH)/chry_shell.c \
	$(CHERRYSH)/cherryrl/chry_readline.c \
	$(CHERRYSH)/builtin/clear.c \
	$(CHERRYSH)/builtin/help.c \
	$(CHERRYSH)/builtin/shsize.c

ALLOC_SRCS := $(ROOT)/components/tlsf/tlsf.c

PRINTF_SRCS := $(ROOT)/components/printf/printf.c
PRINTF_CFLAGS := -DPRINTF_DISABLE_SUPPORT_FLOAT -DPRINTF_DISABLE_SUPPORT_EXPONENTIAL

# the board's C-library names the port files reach (memset family over the
# freestanding subset, printf through the stamped sink)
LIBCBIND_SRCS := $(ROOT)/os/threadx/board/rk3568-itx/libc_bindings.c

ALL_SRCS := $(BASE_SRCS) $(TX_SRCS) $(COMMON_SRCS) $(SHELL_SRCS) $(TESTS_SRCS) \
	$(SDIO_VENDOR_SRCS) $(SDIO_PORT_SRCS) \
	$(VENDORED_SRCS) $(ALLOC_SRCS) $(PRINTF_SRCS) $(LIBCBIND_SRCS)

OBJS := $(addprefix $(BUILD)/obj/,$(addsuffix .o,$(patsubst $(ROOT)/%,%,$(ALL_SRCS))))

.PHONY: all check guards clean FORCE
all: $(TARGET).bin check

define COMPILE_RULE
$(BUILD)/obj/$(patsubst $(ROOT)/%,%,$(1)).o: $(1) $(TAG_STAMP)
	@mkdir -p $$(dir $$@)
	@echo "  CC $$<"
	@$(CC) $(if $(filter %.S,$(1)),$(ASM_CFLAGS),$(if $(filter $(1),$(VENDORED_SRCS) $(SDIO_VENDOR_SRCS)),$(if $(filter $(1),$(SDIO_VENDOR_SRCS)),$(SDIO_VENDOR_CFLAGS),$(VENDORED_CFLAGS)),$(if $(filter $(1),$(SDIO_PORT_SRCS)),$(SDIO_PORT_CFLAGS),$(CFLAGS)))) $(2) $(INCLUDES) $(if $(filter $(1),$(SDIO_VENDOR_SRCS) $(SDIO_PORT_SRCS)),$(SDIO_INCLUDES),) $(if $(filter %.S,$(1)),,-MMD -MP) -c $$< -o $$@
endef

$(foreach s,$(PRINTF_SRCS),$(eval $(call COMPILE_RULE,$(s),$(PRINTF_CFLAGS))))
# minilibc.c carries its own rule below (the completion block); leaving it out
# of this loop keeps make from warning about a second recipe for its object.
$(foreach s,$(filter-out $(ROOT)/os/baremetal/minilibc.c,$(BASE_SRCS)),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(TX_SRCS),$(eval $(call COMPILE_RULE,$(s),$(TX_CFLAGS))))
$(foreach s,$(COMMON_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(filter-out $(SDIO_CASE_SRCS),$(SHELL_SRCS) $(TESTS_SRCS)),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(SDIO_CASE_SRCS),$(eval $(call COMPILE_RULE,$(s),$(SDIO_CMD_CFLAGS))))
$(foreach s,$(VENDORED_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(ALLOC_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
# the freestanding libc completion block (abs and friends) is part of this
# image's C surface: the imported units reference the symbols
$(eval $(call COMPILE_RULE,$(ROOT)/os/baremetal/minilibc.c,-DCONFIG_LIBC_COMPLETION))
$(foreach s,$(SDIO_VENDOR_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(SDIO_PORT_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(LIBCBIND_SRCS),$(eval $(call COMPILE_RULE,$(s),-I$(ROOT)/os/threadx)))

$(TAG_STAMP): FORCE
	@mkdir -p $(BUILD)
	@printf '%s' '$(BUILD_TAG)' | cmp -s - $@ 2>/dev/null || printf '%s' '$(BUILD_TAG)' > $@

$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# --- build-side gates ------------------------------------------------------

guards:
	@echo "== build guards =="
	@echo "$(TX_CFLAGS)" | grep -q -- '-DEL1=1' \
		|| { echo "FAIL: -DEL1=1 missing from TX_CFLAGS"; exit 1; }
	@echo "$(TX_CFLAGS)" | grep -q -- '-DTX_INCLUDE_USER_DEFINE_FILE' \
		|| { echo "FAIL: -DTX_INCLUDE_USER_DEFINE_FILE missing from TX_CFLAGS"; exit 1; }
	@[ "$$(basename $(CURDIR))" = "$(SUITE_NAME)" ] \
		|| { echo "FAIL: SUITE_NAME ($(SUITE_NAME)) is not the directory name"; exit 1; }
	@if echo "$(ALL_SRCS)" | tr ' ' '\n' | grep -qE "^$(ROOT)/os/threadx/(cmsis2_host|heap|tx_glue)\.c$$"; then \
		echo "FAIL: a host-side os/threadx file entered the compile set"; exit 1; \
	fi
	@echo "PASS: port keys present, CMSIS2 flavour in effect, suite name matches, host files out"

check: guards $(TARGET).elf
	@echo "== symbol contract =="
	@set -e; \
	start=$$($(NM) $(TARGET).elf | awk '$$3=="_start"{print $$1}'); \
	[ -n "$$start" ] || { echo "FAIL: _start not found"; exit 1; }; \
	[ $$(( 0x$$start )) -eq $$(( 0xa000000 )) ] \
		|| { echo "FAIL: _start=0x$$start (want 0xa000000)"; exit 1; }; \
	for s in _tx_initialize_kernel_enter osKernelStart osThreadNew osDelay \
	         cmsis2_tx_application_define pvPortMalloc tlsf_malloc \
	         shell_port_init chry_shell_init board_suite_run \
	         SDIO_CfgInitialize SDMMCHOST_Init SDIO_IO_Read_Extended \
	         SDIO_IO_Write_Extended SDIO_SetBlockSize SDIO_GetPendingInterrupt \
	         dwc_mmc_init dwc_mmc_transfer dwc_mmc_board_cfg \
	         lkpi_sdio_fsl_install lkpi_sdio_be_card_init sdmmc_card_start; do \
		$(NM) $(TARGET).elf | awk '{print $$3}' | grep -qx "$$s" \
			|| { echo "FAIL: $$s not in the image"; exit 1; }; \
	done; \
	undef=$$($(NM) -u $(TARGET).elf); \
	[ -z "$$undef" ] || { echo "FAIL: undefined symbols:"; echo "$$undef"; exit 1; }; \
	echo "PASS: _start=0x$$start, SDIO/kernel/board symbols present, 0 undefined"
	@echo "== case-name reconciliation (image / suite.mk / judge) =="
	@sh $(COMMON)/check-case-list.sh $(TARGET).elf $(CURDIR)/suite.mk $(CURDIR)/judge.py $(OBJCOPY) $(READELF) $(BUILD)
	@$(SIZE) $(TARGET).elf
	@echo "build-tag: $(BUILD_TAG)"

# Header dependencies: -MMD -MP writes one .d per object, so a header
# edit rebuilds every object that included it (a stale object would
# otherwise be flashed as the current build).
-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

clean:
	rm -rf $(BUILD)

# @file    Makefile
# @brief   Build the CherrySH board image (cherrysh.bin) for RK3568.
# @author  zhugengyu
# @date    07.10.2026
# @design  self-contained project makefile - same shape as the bring-up smoke
#          (tests/board/freertos-rk3568-smoke/Makefile:1-16) plus the
#          CMSIS-RTOS2 slice; the include path picks the CMSIS-RTOS2 flavour of
#          the board configuration.
#
# Product: build/cherrysh.bin - the flat raw binary loaded with
# `tftp 0xa000000 cherrysh.bin` and entered with `go 0xa000000`.
#
# Beyond the smoke's gates this build checks the command tables' density:
# the shell core iterates FSymTab/VSymTab as flat arrays with a fixed stride,
# so any padding between entries would hide every entry after the first
# (measured on the host build). The check asserts the section byte counts
# equal entries x sizeof(entry).

ROOT := $(abspath $(CURDIR)/../../..)
KERNEL := $(ROOT)/third-party/FreeRTOS-Kernel
CMSIS := $(ROOT)/third-party/CMSIS_6
BOARD_CFG := $(ROOT)/os/freertos/board/rk3568-itx/cmsis2
BOARD_COMPAT := $(ROOT)/os/cmsis2/board-compat
CHERRYSH := $(ROOT)/components/cherrysh
PORT := $(ROOT)/ports/cherrysh

# The CMSIS-FreeRTOS adapter compiles from the patch-replayed tree (the
# handle-width fix is load-bearing on 64-bit targets); the tool prints the
# vendored pin when no patch is declared.
ADAPTER := $(shell sh $(ROOT)/tools/patches/replay.sh CMSIS-FreeRTOS)
ifeq ($(ADAPTER),)
$(error tools/patches/replay.sh returned no source root for CMSIS-FreeRTOS)
endif

AARCH64_CROSS_PATH ?= $(HOME)/workspace/env/src/xpack-aarch64-none-elf-gcc-13.2.1-1.1
CROSS_COMPILE ?= $(AARCH64_CROSS_PATH)/bin/aarch64-none-elf-
CC := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
NM := $(CROSS_COMPILE)nm
SIZE := $(CROSS_COMPILE)size

BUILD_TAG ?= $(shell git -C $(ROOT) describe --always --dirty 2>/dev/null || echo unknown)

BUILD := build
TARGET := $(BUILD)/cherrysh

# The board configuration is found by name (FreeRTOS.h includes it): the
# CMSIS-RTOS2 flavour directory comes first and the plain board directory is
# NOT on the path, so exactly one configuration is ever visible.
INCLUDES := \
	-I$(BOARD_CFG) \
	-I$(BOARD_COMPAT) \
	-I$(KERNEL)/include \
	-I$(KERNEL)/portable/GCC/ARM_AARCH64_SRE \
	-I$(CMSIS)/CMSIS/RTOS2/Include \
	-I$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Include \
	-I$(ROOT) \
	-I$(ROOT)/arch/aarch64 \
	-I$(ROOT)/board/rk3568-itx \
	-I$(ROOT)/board/rk3568-itx/drivers \
	-I$(ROOT)/os/baremetal \
	-I$(CHERRYSH) \
	-I$(PORT)

BASE_CFLAGS := -Og -g3 -std=c11 -Wall -Wextra \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-march=armv8-a -mgeneral-regs-only -mstrict-align -mno-outline-atomics \
	-ffunction-sections -fdata-sections \
	-DGUEST -DFW_BUILD_TAG='"$(BUILD_TAG)"' \
	-DCMSIS2_BACKEND_FREERTOS \
	-DSHELL_PORT_HOST='"rk3568"'

# The freestanding libc surface is declared everywhere it is used: the vendored
# shell core calls strnlen without a prototype of its own, and this image's
# libc is os/baremetal. Assembly files do not take the C header.
CFLAGS := $(BASE_CFLAGS) -include $(ROOT)/os/baremetal/minilibc.h
ASM_CFLAGS := $(BASE_CFLAGS)

# The vendored shell core is byte-identical upstream and cannot be edited, and
# it does trip -Wextra's sign-compare in one builtin. Its translation units are
# therefore compiled at the -Wall level the upstream project itself uses; the
# project-authored files stay at -Wextra.
VENDORED_CFLAGS := $(filter-out -Wextra,$(CFLAGS))

VENDORED_SRCS := \
	$(CHERRYSH)/chry_shell.c \
	$(CHERRYSH)/cherryrl/chry_readline.c \
	$(CHERRYSH)/builtin/clear.c \
	$(CHERRYSH)/builtin/help.c \
	$(CHERRYSH)/builtin/shsize.c

LDFLAGS := -nostdlib -static \
	-Wl,-T,$(ROOT)/arch/aarch64/baremetal.ld \
	-Wl,--gc-sections \
	-Wl,--build-id=none \
	-Wl,-Map,$(TARGET).map

APP_SRCS := \
	$(CURDIR)/main.c \
	$(ROOT)/os/freertos/board/rk3568-itx/freertos_board_glue.c \
	$(ROOT)/os/freertos/board/rk3568-itx/freertos_board_vectors.S \
	$(ROOT)/arch/aarch64/start.S \
	$(ROOT)/arch/aarch64/gicv3.c \
	$(ROOT)/arch/aarch64/generic_timer.c \
	$(ROOT)/arch/aarch64/mmu.c \
	$(ROOT)/arch/aarch64/cache.c \
	$(ROOT)/board/rk3568-itx/drivers/uart.c \
	$(ROOT)/os/baremetal/memops.c \
	$(ROOT)/os/baremetal/minilibc.c \
	$(PORT)/shell_port.c \
	$(PORT)/console_board.c \
	$(CHERRYSH)/chry_shell.c \
	$(CHERRYSH)/cherryrl/chry_readline.c \
	$(CHERRYSH)/builtin/clear.c \
	$(CHERRYSH)/builtin/help.c \
	$(CHERRYSH)/builtin/shsize.c

KERNEL_SRCS := \
	$(KERNEL)/tasks.c \
	$(KERNEL)/list.c \
	$(KERNEL)/queue.c \
	$(KERNEL)/timers.c \
	$(KERNEL)/event_groups.c \
	$(KERNEL)/stream_buffer.c \
	$(KERNEL)/portable/MemMang/heap_4.c \
	$(KERNEL)/portable/GCC/ARM_AARCH64_SRE/port.c \
	$(KERNEL)/portable/GCC/ARM_AARCH64_SRE/portASM.S \
	$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Source/cmsis_os2.c

ALL_SRCS := $(APP_SRCS) $(KERNEL_SRCS)
# The vendored formatter engine (components/printf, mpaland): the freestanding
# image formats only through this engine, bound in os/threadx... os/baremetal/
# minilibc.c. Float and exponential rendering are compiled out - the build is
# -mgeneral-regs-only and has no FP state.
PRINTF_SRCS := $(ROOT)/components/printf/printf.c
PRINTF_CFLAGS := -DPRINTF_DISABLE_SUPPORT_FLOAT -DPRINTF_DISABLE_SUPPORT_EXPONENTIAL
ALL_SRCS += $(PRINTF_SRCS)

OBJS := $(addprefix $(BUILD)/obj/,$(addsuffix .o,$(basename $(notdir $(ALL_SRCS)))))

.PHONY: all check guards clean
all: $(TARGET).bin check

define COMPILE_RULE
$(BUILD)/obj/$(basename $(notdir $(1))).o: $(1)
	@mkdir -p $(BUILD)/obj
	$(CC) $(if $(filter %.S,$(1)),$(ASM_CFLAGS),$(if $(filter $(1),$(VENDORED_SRCS)),$(VENDORED_CFLAGS),$(CFLAGS))) $(INCLUDES) -c $$< -o $$@
endef

$(foreach s,$(PRINTF_SRCS),$(eval $(call COMPILE_RULE,$(s),$(PRINTF_CFLAGS))))
$(foreach s,$(ALL_SRCS),$(eval $(call COMPILE_RULE,$(s))))

$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# --- build-side gates ------------------------------------------------------

guards:
	@echo "== build guards =="
	@echo "$(CFLAGS)" | grep -q -- '-DGUEST' \
		|| { echo "FAIL: -DGUEST missing from CFLAGS"; exit 1; }
	@if grep -E '^[[:space:]]*#[[:space:]]*define[[:space:]]+(CMSIS_device_header|configCPU_CLOCK_HZ)' \
		$(ROOT)/os/freertos/board/rk3568-itx/FreeRTOSConfig.h >/dev/null 2>&1; then \
		echo "FAIL: host-only key in the plain board configuration"; exit 1; \
	fi
	@if grep -rn 'FreeRTOS\.h\|task\.h\|portmacro\.h' \
		$(ROOT)/arch/aarch64 $(ROOT)/board/rk3568-itx >/dev/null 2>&1; then \
		echo "FAIL: a kernel header leaked into arch/ or board/"; exit 1; \
	fi
	@if grep -rn 'tx_[a-z]\|pthread_\|xTaskCreate' $(PORT)/*.c >/dev/null 2>&1; then \
		echo "FAIL: a kernel API leaked into the shell port"; exit 1; \
	fi
	@echo "PASS: -DGUEST present, configurations clean, arch/board/port free of kernel headers and APIs"

check: guards $(TARGET).elf
	@echo "== symbol contract =="
	@set -e; \
	start=$$($(NM) $(TARGET).elf | awk '$$3=="_start"{print $$1}'); \
	[ $$(( 0x$$start )) -eq $$(( 0xa000000 )) ] \
		|| { echo "FAIL: _start=0x$$start (want 0xa000000)"; exit 1; }; \
	vec=$$($(NM) $(TARGET).elf | awk '$$3=="_freertos_vector_table"{print $$1}'); \
	[ $$(( 0x$$vec % 2048 )) -eq 0 ] \
		|| { echo "FAIL: vector table at 0x$$vec is not 2KB aligned"; exit 1; }; \
	for s in FreeRTOS_IRQ_Handler FreeRTOS_SWI_Handler FreeRTOS_Tick_Handler xPortStartScheduler osKernelStart osThreadNew; do \
		$(NM) $(TARGET).elf | awk '{print $$3}' | grep -qx "$$s" \
			|| { echo "FAIL: $$s not in the image"; exit 1; }; \
	done; \
	undef=$$($(NM) -u $(TARGET).elf); \
	[ -z "$$undef" ] || { echo "FAIL: undefined symbols:"; echo "$$undef"; exit 1; }; \
	echo "PASS: _start=0x$$start, vectors=0x$$vec (2KB aligned), handlers + CMSIS entry points present, 0 undefined"
	@echo "== command table density =="
	@set -e; \
	fs=$$($(NM) $(TARGET).elf | awk '$$3=="__fsymtab_end"{print $$1}'); \
	fs0=$$($(NM) $(TARGET).elf | awk '$$3=="__fsymtab_start"{print $$1}'); \
	vs=$$($(NM) $(TARGET).elf | awk '$$3=="__vsymtab_end"{print $$1}'); \
	vs0=$$($(NM) $(TARGET).elf | awk '$$3=="__vsymtab_start"{print $$1}'); \
	fsz=$$(( 0x$$fs - 0x$$fs0 )); vsz=$$(( 0x$$vs - 0x$$vs0 )); \
	echo "fsymtab bytes=$$fsz (expect 4*40=160), vsymtab bytes=$$vsz (expect 2*24=48)"; \
	[ $$fsz -eq 160 ] || { echo "FAIL: FSymTab is not dense"; exit 1; }; \
	[ $$vsz -eq 48 ] || { echo "FAIL: VSymTab is not dense"; exit 1; }; \
	echo "PASS: command and variable tables dense (no inter-entry padding)"
	@$(SIZE) $(TARGET).elf
	@echo "build-tag: $(BUILD_TAG)"

clean:
	rm -rf $(BUILD)

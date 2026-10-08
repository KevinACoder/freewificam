# @file    Makefile
# @brief   Build the FreeRTOS RK3568 bring-up smoke image (freertos.bin).
# @author  zhugengyu
# @date    07.10.2026
# @design  self-contained project makefile - same shape as the host smoke
#          projects (tests/host/freertos-cmsis2-smoke/Makefile:1-45); the
#          root-Makefile mechanism is not in place yet, so this project
#          carries its own build.
#
# Product: build/freertos.bin - the flat raw binary loaded with
# `tftp 0xa000000 freertos.bin` and entered with `go 0xa000000`.
#
# The image is freestanding (-nostdlib): the C library surface it links
# against is os/baremetal (memops + the printf integer subset).
#
# `make check` runs the build-side gates: the board configuration must carry
# no host-only key, the arch/board layers must include no kernel header, the
# build must carry -DGUEST (the port's EL is a build-time decision), and the
# linked image must satisfy the symbol contract (_start at the load address,
# the runtime vector table 2KB-aligned, the port's handlers present, nothing
# unresolved).

ROOT := $(abspath $(CURDIR)/../../..)
KERNEL := $(ROOT)/third-party/FreeRTOS-Kernel
BOARD_CFG := $(ROOT)/os/freertos/board/rk3568-itx

AARCH64_CROSS_PATH ?= $(HOME)/workspace/env/src/xpack-aarch64-none-elf-gcc-13.2.1-1.1
CROSS_COMPILE ?= $(AARCH64_CROSS_PATH)/bin/aarch64-none-elf-
CC := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
NM := $(CROSS_COMPILE)nm
SIZE := $(CROSS_COMPILE)size

BUILD_TAG ?= $(shell git -C $(ROOT) describe --always --dirty 2>/dev/null || echo unknown)

BUILD := build
TARGET := $(BUILD)/freertos

INCLUDES := \
	-I$(BOARD_CFG) \
	-I$(KERNEL)/include \
	-I$(KERNEL)/portable/GCC/ARM_AARCH64_SRE \
	-I$(ROOT)/arch/aarch64 \
	-I$(ROOT)/board/rk3568-itx \
	-I$(ROOT)/board/rk3568-itx/drivers \
	-I$(ROOT)/os/baremetal

# -DGUEST is load-bearing: without it the port builds for EL3, and on this
# board the first yield executes SMC into firmware - a silent death.
CFLAGS := -Og -g3 -std=c11 -Wall -Wextra \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-march=armv8-a -mgeneral-regs-only -mstrict-align -mno-outline-atomics \
	-DGUEST -DFW_BUILD_TAG='"$(BUILD_TAG)"'

LDFLAGS := -nostdlib -static \
	-Wl,-T,$(ROOT)/arch/aarch64/baremetal.ld \
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
	$(ROOT)/os/baremetal/minilibc.c

KERNEL_SRCS := \
	$(KERNEL)/tasks.c \
	$(KERNEL)/list.c \
	$(KERNEL)/queue.c \
	$(KERNEL)/portable/MemMang/heap_4.c \
	$(KERNEL)/portable/GCC/ARM_AARCH64_SRE/port.c \
	$(KERNEL)/portable/GCC/ARM_AARCH64_SRE/portASM.S

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
	$(CC) $(CFLAGS) $(INCLUDES) -c $$< -o $$@
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
		$(BOARD_CFG)/FreeRTOSConfig.h >/dev/null 2>&1; then \
		echo "FAIL: host-only key in the board configuration"; exit 1; \
	fi
	@if grep -rn 'FreeRTOS\.h\|task\.h\|portmacro\.h' \
		$(ROOT)/arch/aarch64 $(ROOT)/board/rk3568-itx >/dev/null 2>&1; then \
		echo "FAIL: a kernel header leaked into arch/ or board/"; exit 1; \
	fi
	@echo "PASS: -DGUEST present, board config free of host keys, arch/board free of kernel headers"

check: guards $(TARGET).elf
	@echo "== symbol contract =="
	@set -e; \
	start=$$($(NM) $(TARGET).elf | awk '$$3=="_start"{print $$1}'); \
	[ -n "$$start" ] || { echo "FAIL: _start not found"; exit 1; }; \
	[ $$(( 0x$$start )) -eq $$(( 0xa000000 )) ] \
		|| { echo "FAIL: _start=0x$$start (want 0xa000000)"; exit 1; }; \
	vec=$$($(NM) $(TARGET).elf | awk '$$3=="_freertos_vector_table"{print $$1}'); \
	[ -n "$$vec" ] || { echo "FAIL: _freertos_vector_table not found"; exit 1; }; \
	[ $$(( 0x$$vec % 2048 )) -eq 0 ] \
		|| { echo "FAIL: vector table at 0x$$vec is not 2KB aligned"; exit 1; }; \
	for s in FreeRTOS_IRQ_Handler FreeRTOS_SWI_Handler FreeRTOS_Tick_Handler xPortStartScheduler; do \
		$(NM) $(TARGET).elf | awk '{print $$3}' | grep -qx "$$s" \
			|| { echo "FAIL: $$s not in the image"; exit 1; }; \
	done; \
	undef=$$($(NM) -u $(TARGET).elf); \
	[ -z "$$undef" ] || { echo "FAIL: undefined symbols:"; echo "$$undef"; exit 1; }; \
	echo "PASS: _start=0x$$start, vectors=0x$$vec (2KB aligned), port handlers present, 0 undefined"
	@$(SIZE) $(TARGET).elf
	@echo "build-tag: $(BUILD_TAG)"

clean:
	rm -rf $(BUILD)

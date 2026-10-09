# @file    Makefile
# @brief   Build the ThreadX RK3568 bring-up smoke image (threadx-rk3568.bin).
# @author  zhugengyu
# @date    07.10.2026
# @design  self-contained project makefile - same shape as the FreeRTOS board
#          smoke project (tests/board/freertos-rk3568-smoke/Makefile:1-45).
#
# Product: build/threadx-rk3568.bin - the flat raw binary staged as rtos.bin
# and loaded with `tftp 0xa000000 rtos.bin`, entered with `go 0xa000000`.
#
# Mode-domain isolation: the tx include paths and the port defines ride on the
# ThreadX compile units only (kernel, port, board glue, app). The base layer
# (arch/, board/, os/baremetal/) never sees a tx header - the layering
# invariant the `guards` target re-checks from the source side.
#
# `make check` runs the build-side gates: TX_CFLAGS must carry -DEL1=1 (the
# port's exception level is a build-time decision; without it the first
# context switch dies on an undefined MSR) and -DTX_INCLUDE_USER_DEFINE_FILE
# (the board fork of tx_user.h is the configuration); the board config must
# not define the CMSIS slot backlink (its consumer is not in this image); the
# port's fp .c files must not be compiled; and the linked image must satisfy
# the symbol contract (_start at the load address, the runtime vector table
# 0x800-aligned, the kernel/schedule/tick symbols present, __top_of_ram
# resolved, nothing unresolved).

ROOT := $(abspath $(CURDIR)/../../..)
TX := $(ROOT)/third-party/threadx
TX_PORT := $(TX)/ports/cortex_a55/gnu
BOARD_CFG := $(ROOT)/os/threadx/board/rk3568-itx

AARCH64_CROSS_PATH ?= $(HOME)/workspace/env/src/xpack-aarch64-none-elf-gcc-13.2.1-1.1
CROSS_COMPILE ?= $(AARCH64_CROSS_PATH)/bin/aarch64-none-elf-
CC := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
NM := $(CROSS_COMPILE)nm
SIZE := $(CROSS_COMPILE)size

BUILD_TAG ?= $(shell git -C $(ROOT) describe --always --dirty 2>/dev/null || echo unknown)

BUILD := build
TARGET := $(BUILD)/threadx-rk3568

# The section flags plus --gc-sections are load-bearing: the vendored
# formatter's printf_/vprintf_ entry points keep a reference to the unbound
# _putchar sink, nothing in this image calls them, and section GC is what
# discards them (the contract in components/printf/PROVENANCE.md:25-27) -
# every sibling board image links this way.
CFLAGS := -Og -g3 -std=c11 -Wall -Wextra \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-march=armv8-a -mgeneral-regs-only -mstrict-align -mno-outline-atomics \
	-ffunction-sections -fdata-sections \
	-DFW_BUILD_TAG='"$(BUILD_TAG)"' \
	-I$(ROOT)/arch/aarch64 \
	-I$(ROOT)/board/rk3568-itx \
	-I$(ROOT)/board/rk3568-itx/drivers \
	-I$(ROOT)/os/baremetal

# -DEL1=1 is load-bearing: the port picks its exception-return register bank
# (SPSR/ELR_EL1 vs _EL2 vs _EL3) from this compile-time macro - undefined, it
# silently emits the EL3 variant and the first context switch dies on an
# undefined MSR at EL1 (EC=0 sync fault, ESR=0x02000000). The board fork of
# tx_user.h wins the include resolution because os/threadx/board/rk3568-itx
# comes first and the host tree is not in this compile set at all.
TX_CFLAGS := -I$(TX)/common/inc -I$(TX_PORT)/inc -I$(BOARD_CFG) \
	-DTX_INCLUDE_USER_DEFINE_FILE -DEL1=1 \
	-DCONFIG_TX_TICKS_PER_SECOND=1000 -DCONFIG_TX_TIMER_STACK=4096

LDFLAGS := -nostdlib -static \
	-Wl,-T,$(ROOT)/arch/aarch64/baremetal.ld \
	-Wl,--gc-sections \
	-Wl,--build-id=none \
	-Wl,-Map,$(TARGET).map

# The port's fp_enable/fp_disable .c files are NOT built (no FP state exists
# under -mgeneral-regs-only); the wildcard keeps them out by construction.
KERNEL_SRCS := $(wildcard $(TX)/common/src/*.c)
PORT_SRCS := $(wildcard $(TX_PORT)/src/*.S)

GLUE_SRCS := \
	$(BOARD_CFG)/tx_glue.c \
	$(BOARD_CFG)/tx_vectors.S
APP_SRCS := $(CURDIR)/main.c

BASE_SRCS := \
	$(ROOT)/arch/aarch64/start.S \
	$(ROOT)/arch/aarch64/gicv3.c \
	$(ROOT)/arch/aarch64/generic_timer.c \
	$(ROOT)/arch/aarch64/mmu.c \
	$(ROOT)/arch/aarch64/cache.c \
	$(ROOT)/board/rk3568-itx/drivers/uart.c \
	$(ROOT)/board/rk3568-itx/param.c \
	$(ROOT)/os/baremetal/memops.c \
	$(ROOT)/os/baremetal/minilibc.c \
	$(ROOT)/os/baremetal/log.c \
	$(ROOT)/os/baremetal/tick.c

TX_SRCS := $(KERNEL_SRCS) $(PORT_SRCS) $(GLUE_SRCS) $(APP_SRCS)
ALL_SRCS := $(TX_SRCS) $(BASE_SRCS)

# Object names mirror the path below ROOT: the kernel and the port share flat
# tx_*.c / tx_*.S basenames, so $(notdir) would collide (198-TU precedent).
# The vendored formatter engine (components/printf, mpaland): the freestanding
# image formats only through this engine, bound in os/threadx... os/baremetal/
# minilibc.c. Float and exponential rendering are compiled out - the build is
# -mgeneral-regs-only and has no FP state.
PRINTF_SRCS := $(ROOT)/components/printf/printf.c
PRINTF_CFLAGS := -DPRINTF_DISABLE_SUPPORT_FLOAT -DPRINTF_DISABLE_SUPPORT_EXPONENTIAL
ALL_SRCS += $(PRINTF_SRCS)

OBJS := $(addprefix $(BUILD)/obj/,$(addsuffix .o,$(patsubst $(ROOT)/%,%,$(ALL_SRCS))))

.PHONY: all check guards clean
all: $(TARGET).bin check

define COMPILE_RULE
$(BUILD)/obj/$(patsubst $(ROOT)/%,%,$(1)).o: $(1)
	@mkdir -p $$(dir $$@)
	$(CC) $(CFLAGS) $(2) -c $$< -o $$@
endef

$(foreach s,$(PRINTF_SRCS),$(eval $(call COMPILE_RULE,$(s),$(PRINTF_CFLAGS))))
$(foreach s,$(BASE_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(TX_SRCS),$(eval $(call COMPILE_RULE,$(s),$(TX_CFLAGS))))

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
	@if grep -E '^[[:space:]]*#[[:space:]]*define[[:space:]]+TX_THREAD_USER_EXTENSION' \
		$(BOARD_CFG)/tx_user.h >/dev/null 2>&1; then \
		echo "FAIL: the CMSIS slot backlink is defined but its consumer is not in this image"; exit 1; \
	fi
	@if grep -rnE '#include[[:space:]]*"[a-z_./]*tx_[a-z0-9_]+\.h"' \
		$(ROOT)/arch/aarch64 $(ROOT)/board/rk3568-itx $(ROOT)/os/baremetal >/dev/null 2>&1; then \
		echo "FAIL: a tx header leaked into arch/, board/ or os/baremetal/"; exit 1; \
	fi
	@if echo "$(ALL_SRCS)" | tr ' ' '\n' | grep -q 'tx_thread_fp_'; then \
		echo "FAIL: a port fp TU entered the compile set"; exit 1; \
	fi
	@echo "PASS: -DEL1=1 and -DTX_INCLUDE_USER_DEFINE_FILE present, board config free of the CMSIS key, base layer free of tx headers, no fp port TU"

check: guards $(TARGET).elf
	@echo "== symbol contract =="
	@set -e; \
	start=$$($(NM) $(TARGET).elf | awk '$$3=="_start"{print $$1}'); \
	[ -n "$$start" ] || { echo "FAIL: _start not found"; exit 1; }; \
	[ $$(( 0x$$start )) -eq $$(( 0xa000000 )) ] \
		|| { echo "FAIL: _start=0x$$start (want 0xa000000)"; exit 1; }; \
	vec=$$($(NM) $(TARGET).elf | awk '$$3=="_threadx_vector_table"{print $$1}'); \
	[ -n "$$vec" ] || { echo "FAIL: _threadx_vector_table not found"; exit 1; }; \
	[ $$(( 0x$$vec % 2048 )) -eq 0 ] \
		|| { echo "FAIL: vector table at 0x$$vec is not 0x800 aligned"; exit 1; }; \
	for s in _tx_initialize_kernel_enter _tx_thread_schedule _tx_timer_interrupt \
	         _tx_thread_context_save _tx_thread_context_restore __top_of_ram; do \
		$(NM) $(TARGET).elf | awk '{print $$3}' | grep -qx "$$s" \
			|| { echo "FAIL: $$s not in the image"; exit 1; }; \
	done; \
	undef=$$($(NM) -u $(TARGET).elf); \
	[ -z "$$undef" ] || { echo "FAIL: undefined symbols:"; echo "$$undef"; exit 1; }; \
	echo "PASS: _start=0x$$start, vectors=0x$$vec (0x800 aligned), kernel/schedule/tick symbols present, __top_of_ram resolved, 0 undefined"
	@$(SIZE) $(TARGET).elf
	@echo "build-tag: $(BUILD_TAG)"

clean:
	rm -rf $(BUILD)

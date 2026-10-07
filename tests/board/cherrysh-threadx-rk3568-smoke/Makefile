# @file    Makefile
# @brief   Build the CherrySH ThreadX board image (cherrysh-threadx.bin) for RK3568.
# @author  zhugengyu
# @date    08.10.2026
# @design  self-contained project makefile - same shape as the ThreadX bring-up
#          smoke (tests/board/threadx-rk3568-smoke/Makefile) plus the CMSIS-RTOS2
#          backend and the shell; the include path picks the CMSIS-RTOS2 flavour
#          of the board configuration (os/threadx/board/rk3568-itx/cmsis2).
#
# Product: build/cherrysh-threadx.bin - the flat raw binary staged as rtos.bin
# and loaded with `tftp 0xa000000 rtos.bin`, entered with `go 0xa000000`.
#
# Beyond the smoke's gates this build checks that the CMSIS flavour is the one
# in effect (the backend needs its slot backlink key), that the host-side files
# are not in the compile set, and the command tables' density: the shell core
# iterates FSymTab/VSymTab as flat arrays with a fixed stride, so any padding
# between entries would hide every entry after the first.

ROOT := $(abspath $(CURDIR)/../../..)
TX := $(ROOT)/third-party/threadx
TX_PORT := $(TX)/ports/cortex_a55/gnu
BOARD_CMSIS2 := $(ROOT)/os/threadx/board/rk3568-itx/cmsis2
CMSIS := $(ROOT)/third-party/CMSIS_6
CHERRYSH := $(ROOT)/components/cherrysh
PORT := $(ROOT)/ports/cherrysh

AARCH64_CROSS_PATH ?= $(HOME)/workspace/env/src/xpack-aarch64-none-elf-gcc-13.2.1-1.1
CROSS_COMPILE ?= $(AARCH64_CROSS_PATH)/bin/aarch64-none-elf-
CC := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
NM := $(CROSS_COMPILE)nm
SIZE := $(CROSS_COMPILE)size

BUILD_TAG ?= $(shell git -C $(ROOT) describe --always --dirty 2>/dev/null || echo unknown)

BUILD := build
TARGET := $(BUILD)/cherrysh-threadx

INCLUDES := \
	-I$(ROOT) \
	-I$(ROOT)/os/cmsis2 \
	-I$(ROOT)/arch/aarch64 \
	-I$(ROOT)/board/rk3568-itx \
	-I$(ROOT)/board/rk3568-itx/drivers \
	-I$(ROOT)/os/baremetal \
	-I$(CHERRYSH) \
	-I$(PORT) \
	-I$(ROOT)/components/tlsf \
	-I$(CMSIS)/CMSIS/RTOS2/Include

# -DEL1=1 is load-bearing (the port's exception-return register bank is a
# build-time decision). The FIRST -I is the CMSIS2 flavour directory: tx_port.h's
# quoted tx_user.h resolves there, the bring-up flavour directory is not on any
# path, and the host header sits later in this list - so exactly one flavour is
# ever visible, and the board files re-assert it with a compile-time gate.
TX_CFLAGS := -I$(BOARD_CMSIS2) \
	-I$(ROOT)/os/threadx \
	-I$(TX)/common/inc \
	-I$(TX_PORT)/inc \
	-DTX_INCLUDE_USER_DEFINE_FILE -DEL1=1 \
	-DCONFIG_TX_TICKS_PER_SECOND=1000 -DCONFIG_TX_TIMER_STACK=4096

BASE_CFLAGS := -Og -g3 -std=c11 -Wall -Wextra \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-march=armv8-a -mgeneral-regs-only -mstrict-align -mno-outline-atomics \
	-ffunction-sections -fdata-sections \
	-DFW_BUILD_TAG='"$(BUILD_TAG)"' \
	-DCMSIS2_BACKEND_THREADX \
	-DSHELL_PORT_HOST='"rk3568"'

# The freestanding libc surface is declared everywhere it is used: the vendored
# shell core calls strnlen without a prototype of its own. Assembly files do not
# take the C header.
CFLAGS := $(BASE_CFLAGS) -include $(ROOT)/os/baremetal/minilibc.h
ASM_CFLAGS := $(BASE_CFLAGS)

# The vendored shell core is byte-identical upstream and cannot be edited; it
# does trip -Wextra's sign-compare in one builtin. Its translation units are
# compiled at the -Wall level the upstream project itself uses.
VENDORED_CFLAGS := $(filter-out -Wextra,$(CFLAGS))

# --gc-sections drops everything nothing references, which would take the
# CMSIS-RTOS2 extension's entry points (osCriticalEnter/Exit) with it - this
# image has no caller yet, but the backend is brought in whole and its public
# surface is part of the symbol contract. -u pins the two.
LDFLAGS := -nostdlib -static \
	-Wl,-T,$(ROOT)/arch/aarch64/baremetal.ld \
	-Wl,--gc-sections \
	-Wl,-u,osCriticalEnter \
	-Wl,-u,osCriticalExit \
	-Wl,--build-id=none \
	-Wl,-Map,$(TARGET).map

# --- compile set (four domains) --------------------------------------------

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

# The port's fp_enable/fp_disable .c files are NOT built (no FP state under
# -mgeneral-regs-only); the wildcard keeps them out by construction.
TX_SRCS := \
	$(wildcard $(TX)/common/src/*.c) \
	$(wildcard $(TX_PORT)/src/*.S) \
	$(ROOT)/os/threadx/board/rk3568-itx/tx_glue.c \
	$(ROOT)/os/threadx/board/rk3568-itx/tx_vectors.S \
	$(ROOT)/os/threadx/board/rk3568-itx/heap.c \
	$(ROOT)/os/threadx/board/rk3568-itx/cmsis2/cmsis2_board.c \
	$(ROOT)/os/threadx/cmsis2_threadx.c \
	$(ROOT)/os/threadx/cmsis2_ext.c

SHELL_SRCS := \
	$(CURDIR)/main.c \
	$(PORT)/shell_port.c \
	$(PORT)/console_board.c

VENDORED_SRCS := \
	$(CHERRYSH)/chry_shell.c \
	$(CHERRYSH)/cherryrl/chry_readline.c \
	$(CHERRYSH)/builtin/clear.c \
	$(CHERRYSH)/builtin/help.c \
	$(CHERRYSH)/builtin/shsize.c

ALLOC_SRCS := $(ROOT)/components/tlsf/tlsf.c

ALL_SRCS := $(BASE_SRCS) $(TX_SRCS) $(SHELL_SRCS) $(VENDORED_SRCS) $(ALLOC_SRCS)

# Object names mirror the path below ROOT: the kernel and the port share flat
# tx_*.c / tx_*.S basenames, so $(notdir) would collide.
OBJS := $(addprefix $(BUILD)/obj/,$(addsuffix .o,$(patsubst $(ROOT)/%,%,$(ALL_SRCS))))

.PHONY: all check guards clean
all: $(TARGET).bin check

define COMPILE_RULE
$(BUILD)/obj/$(patsubst $(ROOT)/%,%,$(1)).o: $(1)
	@mkdir -p $$(dir $$@)
	$(CC) $(if $(filter %.S,$(1)),$(ASM_CFLAGS),$(if $(filter $(1),$(VENDORED_SRCS)),$(VENDORED_CFLAGS),$(CFLAGS))) $(2) $(INCLUDES) -c $$< -o $$@
endef
$(foreach s,$(BASE_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(TX_SRCS),$(eval $(call COMPILE_RULE,$(s),$(TX_CFLAGS))))
$(foreach s,$(SHELL_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(VENDORED_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(ALLOC_SRCS),$(eval $(call COMPILE_RULE,$(s),)))

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
	@echo "$(TX_CFLAGS)" | grep -q -- "-I$(BOARD_CMSIS2) " \
		|| { echo "FAIL: the CMSIS2 flavour directory is not the first -I"; exit 1; }
	@grep -E '^[[:space:]]*#[[:space:]]*define[[:space:]]+TX_THREAD_USER_EXTENSION' \
		$(BOARD_CMSIS2)/tx_user.h >/dev/null 2>&1 \
		|| { echo "FAIL: the CMSIS2 flavour does not define the slot backlink"; exit 1; }
	@if echo "$(ALL_SRCS)" | tr ' ' '\n' | grep -qE "^$(ROOT)/os/threadx/(cmsis2_host|heap|tx_glue)\.c$$|^$(ROOT)/os/threadx/tx_user\.h$$"; then \
		echo "FAIL: a host-side os/threadx file entered the compile set"; exit 1; \
	fi
	@if grep -rnE '#include[[:space:]]*"[a-z_./]*tx_[a-z0-9_]+\.h"' \
		$(ROOT)/arch/aarch64 $(ROOT)/board/rk3568-itx $(ROOT)/os/baremetal >/dev/null 2>&1; then \
		echo "FAIL: a tx header leaked into arch/, board/ or os/baremetal/"; exit 1; \
	fi
	@if echo "$(ALL_SRCS)" | tr ' ' '\n' | grep -q 'tx_thread_fp_'; then \
		echo "FAIL: a port fp TU entered the compile set"; exit 1; \
	fi
	@echo "PASS: port keys present, CMSIS2 flavour in effect, host files out, base layer free of tx headers, no fp port TU"

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
	         _tx_thread_context_save _tx_thread_context_restore __top_of_ram \
	         osKernelStart osThreadNew osDelay osCriticalEnter osCriticalExit \
	         cmsis2_tx_application_define tx_board_app_define tx_kernel_enter_spsel0 \
	         pvPortMalloc vPortFree tlsf_malloc tlsf_free \
	         shell_port_init chry_shell_init console_sput console_sget; do \
		$(NM) $(TARGET).elf | awk '{print $$3}' | grep -qx "$$s" \
			|| { echo "FAIL: $$s not in the image"; exit 1; }; \
	done; \
	undef=$$($(NM) -u $(TARGET).elf); \
	[ -z "$$undef" ] || { echo "FAIL: undefined symbols:"; echo "$$undef"; exit 1; }; \
	echo "PASS: _start=0x$$start, vectors=0x$$vec (0x800 aligned), kernel/CMSIS2/board symbols present, 0 undefined"
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

# @file    Makefile
# @brief   Build the ThreadX board regression image for RK3568.
# @author  zhugengyu
# @date    08.10.2026
# @design  self-contained project makefile - evolved from the shell image
#          (tests/board/cherrysh-threadx-rk3568-smoke/Makefile:1-17): same four
#          compile domains plus the shared regression runner (tests/board/common)
#          and the suite's own test sources. The include path picks the
#          CMSIS-RTOS2 flavour of the board configuration.
#
# Product: build/threadx-rk3568-regression.bin - the flat raw binary staged as
# rtos.bin and loaded with `tftp 0xa000000 rtos.bin`, entered with `go 0xa000000`.
#
# Test execution order = link order = the TESTS_SRCS list below; late-running
# cases (USB bring-up) are appended at the end of that list.
#
# Build gates beyond the shell image's: the test table's density (the runner
# walks BOARD_TESTS as a flat array with a fixed stride - padding would hide
# every entry after the first), the three-source case-name reconciliation
# (image section / suite.mk / judge.py) and the host replay of the selection
# vectors.

ROOT := $(abspath $(CURDIR)/../../..)
TX := $(ROOT)/third-party/threadx
TX_PORT := $(TX)/ports/cortex_a55/gnu
BOARD_CMSIS2 := $(ROOT)/os/threadx/board/rk3568-itx/cmsis2
CMSIS := $(ROOT)/third-party/CMSIS_6
CHERRYSH := $(ROOT)/components/cherrysh
PORT := $(ROOT)/ports/cherrysh
COMMON := $(ROOT)/tests/board/common

# USB host stack domain: the vendored import, the board's USB facts and the
# port glue. CHERRYUSB is the tree to compile from - the vendored pin, or the
# patched materialisation of it (tools/patches/replay.sh prints the path and
# applies the declared patches to a build copy; the vendored tree itself is
# never edited and never compiled from directly when patches exist).
CHERRYUSB := $(shell sh $(ROOT)/tools/patches/replay.sh cherryusb)
USB_PORT := $(ROOT)/ports/cherryusb
USB_BOARD := $(ROOT)/board/rk3568-itx/usb

SUITE_NAME := threadx-rk3568-regression

AARCH64_CROSS_PATH ?= $(HOME)/workspace/env/src/xpack-aarch64-none-elf-gcc-13.2.1-1.1
CROSS_COMPILE ?= $(AARCH64_CROSS_PATH)/bin/aarch64-none-elf-
CC := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
NM := $(CROSS_COMPILE)nm
READELF := $(CROSS_COMPILE)readelf
SIZE := $(CROSS_COMPILE)size
# The selection matcher is replayed on the build host as well (same source, the
# same vector file the judges consume), so this one target needs the host cc.
HOSTCC ?= cc

BUILD_TAG ?= $(shell git -C $(ROOT) describe --always --dirty 2>/dev/null || echo unknown)

BUILD := build
TARGET := $(BUILD)/$(SUITE_NAME)

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
	-I$(COMMON) \
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
	-DBOARD_SUITE_NAME='"$(SUITE_NAME)"' \
	-DCMSIS2_BACKEND_THREADX \
	-DSHELL_PORT_HOST='"rk3568"'

# The freestanding libc surface is declared everywhere it is used: the vendored
# shell core calls strnlen without a prototype of its own, and the runner builds
# its start line with snprintf. Assembly files do not take the C header.
CFLAGS := $(BASE_CFLAGS) -include $(ROOT)/os/baremetal/minilibc.h
ASM_CFLAGS := $(BASE_CFLAGS)

# The vendored shell core is byte-identical upstream and cannot be edited; it
# does trip -Wextra's sign-compare in one builtin. Its translation units are
# compiled at the -Wall level the upstream project itself uses. The imported
# USB stack's sources get the same treatment for the same reason - they are
# bytes this project does not own. (Recursive assignment: the imported list
# is defined below this point.)
VENDORED_WARNED_SRCS = $(VENDORED_SRCS) $(USB_STACK_SRCS)
VENDORED_CFLAGS := $(filter-out -Wextra,$(CFLAGS))

# --gc-sections drops everything nothing references, which would take the
# CMSIS-RTOS2 extension's entry points (osCriticalEnter/Exit) with it - the
# backend's public surface is part of the symbol contract. -u pins the two.
# The USB domain is pinned the same way: the board entry points and the
# stack's initialiser are the surface the USB cases call, and without the
# pins the whole host stack (drivers, DMA pools, the non-cacheable window)
# would be collected until those cases land, leaving the mapping and the
# window's sizing unchecked in the meantime.
LDFLAGS := -nostdlib -static \
	-Wl,-T,$(ROOT)/arch/aarch64/baremetal.ld \
	-Wl,--gc-sections \
	-Wl,-u,osCriticalEnter \
	-Wl,-u,osCriticalExit \
	-Wl,-u,usb_board_init \
	-Wl,-u,usb_board_reg_base \
	-Wl,-u,usbh_initialize \
	-Wl,--build-id=none \
	-Wl,-Map,$(TARGET).map

# --- compile set (six domains) ---------------------------------------------

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

COMMON_SRCS := \
	$(COMMON)/board_suite.c \
	$(COMMON)/board_select.c

SHELL_SRCS := \
	$(CURDIR)/main.c \
	$(PORT)/shell_port.c \
	$(PORT)/console_board.c

# Execution order = this list's order (link order for the BOARD_TESTS section).
TESTS_SRCS := \
	$(CURDIR)/tests/shell_up.c \
	$(CURDIR)/tests/kernel_up.c \
	$(CURDIR)/tests/tick_ladder.c \
	$(CURDIR)/tests/thread_sched.c \
	$(CURDIR)/tests/cmsis2_prio.c \
	$(CURDIR)/tests/tlsf_heap.c \
	$(CURDIR)/tests/os_timer.c \
	$(CURDIR)/tests/usb_msc.c \
	$(CURDIR)/tests/usb_xhci.c \
	$(CURDIR)/tests/usb_ehci.c

VENDORED_SRCS := \
	$(CHERRYSH)/chry_shell.c \
	$(CHERRYSH)/cherryrl/chry_readline.c \
	$(CHERRYSH)/builtin/clear.c \
	$(CHERRYSH)/builtin/help.c \
	$(CHERRYSH)/builtin/shsize.c

ALLOC_SRCS := $(ROOT)/components/tlsf/tlsf.c

# --- USB host domain (imported stack + board facts + port glue) ------------
#
# Two source groups with different warning policy: the port/board sources are
# this project's and compile under the full -Wall -Wextra set, the imported
# stack's sources compile under the -Wall level the upstream project uses.
# The board's usb_config.h is picked up by include path alone (the imported
# headers include "usb_config.h" themselves), so the USB_PORT board directory
# must come before the vendored tree's own paths.
USB_BOARD_SRCS := \
	$(USB_BOARD)/usb_domain.c \
	$(USB_PORT)/board/rk3568-itx/usb_glue.c \
	$(USB_PORT)/osal/cmsis2/osal_cmsis2.c

USB_STACK_SRCS := \
	$(CHERRYUSB)/core/usbh_core.c \
	$(CHERRYUSB)/class/hub/usbh_hub.c \
	$(CHERRYUSB)/class/msc/usbh_msc.c \
	$(CHERRYUSB)/port/ehci/usb_hc_ehci.c \
	$(CHERRYUSB)/port/xhci/usb_hc_xhci.c

USB_INCLUDES := \
	-I$(USB_PORT)/board/rk3568-itx \
	-I$(USB_BOARD) \
	-I$(CHERRYUSB)/common \
	-I$(CHERRYUSB)/core \
	-I$(CHERRYUSB)/class/hub \
	-I$(CHERRYUSB)/class/msc \
	-I$(CHERRYUSB)/port/ehci \
	-I$(CHERRYUSB)/port/xhci

ALL_SRCS := $(BASE_SRCS) $(TX_SRCS) $(COMMON_SRCS) $(SHELL_SRCS) $(TESTS_SRCS) $(VENDORED_SRCS) $(ALLOC_SRCS) $(USB_BOARD_SRCS) $(USB_STACK_SRCS)

# Object names mirror the path below ROOT: the kernel and the port share flat
# tx_*.c / tx_*.S basenames, so $(notdir) would collide.
OBJS := $(addprefix $(BUILD)/obj/,$(addsuffix .o,$(patsubst $(ROOT)/%,%,$(ALL_SRCS))))

.PHONY: all check guards clean
all: $(TARGET).bin check

define COMPILE_RULE
$(BUILD)/obj/$(patsubst $(ROOT)/%,%,$(1)).o: $(1)
	@mkdir -p $$(dir $$@)
	@echo "  CC $$<"
	@$(CC) $(if $(filter %.S,$(1)),$(ASM_CFLAGS),$(if $(filter $(1),$(VENDORED_WARNED_SRCS)),$(VENDORED_CFLAGS),$(CFLAGS))) $(2) $(INCLUDES) -c $$< -o $$@
endef
$(foreach s,$(BASE_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(TX_SRCS),$(eval $(call COMPILE_RULE,$(s),$(TX_CFLAGS))))
$(foreach s,$(COMMON_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(SHELL_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(TESTS_SRCS),$(eval $(call COMPILE_RULE,$(s),$(USB_INCLUDES))))
$(foreach s,$(VENDORED_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(ALLOC_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(USB_BOARD_SRCS),$(eval $(call COMPILE_RULE,$(s),$(USB_INCLUDES))))
$(foreach s,$(USB_STACK_SRCS),$(eval $(call COMPILE_RULE,$(s),$(USB_INCLUDES))))

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
	@echo "$(USB_INCLUDES)" | grep -q "^ *-I$(USB_PORT)/board/rk3568-itx " \
		|| { echo "FAIL: the USB board config directory is not the first USB -I"; exit 1; }
	@test -f $(USB_PORT)/board/rk3568-itx/usb_config.h \
		|| { echo "FAIL: the USB board configuration header is missing"; exit 1; }
	@if ls $(ROOT)/patches/cherryusb/*.patch >/dev/null 2>&1; then \
		case "$(CHERRYUSB)" in \
		$(ROOT)/third-party/cherryusb) \
			echo "FAIL: patches exist but the USB stack would compile from the vendored tree"; exit 1 ;; \
		esac; \
	fi
	@for s in $(USB_STACK_SRCS); do \
		case "$$s" in $(CHERRYUSB)/*) ;; \
		*) echo "FAIL: imported USB source outside the replayed tree: $$s"; exit 1 ;; \
		esac; \
	done
	@echo "$(TX_CFLAGS)" | grep -q -- "-I$(BOARD_CMSIS2) " \
		|| { echo "FAIL: the CMSIS2 flavour directory is not the first -I"; exit 1; }
	@[ "$$(basename $(CURDIR))" = "$(SUITE_NAME)" ] \
		|| { echo "FAIL: SUITE_NAME ($(SUITE_NAME)) is not the directory name"; exit 1; }
	@echo "$(CFLAGS)" | grep -q -- "-DBOARD_SUITE_NAME=" \
		|| { echo "FAIL: BOARD_SUITE_NAME missing from CFLAGS"; exit 1; }
	@echo "$(CFLAGS)" | grep -q -- "$(SUITE_NAME)" \
		|| { echo "FAIL: BOARD_SUITE_NAME does not carry the suite name"; exit 1; }
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
	@if grep -rnE '\btx_[a-z][a-z0-9_]*[[:space:]]*\(' $(COMMON) $(CURDIR)/tests $(CURDIR)/main.c >/dev/null 2>&1; then \
		echo "FAIL: a kernel API leaked into the suite sources (CMSIS-RTOS2 only)"; exit 1; \
	fi
	@if echo "$(ALL_SRCS)" | tr ' ' '\n' | grep -q 'tx_thread_fp_'; then \
		echo "FAIL: a port fp TU entered the compile set"; exit 1; \
	fi
	@echo "PASS: port keys present, CMSIS2 flavour in effect, suite name matches, host files out, base layer and suite sources free of kernel APIs"

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
	         shell_port_init chry_shell_init console_sput console_sget \
	         board_suite_run board_case_fail board_glob_match board_select_parse; do \
		$(NM) $(TARGET).elf | awk '{print $$3}' | grep -qx "$$s" \
			|| { echo "FAIL: $$s not in the image"; exit 1; }; \
	done; \
	undef=$$($(NM) -u $(TARGET).elf); \
	[ -z "$$undef" ] || { echo "FAIL: undefined symbols:"; echo "$$undef"; exit 1; }; \
	echo "PASS: _start=0x$$start, vectors=0x$$vec (0x800 aligned), kernel/CMSIS2/board/runner symbols present, 0 undefined"
	@echo "== command and test table density =="
	@set -e; \
	fs=$$($(NM) $(TARGET).elf | awk '$$3=="__fsymtab_end"{print $$1}'); \
	fs0=$$($(NM) $(TARGET).elf | awk '$$3=="__fsymtab_start"{print $$1}'); \
	vs=$$($(NM) $(TARGET).elf | awk '$$3=="__vsymtab_end"{print $$1}'); \
	vs0=$$($(NM) $(TARGET).elf | awk '$$3=="__vsymtab_start"{print $$1}'); \
	bt=$$($(NM) $(TARGET).elf | awk '$$3=="__board_tests_end"{print $$1}'); \
	bt0=$$($(NM) $(TARGET).elf | awk '$$3=="__board_tests_start"{print $$1}'); \
	fsz=$$(( 0x$$fs - 0x$$fs0 )); vsz=$$(( 0x$$vs - 0x$$vs0 )); btz=$$(( 0x$$bt - 0x$$bt0 )); \
	echo "fsymtab bytes=$$fsz (expect 4*40=160), vsymtab bytes=$$vsz (expect 2*24=48), board_tests bytes=$$btz"; \
	[ $$fsz -eq 160 ] || { echo "FAIL: FSymTab is not dense"; exit 1; }; \
	[ $$vsz -eq 48 ] || { echo "FAIL: VSymTab is not dense"; exit 1; }; \
	[ $$(( btz % 16 )) -eq 0 ] || { echo "FAIL: BOARD_TESTS is not a whole number of entries"; exit 1; }; \
	n=$$(( btz / 16 )); echo "board_tests entries=$$n"; \
	[ $$n -ge 6 ] || { echo "FAIL: fewer than the six historical cases registered"; exit 1; }; \
	echo "PASS: command and test tables dense (no inter-entry padding)"
	@echo "== case-name reconciliation (image / suite.mk / judge) =="
	@sh $(COMMON)/check-case-list.sh $(TARGET).elf $(CURDIR)/suite.mk $(CURDIR)/judge.py $(OBJCOPY) $(READELF) $(BUILD)
	@echo "== selection vectors (host replay) =="
	@$(HOSTCC) -DBOARD_SELECT_HOSTTEST -O1 -std=c11 -Wall -Wextra -c $(COMMON)/board_select.c -o $(BUILD)/board_select_hosttest.o
	@$(HOSTCC) $(BUILD)/board_select_hosttest.o -o $(BUILD)/select-hosttest
	@$(BUILD)/select-hosttest $(COMMON)/select-vectors.tsv
	@$(SIZE) $(TARGET).elf
	@echo "build-tag: $(BUILD_TAG)"

clean:
	rm -rf $(BUILD)

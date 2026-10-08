# @file    Makefile
# @brief   Build the FreeRTOS board regression image for RK3568.
# @author  zhugengyu
# @date    08.10.2026
# @design  self-contained project makefile - the FreeRTOS sibling of the
#          ThreadX regression image (tests/board/threadx-rk3568-regression/
#          Makefile: same regression runner, case table and USB host stack)
#          with the FreeRTOS kernel and the CMSIS-FreeRTOS adapter of the
#          shell image (tests/board/cherrysh-rk3568-smoke/Makefile) in place of
#          the ThreadX kernel. The include path picks the USB heap flavour of
#          the board configuration (os/freertos/board/rk3568-itx/usb/).
#
# Product: build/freertos-rk3568-regression.bin - the flat raw binary staged as
# freertos.bin and loaded with `tftp 0xa000000 freertos.bin`, entered with
# `go 0xa000000`.
#
# Test execution order = link order = the TESTS_SRCS list below.

ROOT := $(abspath $(CURDIR)/../../..)
KERNEL := $(ROOT)/third-party/FreeRTOS-Kernel
CMSIS := $(ROOT)/third-party/CMSIS_6
BOARD_CFG := $(ROOT)/os/freertos/board/rk3568-itx/usb
PORT := $(ROOT)/ports/cherrysh
CHERRYSH := $(ROOT)/components/cherrysh
COMMON := $(ROOT)/tests/board/common

# The CMSIS-FreeRTOS adapter compiles from the patch-replayed tree (the
# handle-width fix is load-bearing on 64-bit targets).
ADAPTER := $(shell sh $(ROOT)/tools/patches/replay.sh CMSIS-FreeRTOS)
ifeq ($(ADAPTER),)
$(error tools/patches/replay.sh returned no source root for CMSIS-FreeRTOS)
endif

# The USB host stack: the vendored import (or its patched materialisation) and
# this project's port of it.
CHERRYUSB := $(shell sh $(ROOT)/tools/patches/replay.sh cherryusb)
USB_PORT := $(ROOT)/ports/cherryusb
USB_BOARD := $(ROOT)/board/rk3568-itx/usb

SUITE_NAME := freertos-rk3568-regression

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
	-I$(BOARD_CFG) \
	-I$(ROOT) \
	-I$(ROOT)/os/cmsis2 \
	-I$(ROOT)/os/cmsis2/board-compat \
	-I$(ROOT)/arch/aarch64 \
	-I$(ROOT)/board/rk3568-itx \
	-I$(ROOT)/board/rk3568-itx/drivers \
	-I$(ROOT)/os/baremetal \
	-I$(CHERRYSH) \
	-I$(PORT) \
	-I$(COMMON) \
	-I$(KERNEL)/include \
	-I$(KERNEL)/portable/GCC/ARM_AARCH64_SRE \
	-I$(CMSIS)/CMSIS/RTOS2/Include \
	-I$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Include

# USB_BOARD_IRQ_PRIORITY_RAW: this kernel's port forbids API calls from
# interrupts more urgent than configMAX_API_CALL_INTERRUPT_PRIORITY (13), and
# the USB handler gives a semaphore from its ISR - so the controller line sits
# at raw 0xd0 (13) instead of the board's mid default (0xa0 = 10), which the
# ThreadX line keeps.
#
# The vendored shell core is byte-identical upstream and cannot be edited; it
# trips -Wextra's sign-compare in one builtin, so its units compile at the
# level the upstream project uses.
BASE_CFLAGS := -Og -g3 -std=c11 -Wall -Wextra \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-march=armv8-a -mgeneral-regs-only -mstrict-align -mno-outline-atomics \
	-ffunction-sections -fdata-sections \
	-DGUEST -DFW_BUILD_TAG='"$(BUILD_TAG)"' \
	-DBOARD_SUITE_NAME='"$(SUITE_NAME)"' \
	-DCMSIS2_BACKEND_FREERTOS \
	-DSHELL_PORT_HOST='"rk3568"' \
	-DUSB_BOARD_IRQ_PRIORITY_RAW=0xd0u

CFLAGS := $(BASE_CFLAGS) -include $(ROOT)/os/baremetal/minilibc.h
ASM_CFLAGS := $(BASE_CFLAGS)
VENDORED_CFLAGS := $(filter-out -Wextra,$(CFLAGS))

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

# --- compile set -----------------------------------------------------------

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
	$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Source/cmsis_os2.c \
	$(ROOT)/os/freertos/board/rk3568-itx/freertos_board_glue.c \
	$(ROOT)/os/freertos/board/rk3568-itx/freertos_board_vectors.S \
	$(ROOT)/os/freertos/board/rk3568-itx/cmsis2_ext_board.c

COMMON_SRCS := \
	$(COMMON)/board_suite.c \
	$(COMMON)/board_select.c

SHELL_SRCS := \
	$(CURDIR)/main.c \
	$(PORT)/shell_port.c \
	$(PORT)/console_board.c

# The USB host stack: board facts, this project's port of it (glue, osal,
# class application half) and the vendored import itself.
USB_BOARD_SRCS := \
	$(USB_BOARD)/usb_domain.c \
	$(USB_PORT)/board/rk3568-itx/usb_glue.c \
	$(USB_PORT)/osal/cmsis2/osal_cmsis2.c \
	$(USB_PORT)/class/msc_app.c

USB_INCLUDES := \
	-I$(USB_PORT)/board/rk3568-itx \
	-I$(USB_PORT)/class \
	-I$(USB_BOARD) \
	-I$(CHERRYUSB)/common \
	-I$(CHERRYUSB)/core \
	-I$(CHERRYUSB)/class/hub \
	-I$(CHERRYUSB)/class/msc \
	-I$(CHERRYUSB)/port/ehci \
	-I$(CHERRYUSB)/port/xhci

USB_STACK_SRCS := \
	$(CHERRYUSB)/core/usbh_core.c \
	$(CHERRYUSB)/class/hub/usbh_hub.c \
	$(CHERRYUSB)/class/msc/usbh_msc.c \
	$(CHERRYUSB)/port/ehci/usb_hc_ehci.c \
	$(CHERRYUSB)/port/xhci/usb_hc_xhci.c

# Execution order = this list's order (link order for the BOARD_TESTS section).
TESTS_SRCS := \
	$(COMMON)/cases/shell_up.c \
	$(COMMON)/cases/kernel_up.c \
	$(COMMON)/cases/tick_ladder.c \
	$(COMMON)/cases/thread_sched.c \
	$(COMMON)/cases/cmsis2_prio.c \
	$(CURDIR)/tests/freertos_sched.c \
	$(COMMON)/cases/os_timer.c \
	$(COMMON)/cases/usb_msc.c \
	$(COMMON)/cases/usb_xhci.c \
	$(COMMON)/cases/usb_ehci.c

VENDORED_SRCS := \
	$(CHERRYSH)/chry_shell.c \
	$(CHERRYSH)/cherryrl/chry_readline.c \
	$(CHERRYSH)/builtin/clear.c \
	$(CHERRYSH)/builtin/help.c \
	$(CHERRYSH)/builtin/shsize.c

ALL_SRCS := $(BASE_SRCS) $(KERNEL_SRCS) $(COMMON_SRCS) $(SHELL_SRCS) $(TESTS_SRCS) $(VENDORED_SRCS) \
	$(USB_BOARD_SRCS) $(USB_STACK_SRCS)

# Object names mirror the path below ROOT (the kernel and the port share flat
# basenames, so $(notdir) would collide).
# The vendored formatter engine (components/printf, mpaland): the freestanding
# image formats only through this engine, bound in os/threadx... os/baremetal/
# minilibc.c. Float and exponential rendering are compiled out - the build is
# -mgeneral-regs-only and has no FP state.
PRINTF_SRCS := $(ROOT)/components/printf/printf.c
PRINTF_CFLAGS := -DPRINTF_DISABLE_SUPPORT_FLOAT -DPRINTF_DISABLE_SUPPORT_EXPONENTIAL
ALL_SRCS += $(PRINTF_SRCS)

OBJS := $(addprefix $(BUILD)/obj/,$(addsuffix .o,$(patsubst $(ROOT)/%,%,$(ALL_SRCS))))

.PHONY: all check guards clean FORCE
all: $(TARGET).bin check

define COMPILE_RULE
$(BUILD)/obj/$(patsubst $(ROOT)/%,%,$(1)).o: $(1) $(TAG_STAMP)
	@mkdir -p $$(dir $$@)
	@echo "  CC $$<"
	@$(CC) $(if $(filter %.S,$(1)),$(ASM_CFLAGS),$(if $(filter $(1),$(VENDORED_SRCS)),$(VENDORED_CFLAGS),$(CFLAGS))) $(2) $(INCLUDES) -c $$< -o $$@
endef

$(foreach s,$(PRINTF_SRCS),$(eval $(call COMPILE_RULE,$(s),$(PRINTF_CFLAGS))))
$(foreach s,$(BASE_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(KERNEL_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(COMMON_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(SHELL_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(TESTS_SRCS),$(eval $(call COMPILE_RULE,$(s),$(USB_INCLUDES))))
$(foreach s,$(VENDORED_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(USB_BOARD_SRCS),$(eval $(call COMPILE_RULE,$(s),$(USB_INCLUDES))))
$(foreach s,$(USB_STACK_SRCS),$(eval $(call COMPILE_RULE,$(s),$(USB_INCLUDES))))

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
	@echo "$(CFLAGS)" | grep -q -- '-DGUEST' \
		|| { echo "FAIL: -DGUEST missing from CFLAGS"; exit 1; }
	@echo "$(CFLAGS)" | grep -q -- '-DCMSIS2_BACKEND_FREERTOS' \
		|| { echo "FAIL: the FreeRTOS backend is not selected"; exit 1; }
	@echo "$(CFLAGS)" | grep -q -- "-DBOARD_SUITE_NAME=" \
		|| { echo "FAIL: BOARD_SUITE_NAME missing from CFLAGS"; exit 1; }
	@echo "$(CFLAGS)" | grep -q -- "$(SUITE_NAME)" \
		|| { echo "FAIL: BOARD_SUITE_NAME does not carry the suite name"; exit 1; }
	@[ "$$(basename $(CURDIR))" = "$(SUITE_NAME)" ] \
		|| { echo "FAIL: SUITE_NAME ($(SUITE_NAME)) is not the directory name"; exit 1; }
	@if grep -qn 'configTOTAL_HEAP_SIZE.*64 \* 1024' $(BOARD_CFG)/FreeRTOSConfig.h; then \
		echo "FAIL: the USB flavour kept the shell image's heap size"; exit 1; \
	fi
	@if echo "$(ALL_SRCS)" | tr ' ' '\n' | grep -qE '^$(ROOT)/os/freertos/(cmsis2_ext|heap)\.c$$'; then \
		echo "FAIL: a host-side os/freertos file entered the compile set"; exit 1; \
	fi
	@if grep -rnE '#include[[:space:]]*"[a-z_./]*tx_[a-z0-9_]+\.h"' \
		$(ROOT)/arch/aarch64 $(ROOT)/board/rk3568-itx $(ROOT)/os/baremetal >/dev/null 2>&1; then \
		echo "FAIL: a tx header leaked into arch/, board/ or os/baremetal/"; exit 1; \
	fi
	@if grep -rnE '\btx_[a-z][a-z0-9_]*[[:space:]]*\(' $(COMMON) $(CURDIR)/tests $(CURDIR)/main.c >/dev/null 2>&1; then \
		echo "FAIL: a ThreadX kernel API leaked into the suite sources"; exit 1; \
	fi
	@if grep -rnE '\bxTask[A-Za-z]*[[:space:]]*\(|\bvTask[A-Za-z]*[[:space:]]*\(|\bxQueue[A-Za-z]*[[:space:]]*\(' \
		$(COMMON)/cases $(USB_PORT) >/dev/null 2>&1; then \
		echo "FAIL: a FreeRTOS kernel API leaked into the shared cases or the USB port"; exit 1; \
	fi
	@echo "PASS: guest backend selected, USB heap flavour in effect, host files out, cases and port free of kernel APIs"

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
	for s in FreeRTOS_IRQ_Handler xPortStartScheduler osKernelStart osThreadNew osDelay \
	         osCriticalEnter osCriticalExit osTimerNew \
	         board_suite_run board_case_fail board_glob_match board_select_parse \
	         usb_board_init usb_board_reg_base usbh_initialize usbh_submit_urb; do \
		$(NM) $(TARGET).elf | awk '{print $$3}' | grep -qx "$$s" \
			|| { echo "FAIL: $$s not in the image"; exit 1; }; \
	done; \
	undef=$$($(NM) -u $(TARGET).elf); \
	[ -z "$$undef" ] || { echo "FAIL: undefined symbols:"; echo "$$undef"; exit 1; }; \
	echo "PASS: _start=0x$$start, vectors=0x$$vec (2KB aligned), kernel/CMSIS2/board/USB symbols present, 0 undefined"
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
	[ $$n -ge 9 ] || { echo "FAIL: fewer than the nine cases registered"; exit 1; }; \
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

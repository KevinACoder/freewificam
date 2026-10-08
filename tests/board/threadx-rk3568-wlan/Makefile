# @file    Makefile
# @brief   Build the ThreadX board WLAN image for RK3568: the USB host stack,
#          the net80211 stack, the linuxkpi faces and the real rtl8188eus
#          driver, plus the suite's own wlan cases and console command.
# @author  zhugengyu
# @date    08.10.2026
# @design  self-contained project makefile - evolved from the regression image
#          (tests/board/threadx-rk3568-regression/Makefile:1-431): same base/TX/
#          common/shell/USB domains plus the WLAN domain (the imported net80211
#          component, its port layer, the linuxkpi bodies and the real driver).
#          The one structural difference is that this image runs with or
#          without the dongle: its wlan cases are the only ones that need the
#          card, so the image stays separate from the regression image and a
#          missing card reds only this suite.
#
# Product: build/threadx-rk3568-wlan.bin - the flat raw binary staged as
# wlan.bin and loaded with `tftp 0xa000000 wlan.bin`, entered with `go 0xa000000`.
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

# USB host stack domain (shared with the regression image).
CHERRYUSB := $(shell sh $(ROOT)/tools/patches/replay.sh cherryusb)
USB_PORT := $(ROOT)/ports/cherryusb
USB_BOARD := $(ROOT)/board/rk3568-itx/usb

# WLAN domain: the imported net80211 component, its port layer, the linuxkpi
# faces and the real driver (a pinned submodule, compiled here as its own
# translation units).
NET := $(ROOT)/components/net80211
NPORT := $(ROOT)/ports/net80211
LKPI := $(ROOT)/components/linuxkpi
DRV := $(ROOT)/third-party/rtl8xxxu

SUITE_NAME := threadx-rk3568-wlan

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
	-I$(ROOT)/board/rk3568-itx/drivers \
	-I$(ROOT)/os/baremetal \
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

# --- WLAN domain flag sets ---------------------------------------------------
#
# The net80211 world (imported stack + its port layer): the port's compatibility
# headers lead the include path, prelude.h is force-included. The driver's
# mac80211 face needs -O2 (its BUILD_BUG_ON demands constant folding, the driver
# build suite's own note). WLAN_PORT_TARGET selects the target branches of the
# port's osal (console sink instead of vfprintf/abort, and the TLSF-backed
# allocation seam rather than the C library's).
WLAN_BSD_CFLAGS := $(filter-out -Wextra,$(CFLAGS)) -std=gnu11 \
	-D_KERNEL -DWLAN_PORT_TARGET -Wno-unused-function -Wno-pointer-sign \
	-include $(NPORT)/compat/prelude.h \
	-I$(NPORT)/compat -I$(NET)/compat/netbsd -I$(NET) -I$(NPORT)

WLAN_KPI_CFLAGS := $(filter-out -Wextra,$(CFLAGS)) -std=gnu11 \
	-DWLAN_PORT_TARGET -DLKPI_ABS_PLATFORM_PROVIDED \
	-DCMSIS_device_header='"cmsis_os2.h"' \
	-I$(ROOT) -I$(LKPI) -I$(LKPI)/include \
	-I$(LKPI)/usb -I$(NET) -I$(NPORT) -I$(NPORT)/bridge

WLAN_DRV_CFLAGS := -std=c11 -O2 -Wall \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-march=armv8-a -mgeneral-regs-only -mstrict-align -mno-outline-atomics \
	-ffunction-sections -fdata-sections \
	-DWLAN_PORT_TARGET -DLKPI_ABS_PLATFORM_PROVIDED \
	-I$(ROOT) -I$(LKPI) -I$(LKPI)/include \
	-I$(CMSIS)/CMSIS/RTOS2/Include -Wno-unused-parameter \
	-include $(ROOT)/os/baremetal/minilibc.h

# --gc-sections drops everything nothing references. The pins below are the
# entry points a case or the start-up order calls that the linker cannot see
# transitively (the module-section walker, the backend registration, the
# firmware registration, the USB board entry points).
LDFLAGS := -nostdlib -static \
	-Wl,-T,$(ROOT)/arch/aarch64/baremetal.ld \
	-Wl,--gc-sections \
	-Wl,-u,osCriticalEnter \
	-Wl,-u,osCriticalExit \
	-Wl,-u,usb_board_init \
	-Wl,-u,usb_board_reg_base \
	-Wl,-u,usbh_initialize \
	-Wl,-u,lkpi_usb_init \
	-Wl,-u,lkpi_usb_cherryusb_init \
	-Wl,-u,lkpi_fw_rtl8188eufw_register \
	-Wl,-u,wlan_seam_adapter_init \
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
	$(CURDIR)/tests/wlan_probe.c \
	$(CURDIR)/tests/wlan_scan.c \
	$(CURDIR)/tests/wlan_cmds.c

VENDORED_SRCS := \
	$(CHERRYSH)/chry_shell.c \
	$(CHERRYSH)/cherryrl/chry_readline.c \
	$(CHERRYSH)/builtin/clear.c \
	$(CHERRYSH)/builtin/help.c \
	$(CHERRYSH)/builtin/shsize.c

ALLOC_SRCS := $(ROOT)/components/tlsf/tlsf.c

# --- USB host domain ---------------------------------------------------------

USB_BOARD_SRCS := \
	$(USB_BOARD)/usb_domain.c \
	$(USB_PORT)/board/rk3568-itx/usb_glue.c \
	$(USB_PORT)/osal/cmsis2/osal_cmsis2.c

USB_STACK_SRCS := \
	$(CHERRYUSB)/core/usbh_core.c \
	$(CHERRYUSB)/class/hub/usbh_hub.c \
	$(CHERRYUSB)/port/ehci/usb_hc_ehci.c \
	$(CHERRYUSB)/port/xhci/usb_hc_xhci.c

USB_INCLUDES := \
	-I$(USB_PORT)/board/rk3568-itx \
	-I$(USB_PORT)/class \
	-I$(USB_BOARD) \
	-I$(CHERRYUSB)/common \
	-I$(CHERRYUSB)/core \
	-I$(CHERRYUSB)/class/hub \
	-I$(CHERRYUSB)/port/ehci \
	-I$(CHERRYUSB)/port/xhci

# --- net80211 domain (imported stack + its port layer) -----------------------

NET80211_SRCS := \
	$(NET)/net80211/ieee80211.c \
	$(NET)/net80211/ieee80211_proto.c \
	$(NET)/net80211/ieee80211_node.c \
	$(NET)/net80211/ieee80211_output.c \
	$(NET)/net80211/ieee80211_input.c \
	$(NET)/net80211/ieee80211_amrr.c \
	$(NET)/net80211/ieee80211_netbsd.c \
	$(NET)/net80211/ieee80211_crypto.c \
	$(NET)/net80211/ieee80211_crypto_none.c \
	$(NET)/net80211/ieee80211_crypto_ccmp.c \
	$(NET)/crypto/aes/aes_bear.c \
	$(NET)/crypto/aes/aes_ct.c \
	$(NET)/crypto/aes/aes_ct_enc.c \
	$(NET)/crypto/aes/aes_ct_dec.c \
	$(NET)/crypto/aes/aes_ccm.c \
	$(NET)/crypto/aes/aes_ccm_mbuf.c \
	$(NET)/port/aes_impl_compat.c

NET80211_PORT_SRCS := \
	$(NPORT)/osal/cmsis2/osal_memory_target.c \
	$(NPORT)/osal/cmsis2/osal_cmsis2.c \
	$(NPORT)/net/mbuf.c \
	$(NPORT)/net/ifnet.c

# --- linuxkpi domain (the faces the driver reaches) --------------------------

LKPI_SRCS := \
	$(LKPI)/linuxkpi_core.c \
	$(LKPI)/linuxkpi_platform_target.c \
	$(LKPI)/linuxkpi_80211.c \
	$(LKPI)/fw_registry.c \
	$(LKPI)/skbuff/skbuff_pool.c \
	$(LKPI)/usb/linuxkpi_usb.c \
	$(LKPI)/usb/lkpi_usbh_class.c \
	$(NPORT)/bridge/wlan_seam_adapter.c \
	$(ROOT)/ports/cherryusb/lkpi_usb_cherryusb.c

# The bridge's net80211 half is a net80211-world translation unit (it owns the
# ieee80211com and includes the imported headers), so it compiles with the BSD
# flag set even though it lives under the linuxkpi domain in the source tree.
BRIDGE_BSD_SRCS := $(NPORT)/bridge/wlan_bridge.c

# --- firmware + driver -------------------------------------------------------

FW_SRCS := $(LKPI)/fw/rtl8188eufw_blob.c

DRV_SRCS := \
	$(DRV)/core.c \
	$(DRV)/8188e.c \
	$(DRV)/8188f.c \
	$(DRV)/8192c.c \
	$(DRV)/8192e.c \
	$(DRV)/8192f.c \
	$(DRV)/8710b.c \
	$(DRV)/8723a.c \
	$(DRV)/8723b.c

PRINTF_SRCS := $(ROOT)/components/printf/printf.c
PRINTF_CFLAGS := -DPRINTF_DISABLE_SUPPORT_FLOAT -DPRINTF_DISABLE_SUPPORT_EXPONENTIAL

ALL_SRCS := $(BASE_SRCS) $(TX_SRCS) $(COMMON_SRCS) $(SHELL_SRCS) $(TESTS_SRCS) \
	$(VENDORED_SRCS) $(ALLOC_SRCS) $(USB_BOARD_SRCS) $(USB_STACK_SRCS) \
	$(NET80211_SRCS) $(NET80211_PORT_SRCS) $(BRIDGE_BSD_SRCS) $(LKPI_SRCS) \
	$(FW_SRCS) $(DRV_SRCS) $(PRINTF_SRCS)

OBJS := $(addprefix $(BUILD)/obj/,$(addsuffix .o,$(patsubst $(ROOT)/%,%,$(ALL_SRCS))))

.PHONY: all check guards clean FORCE
all: $(TARGET).bin check

define COMPILE_RULE
$(BUILD)/obj/$(patsubst $(ROOT)/%,%,$(1)).o: $(1) $(TAG_STAMP)
	@mkdir -p $$(dir $$@)
	@echo "  CC $$<"
	@$(CC) $(if $(filter %.S,$(1)),$(ASM_CFLAGS),$(if $(filter $(1),$(VENDORED_SRCS) $(USB_STACK_SRCS) $(NET80211_SRCS)),$(VENDORED_CFLAGS),$(CFLAGS))) $(2) $(INCLUDES) -c $$< -o $$@
endef

$(foreach s,$(PRINTF_SRCS),$(eval $(call COMPILE_RULE,$(s),$(PRINTF_CFLAGS))))
$(foreach s,$(BASE_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(TX_SRCS),$(eval $(call COMPILE_RULE,$(s),$(TX_CFLAGS))))
$(foreach s,$(COMMON_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(SHELL_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(TESTS_SRCS),$(eval $(call COMPILE_RULE,$(s),$(WLAN_KPI_CFLAGS) $(USB_INCLUDES))))
$(foreach s,$(VENDORED_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(ALLOC_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
# the freestanding libc completion block (abs and friends) is part of this
# image's C surface: the driver and stack reference the symbols
$(eval $(call COMPILE_RULE,$(ROOT)/os/baremetal/minilibc.c,-DCONFIG_LIBC_COMPLETION))
$(foreach s,$(USB_BOARD_SRCS),$(eval $(call COMPILE_RULE,$(s),$(USB_INCLUDES))))
$(foreach s,$(USB_STACK_SRCS),$(eval $(call COMPILE_RULE,$(s),$(USB_INCLUDES))))
$(foreach s,$(NET80211_SRCS),$(eval $(call COMPILE_RULE,$(s),$(WLAN_BSD_CFLAGS))))
$(foreach s,$(NET80211_PORT_SRCS),$(eval $(call COMPILE_RULE,$(s),$(WLAN_BSD_CFLAGS))))
$(foreach s,$(BRIDGE_BSD_SRCS),$(eval $(call COMPILE_RULE,$(s),$(WLAN_BSD_CFLAGS))))
$(foreach s,$(LKPI_SRCS),$(eval $(call COMPILE_RULE,$(s),$(WLAN_KPI_CFLAGS) $(USB_INCLUDES))))
$(foreach s,$(FW_SRCS),$(eval $(call COMPILE_RULE,$(s),$(WLAN_KPI_CFLAGS))))
$(foreach s,$(DRV_SRCS),$(eval $(call COMPILE_RULE,$(s),$(WLAN_DRV_CFLAGS))))

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
	@test -f $(USB_PORT)/board/rk3568-itx/usb_config.h \
		|| { echo "FAIL: the USB board configuration header is missing"; exit 1; }
	@test -f $(NPORT)/compat/prelude.h \
		|| { echo "FAIL: the net80211 port prelude is missing"; exit 1; }
	@echo "$(WLAN_BSD_CFLAGS)" | grep -q -- '-D_KERNEL' \
		|| { echo "FAIL: -D_KERNEL missing from WLAN_BSD_CFLAGS"; exit 1; }
	@echo "$(WLAN_BSD_CFLAGS)" | grep -q -- '-DWLAN_PORT_TARGET' \
		|| { echo "FAIL: -DWLAN_PORT_TARGET missing (the port would take host branches)"; exit 1; }
	@if ls $(ROOT)/patches/cherryusb/*.patch >/dev/null 2>&1; then \
		case "$(CHERRYUSB)" in \
		$(ROOT)/third-party/cherryusb) \
			echo "FAIL: patches exist but the USB stack would compile from the vendored tree"; exit 1 ;; \
		esac; \
	fi
	@if echo "$(ALL_SRCS)" | tr ' ' '\n' | grep -qE "^$(ROOT)/os/threadx/(cmsis2_host|heap|tx_glue)\.c$$"; then \
		echo "FAIL: a host-side os/threadx file entered the compile set"; exit 1; \
	fi
	@[ "$$(basename $(CURDIR))" = "$(SUITE_NAME)" ] \
		|| { echo "FAIL: SUITE_NAME ($(SUITE_NAME)) is not the directory name"; exit 1; }
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
	         usb_board_init usbh_initialize \
	         lkpi_usb_init lkpi_usb_cherryusb_init lkpi_fw_rtl8188eufw_register \
	         wlan_seam_adapter_init wlan_bridge_scan_start \
	         ieee80211_ifattach ieee80211_input rtl8xxxu_read8 \
	         rtl8188eufw_data; do \
		$(NM) $(TARGET).elf | awk '{print $$3}' | grep -qx "$$s" \
			|| { echo "FAIL: $$s not in the image"; exit 1; }; \
	done; \
	undef=$$($(NM) -u $(TARGET).elf); \
	[ -z "$$undef" ] || { echo "FAIL: undefined symbols:"; echo "$$undef"; exit 1; }; \
	echo "PASS: _start=0x$$start, WLAN/USB/kernel/board symbols present, 0 undefined"
	@echo "== module-section density =="
	@set -e; \
	lm=$$($(NM) $(TARGET).elf | awk '$$3=="__lkpi_module_end"{print $$1}'); \
	lm0=$$($(NM) $(TARGET).elf | awk '$$3=="__lkpi_module_start"{print $$1}'); \
	bt=$$($(NM) $(TARGET).elf | awk '$$3=="__board_tests_end"{print $$1}'); \
	bt0=$$($(NM) $(TARGET).elf | awk '$$3=="__board_tests_start"{print $$1}'); \
	lmz=$$(( 0x$$lm - 0x$$lm0 )); btz=$$(( 0x$$bt - 0x$$bt0 )); \
	echo "lkpi_module bytes=$$lmz (expect 8*1=8), board_tests bytes=$$btz"; \
	[ $$lmz -eq 8 ] || { echo "FAIL: the module section does not carry exactly one driver pointer"; exit 1; }; \
	[ $$(( btz % 16 )) -eq 0 ] || { echo "FAIL: BOARD_TESTS is not a whole number of entries"; exit 1; }; \
	echo "PASS: module section carries one registered driver; test table dense"
	@echo "== case-name reconciliation (image / suite.mk / judge) =="
	@sh $(COMMON)/check-case-list.sh $(TARGET).elf $(CURDIR)/suite.mk $(CURDIR)/judge.py $(OBJCOPY) $(READELF) $(BUILD)
	@$(SIZE) $(TARGET).elf
	@echo "build-tag: $(BUILD_TAG)"

clean:
	rm -rf $(BUILD)

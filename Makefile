# freewebcam bare-metal image (aarch64, RK3568-itx)
#
# SPEC: ../specs/aarch64-baremetal-boot/SPEC.md (v3) - flat raw binary,
# load = link = 0xa000000, entered with U-Boot `go 0xa000000` (bootelf is
# unusable on this board; its ELF path copies no segments either).
#
#   make CONFIG=mmu all      build (default CONFIG=mmu)
#   make CONFIG=mmu gates    vendor-trace + print-discipline scans
#   make CONFIG=mmu check    build self-checks (_start position, .bss NOBITS,
#                            no libgcc helpers)
#   make CONFIG=mmu deploy   sha256 chain + size/crc32 + copy to the TFTP root
#
# One build directory per configuration: switching CONFIG= never reuses the
# previous image's objects (multi-config discipline, single-variable
# debugging).

CONFIG ?= mmu
CONFIG_FILE := configs/$(CONFIG).config
ifeq ($(wildcard $(CONFIG_FILE)),)
$(error unknown CONFIG=$(CONFIG); available: $(patsubst configs/%.config,%,$(wildcard configs/*.config)))
endif
include $(CONFIG_FILE)

# Bare-metal toolchain, set explicitly (non-interactive shells do not source
# ~/.bashrc - silently picking up a Linux-targeted compiler links against
# glibc assumptions that cannot work here).
AARCH64_CROSS_PATH ?= $(HOME)/workspace/env/src/xpack-aarch64-none-elf-gcc-13.2.1-1.1
CROSS_COMPILE ?= $(AARCH64_CROSS_PATH)/bin/aarch64-none-elf-
CC      := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump
READELF := $(CROSS_COMPILE)readelf
NM      := $(CROSS_COMPILE)nm
SIZE    := $(CROSS_COMPILE)size

BOARD     := rk3568-itx
BUILD     := build/baremetal-$(CONFIG)
TARGET    := $(BUILD)/baremetal
TFTP_FILE := /mnt/d/tftpboot/rtos.bin

# App entry by feature line (S5/cherryusb-xhci-port §4/P5-a): the USB
# config swaps the smoke main for the enumeration bring-up. Resolved
# before SRCS (immediate expansion); the USB source wiring lives in the
# USB block below.
APP_MAIN := app/hello_world/main.c
ifeq ($(CONFIG_USBHOST),y)
APP_MAIN := app/cherryusb_enum/main.c
endif
ifeq ($(CONFIG_LWIP),y)
APP_MAIN := app/lwip_lan/main.c
endif
# S5a W1 (specs/rtl8821cu-vendor-driver/SPEC.md v2): the wlan image swaps in
# the wlan8821 bring-up main - the same usb-domain start sequence as
# cherryusb_enum with the rtl8821cu driver registered instead of the udisk
# acceptance driver. Takes precedence (the wlan config is USBHOST-based).
ifeq ($(CONFIG_NIC_RTL8821CU),y)
APP_MAIN := app/wlan8821/main.c
endif
# S5b W1 (specs/rtl8851bu-vendor-driver/SPEC.md v3.2): the wlan8851
# bring-up main - same shape, no modeswitch tier (0bda:b851 direct).
ifeq ($(CONFIG_NIC_RTL8851BU),y)
APP_MAIN := app/wlan8851/main.c
endif

# OS layer selection (SPEC S3/threadx-up-boot §3.1.3): baremetal keeps the
# timebase-only cmsis2 shim; threadx swaps in the ThreadX UP kernel and the
# cmsis2 baremetal shim leaves the image (cmsis2_threadx.c joins in T3).
CONFIG_OS ?= baremetal
ifeq ($(filter $(CONFIG_OS),baremetal threadx),)
$(error CONFIG_OS must be baremetal or threadx, got '$(CONFIG_OS)')
endif

SRCS := \
	arch/aarch64/start.S \
	arch/aarch64/mmu.c \
	arch/aarch64/cache.c \
	arch/aarch64/gicv3.c \
	os/baremetal/memops.c \
	os/baremetal/minilibc.c \
	os/baremetal/log.c \
	os/baremetal/tick.c \
	board/$(BOARD)/drivers/uart.c \
	board/$(BOARD)/drivers/cpu_boost.c \
	board/$(BOARD)/param.c \
	$(APP_MAIN)

ifneq ($(CONFIG_OS),threadx)
SRCS += os/cmsis2/cmsis2_shim.c
endif

# -mgeneral-regs-only: the image saves no FP state, so any FP instruction is
#   a bug we want the compiler to prevent rather than discover as corruption.
# -mno-outline-atomics: a -nostdlib image has no library for __aarch64_*
#   helpers; inlined LDXR/STXR loops need none.
# -mstrict-align: the compiler must not generate unaligned accesses on its
#   own (Device mappings fault on them, and Normal ones are a debug hazard).
CONFIG_OPT ?= -Og
CFLAGS := $(CONFIG_OPT) -g3 -std=c11 -Wall -Wextra \
	-ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	-march=armv8-a -mgeneral-regs-only -mstrict-align -mno-outline-atomics \
	-Iarch/aarch64 -Iboard/$(BOARD) -Iboard/$(BOARD)/drivers -Ios/baremetal \
	-Ios/cmsis2 -Icomponents/cmsis5 -Icomponents/tlsf

# CMSIS2 shim identity (SPEC v3 §3.7.2): the string osKernelGetInfo reports;
# bumped per round so the anchor line carries the build's provenance.
CONFIG_CMSIS2_VERSION ?= baremetal-r4
CFLAGS += -DCONFIG_CMSIS2_VERSION='"$(CONFIG_CMSIS2_VERSION)"'

# Tick source (SPEC v3 §3.5.3 / §4/R2 判据 5): default CNTV (PPI 27);
# CONFIG_TICK_SOURCE=phys_ns selects the EL1 ns physical timer (CNTP, PPI 30)
# as the R2 adjudication fallback. Sits after the CFLAGS := above on purpose.
CONFIG_TICK_SOURCE ?= virt
ifeq ($(filter $(CONFIG_TICK_SOURCE),virt phys_ns),)
$(error CONFIG_TICK_SOURCE must be virt or phys_ns, got '$(CONFIG_TICK_SOURCE)')
endif
ifeq ($(CONFIG_TICK_SOURCE),phys_ns)
CFLAGS += -DTICK_SOURCE_PHYS_NS=1
endif

# gdb stub (SPEC v3 §3.6): CONFIG_GDB_STUB=y builds the RSP-over-UART2 stub,
# the boot_sync_entry vector path and the glue; the base image's sync slots
# stay on the fatal-park path.
CONFIG_GDB_STUB ?= n
ifeq ($(filter $(CONFIG_GDB_STUB),y n),)
$(error CONFIG_GDB_STUB must be y or n, got '$(CONFIG_GDB_STUB)')
endif
ifeq ($(CONFIG_GDB_STUB),y)
SRCS += \
	arch/aarch64/gdb/gdb_main.c \
	arch/aarch64/gdb/gdb_packet.c \
	arch/aarch64/gdb/gdb_arch.c \
	os/baremetal/gdb_glue.c
CFLAGS += -DCONFIG_GDB_STUB=1 -Iarch/aarch64/gdb
endif

# UART RX interrupt reception (SPEC S4/cherrysh-adapt §3.4): CONFIG_UART_RX_IRQ=y
# adds the armed-reception extension to the console driver; the polled path
# above is untouched. The shell requires it; baremetal configs keep it off.
CONFIG_UART_RX_IRQ ?= n
ifeq ($(filter $(CONFIG_UART_RX_IRQ),y n),)
$(error CONFIG_UART_RX_IRQ must be y or n, got '$(CONFIG_UART_RX_IRQ)')
endif
ifeq ($(CONFIG_UART_RX_IRQ),y)
CFLAGS += -DCONFIG_UART_RX_IRQ=1
endif

# CherrySH console shell (SPEC S4/cherrysh-adapt): exists ONLY under
# CONFIG_OS=threadx (N11 - the adapter is hard-dependent on osThread/osThreadFlags*).
# CONFIG_SHELL=0 (or =n) drops the shell and every command TU with it:
# commands register through the FSymTab section, so a command is compiled in
# iff its translation unit is.
CONFIG_SHELL ?= n
ifeq ($(filter $(CONFIG_SHELL),y n),)
$(error CONFIG_SHELL must be y or n, got '$(CONFIG_SHELL)')
endif
CONFIG_SHELL_STACK ?= 4096

# Debug knob for the shell RX bring-up: periodic uart_console_rx_diag lines.
ifeq ($(CONFIG_SHELL_RX_DIAG),y)
CFLAGS += -DSHELL_RX_DIAG=1
endif

# N7-stab forensics (2026-10-02 sustained-TX freeze round), three independent
# single-factor knobs:
#   GMAC_IRQ_BREAKER - drop AIE when unserviced status bits storm (>=32 odd
#                      handler passes per 100 ms); converts a livelock into a
#                      degraded-but-alive system plus one emergency log line.
#   GMAC_TICK_MON    - CNTP watchdog above every other line; prints the GMAC
#                      register crime scene when the kernel tick stalls.
#   GMAC_FAST_PROBE  - 10 Hz thread-state probe (first 60 s), run counters of
#                      every thread on one line.
ifeq ($(CONFIG_GMAC_IRQ_BREAKER),y)
CFLAGS += -DCONFIG_GMAC_IRQ_BREAKER=1
endif
ifeq ($(CONFIG_GMAC_TICK_MON),y)
CFLAGS += -DCONFIG_GMAC_TICK_MON=1
endif
ifeq ($(CONFIG_GMAC_FAST_PROBE),y)
CFLAGS += -DCONFIG_GMAC_FAST_PROBE=1
endif

ifeq ($(CONFIG_OS),threadx)
ifeq ($(CONFIG_SHELL),y)
SHELL_SRCS := \
	components/cherrysh/chry_shell.c \
	components/cherrysh/builtin/help.c \
	components/cherrysh/builtin/clear.c \
	components/cherrysh/builtin/shsize.c \
	components/cherrysh/cherryrl/chry_readline.c \
	components/cherryrb/chry_ringbuffer.c \
	port/adapters/cherrysh/cherrysh_adapter.c \
	board/$(BOARD)/reset.c
SRCS += $(SHELL_SRCS)
# Shadow-config include order is load-bearing (§3.8.2): port/adapters/cherrysh
# must come before components/cherrysh so csh_config.h (the real config) beats
# nothing - upstream ships only the template, but the order discipline does
# not depend on that accident.
CSH_CFLAGS := -Iport/adapters/cherrysh -Icomponents/cherrysh \
	-Icomponents/cherrysh/cherryrl -Icomponents/cherryrb \
	-include os/baremetal/minilibc.h \
	-DCONFIG_SHELL=1 -DCONFIG_SHELL_STACK=$(CONFIG_SHELL_STACK)
$(BUILD)/components/%.o: EXTRAFLAGS := $(CSH_CFLAGS)
$(BUILD)/port/%.o: EXTRAFLAGS := $(CSH_CFLAGS)
# main.o needs cherrysh_adapter.h (the shellboot task).
$(BUILD)/app/%.o: EXTRAFLAGS := $(CSH_CFLAGS)
endif
endif


# ThreadX UP kernel (SPEC S3/threadx-up-boot §3.1, FR-1/FR-2): upstream
# common + the cortex_a55 GNU port .S files (the port's fp_enable/fp_disable
# .c files are NOT built - no FP state exists under -mgeneral-regs-only) +
# the adapter layer. tx include paths and port defines ride EXTRAFLAGS on
# the tx objects only - the tx headers never leak into base-layer compile
# units (FR-3 layering invariant). Sits after the global CFLAGS :=
# definition on purpose (the -DTHREADX_BUILD append must not be wiped).
ifeq ($(CONFIG_OS),threadx)
TX_DIR := third-party/threadx
TX_PORT_DIR := $(TX_DIR)/ports/cortex_a55/gnu
TX_KERNEL_SRCS := $(wildcard $(TX_DIR)/common/src/*.c)
TX_PORT_SRCS := $(wildcard $(TX_PORT_DIR)/src/*.S)
TX_ADAPT_SRCS := os/threadx/tx_glue.c os/threadx/tx_vectors.S \
	os/threadx/tx_up_shim.c os/threadx/heap.c
TX_CMSIS_SRCS := os/cmsis2/cmsis2_threadx.c
SRCS += $(TX_KERNEL_SRCS) $(TX_PORT_SRCS) $(TX_ADAPT_SRCS) $(TX_CMSIS_SRCS)
# tlsf needs no tx header: plain freestanding compile, global flags.
SRCS += components/tlsf/tlsf.c
CFLAGS += -DTHREADX_BUILD=1

CONFIG_TX_TICKS_PER_SECOND ?= 1000
CONFIG_TX_TIMER_STACK ?= 4096
CONFIG_TX_MAX_THREADS ?= 16
CONFIG_HEAP_BYTES ?= 0x400000
TX_CFLAGS := -I$(TX_DIR)/common/inc -I$(TX_PORT_DIR)/inc -Ios/threadx \
	-DTHREADX_UP_BUILD=1 -DTX_INCLUDE_USER_DEFINE_FILE -DEL1=1 \
	-DCONFIG_TX_TICKS_PER_SECOND=$(CONFIG_TX_TICKS_PER_SECOND) \
	-DCONFIG_TX_TIMER_STACK=$(CONFIG_TX_TIMER_STACK) \
	-DCONFIG_TX_MAX_THREADS=$(CONFIG_TX_MAX_THREADS) \
	-DCONFIG_HEAP_BYTES=$(CONFIG_HEAP_BYTES)
# -DEL1: the port picks its exception-return register bank (SPSR/ELR_EL1 vs
# _EL2 vs _EL3) from this compile-time macro - undefined, it silently emits
# the EL3 variant and the first context switch dies on an undefined MSR at
# EL1 (EC=0 sync fault; board-proven 2026-09-30). Same key as the prior
# project's port build (its Makefile:229-240).
# Pattern-scoped: kernel, port, adapter AND the threadx cmsis2 shim compile
# with the tx flag set on top of the global CFLAGS; everything else (incl.
# tlsf) never sees it - tx headers stay inside the adapter units (FR-3).
# ThreadX-scoped on purpose: a bare third-party/%.o pattern would swallow
# the cherryusb objects the USB block below adds (and its longer stem would
# win the specificity race over their own rule).
$(BUILD)/third-party/threadx/%.o: EXTRAFLAGS := $(TX_CFLAGS)
$(BUILD)/os/threadx/%.o: EXTRAFLAGS := $(TX_CFLAGS)
$(BUILD)/os/cmsis2/%.o: EXTRAFLAGS := $(TX_CFLAGS)
endif

# CherryUSB host stack (SPEC S5/cherryusb-xhci-port §3.3/§3.4): the vendored
# core + hub class + the xHCI port (patches/cherryusb/0002, replayed by
# `make modules`) + the adapter layer (port/adapters/cherryusb) + the
# shared domain sequence (port/adapters/usb). ThreadX-only (the osal is
# ThreadX-native). Include order is load-bearing: port/adapters/cherryusb
# FIRST so the shadow usb_config.h beats upstream's config template; the
# tx include paths ride EXTRAFLAGS on usb_osal_threadx.o only (FR-3).
CONFIG_USBHOST ?= n
ifeq ($(filter $(CONFIG_USBHOST),y n),)
$(error CONFIG_USBHOST must be y or n, got '$(CONFIG_USBHOST)')
endif
ifeq ($(CONFIG_USBHOST),y)
ifneq ($(CONFIG_OS),threadx)
$(error CONFIG_USBHOST=y requires CONFIG_OS=threadx (the cherryusb osal is ThreadX-native))
endif
CONFIG_USBHOST_XHCI ?= n
ifeq ($(filter $(CONFIG_USBHOST_XHCI),y n),)
$(error CONFIG_USBHOST_XHCI must be y or n, got '$(CONFIG_USBHOST_XHCI)')
endif
CONFIG_USBHOST_XHCI_START_NUM ?= 1

CHY_DIR := third-party/cherryusb
USB_CORE_SRCS := $(CHY_DIR)/core/usbh_core.c $(CHY_DIR)/class/hub/usbh_hub.c
USB_HCD_SRCS :=
ifeq ($(CONFIG_USBHOST_XHCI),y)
USB_HCD_SRCS += $(CHY_DIR)/port/xhci/usb_hc_xhci.c
endif
# EHCI joins at P5c (flip CONFIG_USBHOST_EHCI in the config; the adapter
# halves are already keyed on it).
CONFIG_USBHOST_EHCI ?= n
USB_ADAPTER_SRCS := \
	port/adapters/cherryusb/usb_osal_threadx.c \
	port/adapters/cherryusb/usbh_glue.c \
	port/adapters/cherryusb/usbh_platform.c \
	port/adapters/usb/usb_domain.c \
	port/adapters/usb/usb_host_cherryusb.c
ifeq ($(CONFIG_USBHOST_EHCI),y)
USB_HCD_SRCS += $(CHY_DIR)/port/ehci/usb_hc_ehci.c
endif
SRCS += $(USB_CORE_SRCS) $(USB_HCD_SRCS) $(USB_ADAPTER_SRCS)

USB_CFLAGS := -Iport/adapters/cherryusb -Iport/adapters/usb \
	-I$(CHY_DIR)/core -I$(CHY_DIR)/common -I$(CHY_DIR)/class/hub \
	-I$(CHY_DIR)/class/msc \
	-I$(CHY_DIR)/port/xhci -I$(CHY_DIR)/port/ehci \
	-include os/baremetal/minilibc.h \
	-DCONFIG_USBHOST_XHCI=$(if $(filter y,$(CONFIG_USBHOST_XHCI)),1,0) \
	-DCONFIG_USBHOST_XHCI_START_NUM=$(CONFIG_USBHOST_XHCI_START_NUM)
ifeq ($(CONFIG_USBHOST_EHCI),y)
USB_CFLAGS += -DCONFIG_USBHOST_EHCI=1
endif

# MSC (P5-e, user ruling 2026-10-01: adapt the upstream class, no local BOT
# implementation): cherryusb class/msc + our adapter (shell commands are the
# acceptance toolbox, so the adapter TU joins with the shell flag set).
CONFIG_USBHOST_MSC ?= n
ifeq ($(CONFIG_USBHOST_MSC),y)
ifeq ($(filter y,$(CONFIG_USBHOST_XHCI)),)
$(error CONFIG_USBHOST_MSC=y currently requires the xHCI build (P5-d slice))
endif
USB_CFLAGS += -DCONFIG_USBHOST_MSC=1
SRCS += $(CHY_DIR)/class/msc/usbh_msc.c
SRCS += port/adapters/cherryusb/usbh_msc.c
$(BUILD)/port/adapters/cherryusb/usbh_msc.o: \
	EXTRAFLAGS := $(USB_CFLAGS) $(CSH_CFLAGS)
endif

# Exact-file rules win over the shell block's port/% pattern; the osal
# additionally gets the tx include set (tx_api.h). The cmds file is the
# shell-facing toolbox - it joins only with the shell (csh.h headers) and
# rides the shell flag set on top.
$(BUILD)/port/adapters/cherryusb/usb_osal_threadx.o: \
	EXTRAFLAGS := $(USB_CFLAGS) $(TX_CFLAGS)
$(BUILD)/port/adapters/cherryusb/usbh_glue.o: EXTRAFLAGS := $(USB_CFLAGS)
$(BUILD)/port/adapters/cherryusb/usbh_platform.o: EXTRAFLAGS := $(USB_CFLAGS)
$(BUILD)/port/adapters/usb/%.o: EXTRAFLAGS := $(USB_CFLAGS)
$(BUILD)/third-party/cherryusb/%.o: EXTRAFLAGS := $(USB_CFLAGS)
$(BUILD)/app/cherryusb_enum/main.o: EXTRAFLAGS := $(CSH_CFLAGS) $(USB_CFLAGS)
ifeq ($(CONFIG_SHELL),y)
SRCS += port/adapters/cherryusb/usbh_cmds.c
$(BUILD)/port/adapters/cherryusb/usbh_cmds.o: \
	EXTRAFLAGS := $(USB_CFLAGS) $(CSH_CFLAGS)
endif
endif

# linuxkpi (SPEC S6/linuxkpi §4/P6-a): the Linux kernel API compat layer
# over the cherryusb engine (components/linuxkpi) + the claim hook and the
# usbkpi shell exit (port/adapters/linuxkpi) + the acceptance driver's
# RTOS shell (app/lkpi_udisk). Needs the USB host stack (it submits into
# usbh) and the shell (the acceptance toolbox). Include order: cherryusb
# dirs first (usb_def.h sets USB_DEF_H so the descriptor structs are
# shared), then the linuxkpi headers for <linux/*.h>.
CONFIG_LINUXKPI ?= n
ifeq ($(filter $(CONFIG_LINUXKPI),y n),)
$(error CONFIG_LINUXKPI must be y or n, got '$(CONFIG_LINUXKPI)')
endif
ifeq ($(CONFIG_LINUXKPI),y)
ifeq ($(filter y,$(CONFIG_USBHOST)),)
$(error CONFIG_LINUXKPI=y requires CONFIG_USBHOST=y)
endif
ifeq ($(filter y,$(CONFIG_SHELL)),)
$(error CONFIG_LINUXKPI=y requires CONFIG_SHELL=y (the acceptance toolbox))
endif
LKPI_DIR := components/linuxkpi
LKPI_SRCS := \
	$(LKPI_DIR)/linuxkpi_core.c \
	$(LKPI_DIR)/linuxkpi_usb.c \
	$(LKPI_DIR)/fw_registry.c \
	port/adapters/linuxkpi/lkpi_usbh_class.c \
	port/adapters/linuxkpi/usbkpi_cmds.c
# The udisk/hub acceptance drivers ride their own key (single-factor
# discipline, S5a W1): the wlan image takes the linuxkpi base without them.
CONFIG_LINUXKPI_UDISK ?= n
ifeq ($(filter $(CONFIG_LINUXKPI_UDISK),y n),)
$(error CONFIG_LINUXKPI_UDISK must be y or n, got '$(CONFIG_LINUXKPI_UDISK)')
endif
ifeq ($(CONFIG_LINUXKPI_UDISK),y)
LKPI_SRCS += \
	app/lkpi_udisk/lkpi_udisk_core.c \
	app/lkpi_udisk/lkpi_udisk_rtos.c \
	app/lkpi_hub/lkpi_hub_core.c \
	app/lkpi_hub/lkpi_hub_rtos.c
endif
SRCS += $(LKPI_SRCS)
CFLAGS += -DCONFIG_LINUXKPI=1
# P6-b root-cause experiment key (temporary, issues/2026-10-01-xhci-
# bulk-td-not-fetched-under-lkpi-shape): ud_post_probe fires one bulk
# pattern round from the hub-thread probe context (the msc reference
# context) instead of waiting for the shell. Removing the key from the
# config needs make clean-config (the key-delete direction is not
# stamp-guarded - run ledger lesson 2026-10-01).
CONFIG_LINUXKPI_PROBE_BULK ?= n
ifeq ($(CONFIG_LINUXKPI_PROBE_BULK),y)
CFLAGS += -DCONFIG_LINUXKPI_PROBE_BULK=1
endif
# [S5a] W3: bulk IN rides the async done-list chain (the wlan inirp's 8
# RX urbs cannot burn a 5s sync round each); stage small slots must cover
# the RX urbs' permanent stage holdings (CONFIG_LINUXKPI_STAGE_SMALL_N=18
# on the wlan lane = 16 RX + transient, see linuxkpi_usb.c).
CONFIG_LINUXKPI_USB_RX_ASYNC ?= n
ifeq ($(filter $(CONFIG_LINUXKPI_USB_RX_ASYNC),y n),)
$(error CONFIG_LINUXKPI_USB_RX_ASYNC must be y or n, got '$(CONFIG_LINUXKPI_USB_RX_ASYNC)')
endif
CONFIG_LINUXKPI_STAGE_SMALL_N ?= 2
ifeq ($(CONFIG_LINUXKPI_USB_RX_ASYNC),y)
CFLAGS += -DCONFIG_LINUXKPI_USB_RX_ASYNC=1
endif
# TX include set rides along: the impl layer embeds ThreadX primitives
# (semaphores/timers) exactly like the cherryusb osal does (FR-3: tx
# headers stay inside adapter/implementation units).
LKPI_CFLAGS := $(USB_CFLAGS) $(TX_CFLAGS) -I$(LKPI_DIR)/include \
	-I$(LKPI_DIR) -include os/baremetal/minilibc.h \
	-DLKPI_SMALL_SLOTS=$(CONFIG_LINUXKPI_STAGE_SMALL_N)
$(BUILD)/components/linuxkpi/%.o: EXTRAFLAGS := $(LKPI_CFLAGS)
$(BUILD)/port/adapters/linuxkpi/%.o: \
	EXTRAFLAGS := $(LKPI_CFLAGS) $(CSH_CFLAGS)
ifeq ($(CONFIG_LINUXKPI_UDISK),y)
$(BUILD)/app/lkpi_udisk/%.o: EXTRAFLAGS := $(LKPI_CFLAGS) $(CSH_CFLAGS)
$(BUILD)/app/lkpi_hub/%.o: EXTRAFLAGS := $(LKPI_CFLAGS) $(CSH_CFLAGS)
endif
endif

# --- the rtl8821cu vendor driver (S5a W1, feat-only - never merges) ----------
# The Realtek out-of-tree rtl8821cu driver (submodule third-party/rtl8821cu,
# GPL-2.0, pin 8df92c5) consumed through the linuxkpi vendor tier ([S6]
# §3.4 B1-B8): os_dep 7 units + core/hal/phydm compile VERBATIM against the
# linuxkpi headers (zero-modification rule, [S5a] §3.2.3); the ioctl/proc/
# cfg80211 half is config'd out. Compile set locked to the [FWC] Makefile
# :898-1011 lists + osdep_service.c (spec v2 addition, LINUX branch).
CONFIG_NIC_RTL8821CU ?= n
ifeq ($(filter $(CONFIG_NIC_RTL8821CU),y n),)
$(error CONFIG_NIC_RTL8821CU must be y or n, got '$(CONFIG_NIC_RTL8821CU)')
endif
ifeq ($(CONFIG_NIC_RTL8821CU),y)
ifeq ($(filter y,$(CONFIG_LINUXKPI)),)
$(error CONFIG_NIC_RTL8821CU=y requires CONFIG_LINUXKPI=y)
endif
RTL8821CU_ROOT := third-party/rtl8821cu

RTL8821CU_CORE_SRCS := \
	$(RTL8821CU_ROOT)/core/rtw_cmd.c \
	$(RTL8821CU_ROOT)/core/rtw_security.c \
	$(RTL8821CU_ROOT)/core/rtw_debug.c \
	$(RTL8821CU_ROOT)/core/rtw_io.c \
	$(RTL8821CU_ROOT)/core/rtw_ioctl_query.c \
	$(RTL8821CU_ROOT)/core/rtw_ioctl_set.c \
	$(RTL8821CU_ROOT)/core/rtw_mp.c \
	$(RTL8821CU_ROOT)/core/rtw_mp_ioctl.c \
	$(RTL8821CU_ROOT)/core/rtw_ieee80211.c \
	$(RTL8821CU_ROOT)/core/rtw_mlme.c \
	$(RTL8821CU_ROOT)/core/rtw_mlme_ext.c \
	$(RTL8821CU_ROOT)/core/rtw_mi.c \
	$(RTL8821CU_ROOT)/core/rtw_wlan_util.c \
	$(RTL8821CU_ROOT)/core/rtw_vht.c \
	$(RTL8821CU_ROOT)/core/rtw_pwrctrl.c \
	$(RTL8821CU_ROOT)/core/rtw_rf.c \
	$(RTL8821CU_ROOT)/core/rtw_recv.c \
	$(RTL8821CU_ROOT)/core/rtw_sta_mgt.c \
	$(RTL8821CU_ROOT)/core/rtw_ap.c \
	$(RTL8821CU_ROOT)/core/rtw_xmit.c \
	$(RTL8821CU_ROOT)/core/rtw_p2p.c \
	$(RTL8821CU_ROOT)/core/rtw_tdls.c \
	$(RTL8821CU_ROOT)/core/rtw_iol.c \
	$(RTL8821CU_ROOT)/core/rtw_sreset.c \
	$(RTL8821CU_ROOT)/core/rtw_btcoex_wifionly.c \
	$(RTL8821CU_ROOT)/core/rtw_btcoex.c \
	$(RTL8821CU_ROOT)/core/rtw_beamforming.c \
	$(RTL8821CU_ROOT)/core/rtw_odm.c \
	$(RTL8821CU_ROOT)/core/rtw_mem.c \
	$(RTL8821CU_ROOT)/core/efuse/rtw_efuse.c

RTL8821CU_HAL_SRCS := \
	$(RTL8821CU_ROOT)/hal/hal_intf.c \
	$(RTL8821CU_ROOT)/hal/hal_com.c \
	$(RTL8821CU_ROOT)/hal/hal_com_phycfg.c \
	$(RTL8821CU_ROOT)/hal/hal_phy.c \
	$(RTL8821CU_ROOT)/hal/hal_dm.c \
	$(RTL8821CU_ROOT)/hal/hal_btcoex_wifionly.c \
	$(RTL8821CU_ROOT)/hal/hal_btcoex.c \
	$(RTL8821CU_ROOT)/hal/hal_mp.c \
	$(RTL8821CU_ROOT)/hal/hal_mcc.c \
	$(RTL8821CU_ROOT)/hal/hal_hci/hal_usb.c \
	$(RTL8821CU_ROOT)/hal/led/hal_usb_led.c \
	$(RTL8821CU_ROOT)/hal/hal_halmac.c \
	$(RTL8821CU_ROOT)/hal/halmac/halmac_api.c \
	$(RTL8821CU_ROOT)/hal/halmac/halmac_88xx/halmac_api_88xx.c \
	$(RTL8821CU_ROOT)/hal/halmac/halmac_88xx/halmac_func_88xx.c \
	$(RTL8821CU_ROOT)/hal/halmac/halmac_88xx/halmac_api_88xx_usb.c \
	$(RTL8821CU_ROOT)/hal/halmac/halmac_88xx/halmac_api_88xx_sdio.c \
	$(RTL8821CU_ROOT)/hal/halmac/halmac_88xx/halmac_api_88xx_pcie.c \
	$(RTL8821CU_ROOT)/hal/halmac/halmac_88xx/halmac_8821c/halmac_8821c_pwr_seq.c \
	$(RTL8821CU_ROOT)/hal/halmac/halmac_88xx/halmac_8821c/halmac_api_8821c.c \
	$(RTL8821CU_ROOT)/hal/halmac/halmac_88xx/halmac_8821c/halmac_func_8821c.c \
	$(RTL8821CU_ROOT)/hal/halmac/halmac_88xx/halmac_8821c/halmac_api_8821c_usb.c \
	$(RTL8821CU_ROOT)/hal/halmac/halmac_88xx/halmac_8821c/halmac_api_8821c_sdio.c \
	$(RTL8821CU_ROOT)/hal/halmac/halmac_88xx/halmac_8821c/halmac_api_8821c_pcie.c \
	$(RTL8821CU_ROOT)/hal/halmac/halmac_88xx/halmac_8821c/halmac_8821c_phy.c \
	$(RTL8821CU_ROOT)/hal/rtl8821c/rtl8821c_halinit.c \
	$(RTL8821CU_ROOT)/hal/rtl8821c/rtl8821c_mac.c \
	$(RTL8821CU_ROOT)/hal/rtl8821c/rtl8821c_cmd.c \
	$(RTL8821CU_ROOT)/hal/rtl8821c/rtl8821c_phy.c \
	$(RTL8821CU_ROOT)/hal/rtl8821c/rtl8821c_dm.c \
	$(RTL8821CU_ROOT)/hal/rtl8821c/rtl8821c_ops.c \
	$(RTL8821CU_ROOT)/hal/rtl8821c/hal8821c_fw.c \
	$(RTL8821CU_ROOT)/hal/rtl8821c/usb/rtl8821cu_halinit.c \
	$(RTL8821CU_ROOT)/hal/rtl8821c/usb/rtl8821cu_halmac.c \
	$(RTL8821CU_ROOT)/hal/rtl8821c/usb/rtl8821cu_io.c \
	$(RTL8821CU_ROOT)/hal/rtl8821c/usb/rtl8821cu_xmit.c \
	$(RTL8821CU_ROOT)/hal/rtl8821c/usb/rtl8821cu_recv.c \
	$(RTL8821CU_ROOT)/hal/rtl8821c/usb/rtl8821cu_led.c \
	$(RTL8821CU_ROOT)/hal/rtl8821c/usb/rtl8821cu_ops.c

RTL8821CU_PHYDM_SRCS := \
	$(RTL8821CU_ROOT)/hal/phydm/phydm_debug.c \
	$(RTL8821CU_ROOT)/hal/phydm/phydm_antdiv.c \
	$(RTL8821CU_ROOT)/hal/phydm/phydm_antdect.c \
	$(RTL8821CU_ROOT)/hal/phydm/phydm_interface.c \
	$(RTL8821CU_ROOT)/hal/phydm/phydm_hwconfig.c \
	$(RTL8821CU_ROOT)/hal/phydm/phydm.c \
	$(RTL8821CU_ROOT)/hal/phydm/halphyrf_ce.c \
	$(RTL8821CU_ROOT)/hal/phydm/phydm_dig.c \
	$(RTL8821CU_ROOT)/hal/phydm/phydm_pathdiv.c \
	$(RTL8821CU_ROOT)/hal/phydm/phydm_rainfo.c \
	$(RTL8821CU_ROOT)/hal/phydm/phydm_dynamicbbpowersaving.c \
	$(RTL8821CU_ROOT)/hal/phydm/phydm_powertracking_ce.c \
	$(RTL8821CU_ROOT)/hal/phydm/phydm_dynamictxpower.c \
	$(RTL8821CU_ROOT)/hal/phydm/phydm_adaptivity.c \
	$(RTL8821CU_ROOT)/hal/phydm/phydm_cfotracking.c \
	$(RTL8821CU_ROOT)/hal/phydm/phydm_noisemonitor.c \
	$(RTL8821CU_ROOT)/hal/phydm/phydm_acs.c \
	$(RTL8821CU_ROOT)/hal/phydm/phydm_beamforming.c \
	$(RTL8821CU_ROOT)/hal/phydm/phydm_dfs.c \
	$(RTL8821CU_ROOT)/hal/phydm/txbf/halcomtxbf.c \
	$(RTL8821CU_ROOT)/hal/phydm/txbf/haltxbfinterface.c \
	$(RTL8821CU_ROOT)/hal/phydm/txbf/phydm_hal_txbf_api.c \
	$(RTL8821CU_ROOT)/hal/phydm/phydm_adc_sampling.c \
	$(RTL8821CU_ROOT)/hal/phydm/phydm_kfree.c \
	$(RTL8821CU_ROOT)/hal/phydm/phydm_ccx.c \
	$(RTL8821CU_ROOT)/hal/phydm/phydm_psd.c \
	$(RTL8821CU_ROOT)/hal/phydm/rtl8821c/halhwimg8821c_bb.c \
	$(RTL8821CU_ROOT)/hal/phydm/rtl8821c/halhwimg8821c_mac.c \
	$(RTL8821CU_ROOT)/hal/phydm/rtl8821c/halhwimg8821c_rf.c \
	$(RTL8821CU_ROOT)/hal/phydm/rtl8821c/phydm_hal_api8821c.c \
	$(RTL8821CU_ROOT)/hal/phydm/rtl8821c/phydm_regconfig8821c.c \
	$(RTL8821CU_ROOT)/hal/phydm/rtl8821c/halphyrf_8821c.c \
	$(RTL8821CU_ROOT)/hal/phydm/rtl8821c/phydm_iqk_8821c.c \
	$(RTL8821CU_ROOT)/hal/btc/halbtc8821cwifionly.c \
	$(RTL8821CU_ROOT)/hal/btc/halbtc8723bwifionly.c \
	$(RTL8821CU_ROOT)/hal/btc/halbtc8822bwifionly.c \
	$(RTL8821CU_ROOT)/hal/btc/halbtc8821c2ant.c \
	$(RTL8821CU_ROOT)/hal/btc/halbtc8821c1ant.c \
	$(RTL8821CU_ROOT)/hal/efuse/rtl8821c/HalEfuseMask8821C_USB.c

RTL8821CU_OSDEP_SRCS := \
	$(RTL8821CU_ROOT)/os_dep/osdep_service.c \
	$(RTL8821CU_ROOT)/os_dep/linux/os_intfs.c \
	$(RTL8821CU_ROOT)/os_dep/linux/usb_intf.c \
	$(RTL8821CU_ROOT)/os_dep/linux/usb_ops_linux.c \
	$(RTL8821CU_ROOT)/os_dep/linux/xmit_linux.c \
	$(RTL8821CU_ROOT)/os_dep/linux/recv_linux.c \
	$(RTL8821CU_ROOT)/os_dep/linux/mlme_linux.c \
	$(RTL8821CU_ROOT)/platform/platform_ops.c

# NOT compiled (spec §3.2.3 reverse check): os_dep/linux/{ioctl_linux,
# ioctl_mp,ioctl_cfg80211,rtw_proc,rtw_cfgvendor,rtw_android}.c,
# os_dep/linux/wifi_regd.c - the W1 judgement nm-checks their absence.
RTL8821CU_VENDOR_SRCS := $(RTL8821CU_CORE_SRCS) $(RTL8821CU_HAL_SRCS) \
	$(RTL8821CU_PHYDM_SRCS) $(RTL8821CU_OSDEP_SRCS)

# W4 (S5a A-case): the wpa_supplicant vendor backend rides the adapter
# tier when the component line is in the image (trunk merge brought
# os/wpa_supplicant). The guard define is what keeps the seam/drv_entry
# hooks compiling cleanly without it.
RTL8821CU_WPA_BACKEND_SRCS :=
RTL8821CU_WPA_ADAPTER_CFLAGS :=
ifeq ($(CONFIG_WPA_SUPPLICANT),y)
RTL8821CU_WPA_BACKEND_SRCS := \
	port/adapters/rtl8821cu/wpa_vendor_rtl8821cu.c
RTL8821CU_WPA_ADAPTER_CFLAGS := -DCONFIG_NIC_WPA_BACKEND=1 -Ios/wpa_supplicant
endif

# W5 (S5a data plane): the lwip netif bridge over the vendor driver. The
# key requires the lwip stack in the image; the bridge compiles against the
# lwip includes directly (the LWIP block owns them, this block only needs
# the headers).
CONFIG_WLAN_NETIF ?= n
ifeq ($(CONFIG_WLAN_NETIF),y)
ifeq ($(filter y,$(CONFIG_LWIP)),)
$(error CONFIG_WLAN_NETIF=y requires CONFIG_LWIP=y)
endif
RTL8821CU_WPA_BACKEND_SRCS += port/adapters/rtl8821cu/wlan8821_netif.c
RTL8821CU_WPA_ADAPTER_CFLAGS += -DCONFIG_WLAN_NETIF=1 \
	-Ios/lwip/include -Ithird-party/lwip/src/include
endif

RTL8821CU_ADAPTER_SRCS := \
	port/adapters/rtl8821cu/drv_entry.c \
	port/adapters/rtl8821cu/wlan8821_cmds.c \
	port/adapters/rtl8821cu/wlan8821_seam.c \
	port/adapters/rtl8821cu/usbh_modeswitch.c \
	port/adapters/rtl8821cu/usbh_wlan_class.c \
	$(RTL8821CU_WPA_BACKEND_SRCS)

# The adapter objects go FIRST on the link line: .usbh_class_info is
# concatenated in link order and cherryusb takes the first match - the
# modeswitch hook (VID/PID 0bda:1a2b) must precede lkpi_msc's class-only
# 08/06/50 match, or the CD-ROM persona dies in lkpi_msc's negative
# connect before the eject assistant ever runs (W2 board round lesson).
SRCS := $(RTL8821CU_ADAPTER_SRCS) $(SRCS)
SRCS += $(RTL8821CU_VENDOR_SRCS)
CFLAGS += -DCONFIG_NIC_RTL8821CU=1

# Vendor build config: the W0-L board-proven macro set minus kbuild-internal
# defines (patches/rtl8821cu/README.md records the full accounting). The
# explicit NOT-defined set does the trimming: WOWLAN (kills the netlink
# route_dump face - W0L-S3 proven dead code), RTW_NAPI/GRO, BR_EXT,
# IOCTL_CFG80211, WIRELESS_EXT, USB_SUSPEND, MP_VHT_HW_TX_MODE.
RTL8821CU_VENDOR_CFG := -DDM_ODM_SUPPORT_TYPE=0x04 \
	-DCONFIG_RTL8821C -DCONFIG_MP_INCLUDED -DCONFIG_POWER_SAVING \
	-DCONFIG_BT_COEXIST -DCONFIG_EFUSE_CONFIG_FILE -DCONFIG_TRAFFIC_PROTECT \
	-DCONFIG_LOAD_PHY_PARA_FROM_FILE -DCONFIG_TXPWR_BY_RATE_EN=1 \
	-DCONFIG_TXPWR_LIMIT_EN=0 -DCONFIG_RTW_ADAPTIVITY_EN=0 \
	-DCONFIG_RTW_ADAPTIVITY_MODE=0 -DCONFIG_RTW_DEBUG \
	-DCONFIG_LITTLE_ENDIAN -DRTW_LOG_LEVEL=$(CONFIG_RTW_LOG_LEVEL) \
	-DREALTEK_CONFIG_PATH=\"\" -DEFUSE_MAP_PATH=\"\" -DWIFIMAC_PATH=\"\"
# [W7 print-clean 2026-10-03] configs/ owns the knob (Makefile is a pure
# consumer - no default here): 3 = _DRV_WARNING_ (ERR/WARN survive, the
# RTW_INFO ramask/EEPROM/ack_tx_done chatter is gone; runtime escape
# hatch `wlan8821 dbg <1-5>`, vendor rtw_debug.h enum)
ifeq ($(CONFIG_RTW_LOG_LEVEL),)
$(error CONFIG_RTW_LOG_LEVEL must be set in the config (configs/ owns it))
endif
# Include order is load-bearing: the linuxkpi headers shadow <linux/*.h>
# before the vendor tree comes in; -w is the frozen-import face ([FWC]
# Makefile:1192 precedent).
RTL8821CU_CFLAGS := $(LKPI_CFLAGS) $(USB_CFLAGS) \
	-I$(RTL8821CU_ROOT)/include -I$(RTL8821CU_ROOT)/hal/btc \
	-I$(RTL8821CU_ROOT)/platform -Iport/adapters/rtl8821cu \
	-I$(RTL8821CU_ROOT)/hal/phydm -I$(RTL8821CU_ROOT)/hal/phydm/txbf \
	-I$(RTL8821CU_ROOT)/hal/phydm/rtl8821c \
	$(RTL8821CU_VENDOR_CFG) -fgnu89-inline -w
$(BUILD)/$(RTL8821CU_ROOT)/%.o: EXTRAFLAGS := $(RTL8821CU_CFLAGS)
$(BUILD)/port/adapters/rtl8821cu/%.o: \
	EXTRAFLAGS := $(LKPI_CFLAGS) $(USB_CFLAGS) $(CSH_CFLAGS) \
		-I$(RTL8821CU_ROOT)/include -I$(RTL8821CU_ROOT)/hal/btc \
		-I$(RTL8821CU_ROOT)/platform \
		$(RTL8821CU_VENDOR_CFG) -fgnu89-inline -DCONFIG_NIC_RTL8821CU=1 \
		$(RTL8821CU_WPA_ADAPTER_CFLAGS)
# NOTE: this pattern rule OVERRIDES the shell block's $(BUILD)/app/%.o rule
# (last pattern match wins in GNU make) - CSH_CFLAGS (which carries
# -DCONFIG_SHELL=1) MUST ride along or app/wlan8821/main.c's shellboot
# #ifdef compiles out and the shell silently never starts (W2 board round).
# Recursive assignment on purpose: LWIP_WLAN_APP_CFLAGS is defined in the
# lwip block below and a target-specific := would snapshot it empty here.
$(BUILD)/app/wlan8821/%.o: \
	EXTRAFLAGS = $(LKPI_CFLAGS) $(USB_CFLAGS) $(CSH_CFLAGS) \
		-Iport/adapters/rtl8821cu -DCONFIG_NIC_RTL8821CU=1 \
		$(LWIP_WLAN_APP_CFLAGS)
endif

# --- the rtl8851bu vendor driver (S5b W1, feat-only - never merges) ----------
# The Realtek out-of-tree RTL8851BU driver (submodule third-party/RTL8851bu,
# PHL architecture, pin 5b6408d) consumed through the linuxkpi vendor tier.
# Compile set = the W0-B board-proven 374-unit USB face (Linux 7.3-rc2,
# runs/20261004-rtl8851bu-w0b-linux/, POWER_SAVE=n / BR_EXT=n) MINUS the
# RTOS cuts - the ioctl/proc/cfg80211/regd/android half (ioctl_linux
# ioctl_cfg80211 rtw_cfgvendor wifi_regd rtw_proc rtw_android ioctl_mp
# ioctl_efuse) plus the naturally-empty units (nlrtw rtw_rhashtable rtw_wds:
# their config gates live under CONFIG_RTW_MULTI_AP which defaults off, so
# they compile to zero symbols - W0-B nm evidence; patches/RTL8851bu/README.md
# holds the full accounting).  V5 diffs vs the rtl8821cu sibling: NO
# core/rtw_mem.c (no prealloc pool) and os_dep/osdep_service_linux.c joins
# the set (the PHL tree split the LINUX primitives out of osdep_service.c).
CONFIG_NIC_RTL8851BU ?= n
ifeq ($(filter $(CONFIG_NIC_RTL8851BU),y n),)
$(error CONFIG_NIC_RTL8851BU must be y or n, got '$(CONFIG_NIC_RTL8851BU)')
endif
ifeq ($(CONFIG_NIC_RTL8851BU),y)
ifeq ($(filter y,$(CONFIG_LINUXKPI)),)
$(error CONFIG_NIC_RTL8851BU=y requires CONFIG_LINUXKPI=y)
endif
RTL8851BU_ROOT := third-party/RTL8851bu

RTL8851BU_VENDOR_SRCS := \
	third-party/RTL8851bu/core/crypto/aes-ccm.c \
	third-party/RTL8851bu/core/crypto/aes-ctr.c \
	third-party/RTL8851bu/core/crypto/aes-gcm.c \
	third-party/RTL8851bu/core/crypto/aes-internal.c \
	third-party/RTL8851bu/core/crypto/aes-internal-enc.c \
	third-party/RTL8851bu/core/crypto/aes-omac1.c \
	third-party/RTL8851bu/core/crypto/aes-siv.c \
	third-party/RTL8851bu/core/crypto/ccmp.c \
	third-party/RTL8851bu/core/crypto/gcmp.c \
	third-party/RTL8851bu/core/crypto/rtw_crypto_wrap.c \
	third-party/RTL8851bu/core/crypto/sha256.c \
	third-party/RTL8851bu/core/crypto/sha256-internal.c \
	third-party/RTL8851bu/core/crypto/sha256-prf.c \
	third-party/RTL8851bu/core/mesh/rtw_mesh.c \
	third-party/RTL8851bu/core/mesh/rtw_mesh_hwmp.c \
	third-party/RTL8851bu/core/mesh/rtw_mesh_pathtbl.c \
	third-party/RTL8851bu/core/monitor/rtw_radiotap.c \
	third-party/RTL8851bu/core/regdb/rtw_regdb_rtk_64-40.c \
	third-party/RTL8851bu/core/rtw_ap.c \
	third-party/RTL8851bu/core/rtw_beamforming.c \
	third-party/RTL8851bu/core/rtw_btc.c \
	third-party/RTL8851bu/core/rtw_chplan.c \
	third-party/RTL8851bu/core/rtw_chset.c \
	third-party/RTL8851bu/core/rtw_cmd.c \
	third-party/RTL8851bu/core/rtw_csa.c \
	third-party/RTL8851bu/core/rtw_csi.c \
	third-party/RTL8851bu/core/rtw_debug.c \
	third-party/RTL8851bu/core/rtw_dfs.c \
	third-party/RTL8851bu/core/rtw_eht.c \
	third-party/RTL8851bu/core/rtw_ft.c \
	third-party/RTL8851bu/core/rtw_he.c \
	third-party/RTL8851bu/core/rtw_ieee80211.c \
	third-party/RTL8851bu/core/rtw_io.c \
	third-party/RTL8851bu/core/rtw_ioctl_query.c \
	third-party/RTL8851bu/core/rtw_ioctl_set.c \
	third-party/RTL8851bu/core/rtw_mbo.c \
	third-party/RTL8851bu/core/rtw_mi.c \
	third-party/RTL8851bu/core/rtw_mlme.c \
	third-party/RTL8851bu/core/rtw_mlme_ext.c \
	third-party/RTL8851bu/core/rtw_mp.c \
	third-party/RTL8851bu/core/rtw_p2p.c \
	third-party/RTL8851bu/core/rtw_phl.c \
	third-party/RTL8851bu/core/rtw_phl_cmd.c \
	third-party/RTL8851bu/core/rtw_pwrctrl.c \
	third-party/RTL8851bu/core/rtw_recv.c \
	third-party/RTL8851bu/core/rtw_recv_shortcut.c \
	third-party/RTL8851bu/core/rtw_rf.c \
	third-party/RTL8851bu/core/rtw_rm.c \
	third-party/RTL8851bu/core/rtw_rm_fsm.c \
	third-party/RTL8851bu/core/rtw_rm_util.c \
	third-party/RTL8851bu/core/rtw_scan.c \
	third-party/RTL8851bu/core/rtw_sec_cam.c \
	third-party/RTL8851bu/core/rtw_security.c \
	third-party/RTL8851bu/core/rtw_sreset.c \
	third-party/RTL8851bu/core/rtw_sta_mgt.c \
	third-party/RTL8851bu/core/rtw_swcrypto.c \
	third-party/RTL8851bu/core/rtw_tdls.c \
	third-party/RTL8851bu/core/rtw_trx.c \
	third-party/RTL8851bu/core/rtw_trx_usb.c \
	third-party/RTL8851bu/core/rtw_twt.c \
	third-party/RTL8851bu/core/rtw_txpwr.c \
	third-party/RTL8851bu/core/rtw_vht.c \
	third-party/RTL8851bu/core/rtw_wlan_util.c \
	third-party/RTL8851bu/core/rtw_wnm.c \
	third-party/RTL8851bu/core/rtw_xmit.c \
	third-party/RTL8851bu/core/rtw_xmit_shortcut.c \
	third-party/RTL8851bu/os_dep/linux/mlme_linux.c \
	third-party/RTL8851bu/os_dep/linux/os_ch_utils.c \
	third-party/RTL8851bu/os_dep/linux/os_intfs.c \
	third-party/RTL8851bu/os_dep/linux/recv_linux.c \
	third-party/RTL8851bu/os_dep/linux/rtw_cfg.c \
	third-party/RTL8851bu/os_dep/linux/usb_intf.c \
	third-party/RTL8851bu/os_dep/linux/usb_ops_linux.c \
	third-party/RTL8851bu/os_dep/linux/xmit_linux.c \
	third-party/RTL8851bu/os_dep/osdep_service.c \
	third-party/RTL8851bu/os_dep/osdep_service_linux.c \
	third-party/RTL8851bu/phl/hal_g6/btc/btc_8852b/btc_8852b.c \
	third-party/RTL8851bu/phl/hal_g6/btc/hal_btc.c \
	third-party/RTL8851bu/phl/hal_g6/btc/halbtc_action.c \
	third-party/RTL8851bu/phl/hal_g6/btc/halbtc_dbg_cmd.c \
	third-party/RTL8851bu/phl/hal_g6/btc/halbtc_def.c \
	third-party/RTL8851bu/phl/hal_g6/btc/halbtc_fw.c \
	third-party/RTL8851bu/phl/hal_g6/efuse/hal_efuse.c \
	third-party/RTL8851bu/phl/hal_g6/hal_acs.c \
	third-party/RTL8851bu/phl/hal_g6/hal_api.c \
	third-party/RTL8851bu/phl/hal_g6/hal_api_bb.c \
	third-party/RTL8851bu/phl/hal_g6/hal_api_btc.c \
	third-party/RTL8851bu/phl/hal_g6/hal_api_efuse.c \
	third-party/RTL8851bu/phl/hal_g6/hal_api_mac.c \
	third-party/RTL8851bu/phl/hal_g6/hal_api_rf.c \
	third-party/RTL8851bu/phl/hal_g6/hal_beamform.c \
	third-party/RTL8851bu/phl/hal_g6/hal_c2h.c \
	third-party/RTL8851bu/phl/hal_g6/hal_cam.c \
	third-party/RTL8851bu/phl/hal_g6/hal_cap.c \
	third-party/RTL8851bu/phl/hal_g6/hal_chan.c \
	third-party/RTL8851bu/phl/hal_g6/hal_chan_info.c \
	third-party/RTL8851bu/phl/hal_g6/hal_com_i.c \
	third-party/RTL8851bu/phl/hal_g6/hal_csi_buffer.c \
	third-party/RTL8851bu/phl/hal_g6/hal_dbcc.c \
	third-party/RTL8851bu/phl/hal_g6/hal_dfs.c \
	third-party/RTL8851bu/phl/hal_g6/hal_ext_tx_pwr_lmt.c \
	third-party/RTL8851bu/phl/hal_g6/hal_fw.c \
	third-party/RTL8851bu/phl/hal_g6/hal_init.c \
	third-party/RTL8851bu/phl/hal_g6/hal_io.c \
	third-party/RTL8851bu/phl/hal_g6/hal_ld_file.c \
	third-party/RTL8851bu/phl/hal_g6/hal_led.c \
	third-party/RTL8851bu/phl/hal_g6/hal_mcc.c \
	third-party/RTL8851bu/phl/hal_g6/hal_notify.c \
	third-party/RTL8851bu/phl/hal_g6/hal_p2pps.c \
	third-party/RTL8851bu/phl/hal_g6/hal_ps.c \
	third-party/RTL8851bu/phl/hal_g6/hal_regulation.c \
	third-party/RTL8851bu/phl/hal_g6/hal_rx.c \
	third-party/RTL8851bu/phl/hal_g6/hal_scanofld.c \
	third-party/RTL8851bu/phl/hal_g6/hal_ser.c \
	third-party/RTL8851bu/phl/hal_g6/hal_sound.c \
	third-party/RTL8851bu/phl/hal_g6/hal_sta.c \
	third-party/RTL8851bu/phl/hal_g6/hal_str_proc.c \
	third-party/RTL8851bu/phl/hal_g6/hal_thermal.c \
	third-party/RTL8851bu/phl/hal_g6/hal_trx_mit.c \
	third-party/RTL8851bu/phl/hal_g6/hal_twt.c \
	third-party/RTL8851bu/phl/hal_g6/hal_tx.c \
	third-party/RTL8851bu/phl/hal_g6/hal_txpwr.c \
	third-party/RTL8851bu/phl/hal_g6/hal_usb.c \
	third-party/RTL8851bu/phl/hal_g6/hal_wow.c \
	third-party/RTL8851bu/phl/hal_g6/mac/fw_ax/rtl8851b/hal8851b_fw.c \
	third-party/RTL8851bu/phl/hal_g6/mac/fw_ax/rtl8851b/hal8851b_fw_log.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/_usb.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/addr_cam.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/beacon.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/cmac_tx.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/coex.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/common.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/cpuio.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/dbcc.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/dbg_cmd.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/dbgpkg.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/dbgport_hw.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/dle.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/eco_patch_check.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/efuse.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/flash.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/fwcmd.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/fwdl.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/fwofld.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/gpio.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/h2c_agg.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/hci_fc.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/hdr_conv.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/hw.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/hw_seq.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/hwamsdu.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/init.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/la_mode.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/mac_8851b/_usb_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/mac_8851b/cmac_tx_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/mac_8851b/coex_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/mac_8851b/dbgpkg_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/mac_8851b/dle_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/mac_8851b/efuse_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/mac_8851b/fwcmd_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/mac_8851b/gpio_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/mac_8851b/hci_fc_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/mac_8851b/hdr_conv_rx_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/mac_8851b/hwamsdu_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/mac_8851b/init_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/mac_8851b/mac_priv_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/mac_8851b/phy_rpt_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/mac_8851b/pwr_seq_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/mac_8851b/pwr_seq_func_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/mac_8851b/rrsr_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/mac_8851b/tblupd_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/mac_8851b/trx_desc_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/mac_8851b/wowlan_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/mac_priv.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/mcc.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/mport.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/nan.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/otpkeysinfo.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/p2p.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/phy_misc.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/phy_rpt.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/power_saving.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/pwr.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/pwr_seq_func.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/role.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/rx_filter.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/rx_forwarding.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/secure_boot.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/security_cam.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/ser.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/sounding.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/spatial_reuse.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/sta_sch.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/status.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/tblupd.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/tcpip_checksum_offload.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/trx_desc.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/trxcfg.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/twt.c \
	third-party/RTL8851bu/phl/hal_g6/mac/mac_ax/wowlan.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_8851b/halbb_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_8851b/halbb_8851b_api.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_8851b/halbb_8851b_fwofld_api.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_8851b/halbb_hwimg_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_8851b/halbb_reg_cfg_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_ant_div.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_api.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_api_be.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_api_be_hw.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_api_be_intg.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_api_be_sw.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_auto_dbg.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_bb_wrapper.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_cfo_trk.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_ch_info.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_cmn_rpt.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_dbcc.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_dbg.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_dbg_cmd.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_dbg_cnsl_out.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_dfs.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_dig.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_dyn_1r_cca.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_dyn_csi_rsp.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_dyn_dtr.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_edcca.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_env_mntr.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_fwofld.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_hw_cfg.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_init.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_interface.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_la_mode.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_math_lib.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_mp.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_path_div.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_physts.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_physts_7.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_plcp_gen.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_plcp_tx.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_plcp_tx_7.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_pmac_setting.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_psd.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_pwr_ctrl.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_ra.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_rua_tbl.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_snif.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_spatial_reuse.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_spur_suppress.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_statistics.c \
	third-party/RTL8851bu/phl/hal_g6/phy/bb/halbb_ul_tb_ctrl.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_8851b/halrf_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_8851b/halrf_8851b_api.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_8851b/halrf_dack_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_8851b/halrf_dpk_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_8851b/halrf_efuse_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_8851b/halrf_hwimg_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_8851b/halrf_iqk_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_8851b/halrf_kfree_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_8851b/halrf_ops_rtl8851b.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_8851b/halrf_psd_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_8851b/halrf_reg_cfg_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_8851b/halrf_set_pwr_table_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_8851b/halrf_tssi_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_8851b/halrf_txgapk_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_api.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_dbg.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_dbg_cmd.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_ex.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_hw_cfg.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_init.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_interface.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_iqk.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_pmac.c \
	third-party/RTL8851bu/phl/hal_g6/phy/rf/halrf_pwr_table.c \
	third-party/RTL8851bu/phl/hal_g6/rtl8851b/hal_trx_8851b.c \
	third-party/RTL8851bu/phl/hal_g6/rtl8851b/rtl8851b_cmd.c \
	third-party/RTL8851bu/phl/hal_g6/rtl8851b/rtl8851b_halinit.c \
	third-party/RTL8851bu/phl/hal_g6/rtl8851b/rtl8851b_mac.c \
	third-party/RTL8851bu/phl/hal_g6/rtl8851b/rtl8851b_ops.c \
	third-party/RTL8851bu/phl/hal_g6/rtl8851b/rtl8851b_phy.c \
	third-party/RTL8851bu/phl/hal_g6/rtl8851b/usb/hal_trx_8851bu.c \
	third-party/RTL8851bu/phl/hal_g6/rtl8851b/usb/rtl8851bu_halinit.c \
	third-party/RTL8851bu/phl/hal_g6/rtl8851b/usb/rtl8851bu_halmac.c \
	third-party/RTL8851bu/phl/hal_g6/rtl8851b/usb/rtl8851bu_io.c \
	third-party/RTL8851bu/phl/hal_g6/rtl8851b/usb/rtl8851bu_led.c \
	third-party/RTL8851bu/phl/hal_g6/rtl8851b/usb/rtl8851bu_ops.c \
	third-party/RTL8851bu/phl/hal_g6/test/hal_test_module.c \
	third-party/RTL8851bu/phl/hal_g6/test/mp/hal_test_mp.c \
	third-party/RTL8851bu/phl/hal_g6/test/mp/hal_test_mp_cal.c \
	third-party/RTL8851bu/phl/hal_g6/test/mp/hal_test_mp_config.c \
	third-party/RTL8851bu/phl/hal_g6/test/mp/hal_test_mp_efuse.c \
	third-party/RTL8851bu/phl/hal_g6/test/mp/hal_test_mp_reg.c \
	third-party/RTL8851bu/phl/hal_g6/test/mp/hal_test_mp_rx.c \
	third-party/RTL8851bu/phl/hal_g6/test/mp/hal_test_mp_tx.c \
	third-party/RTL8851bu/phl/hal_g6/test/mp/hal_test_mp_txpwr.c \
	third-party/RTL8851bu/phl/hci/phl_trx_usb.c \
	third-party/RTL8851bu/phl/phl_acs.c \
	third-party/RTL8851bu/phl/phl_api_drv.c \
	third-party/RTL8851bu/phl/phl_chan.c \
	third-party/RTL8851bu/phl/phl_chan_info.c \
	third-party/RTL8851bu/phl/phl_cmd_btc.c \
	third-party/RTL8851bu/phl/phl_cmd_dispatch_engine.c \
	third-party/RTL8851bu/phl/phl_cmd_dispatcher.c \
	third-party/RTL8851bu/phl/phl_cmd_dispr_controller.c \
	third-party/RTL8851bu/phl/phl_cmd_general.c \
	third-party/RTL8851bu/phl/phl_cmd_scan.c \
	third-party/RTL8851bu/phl/phl_cmd_ser.c \
	third-party/RTL8851bu/phl/phl_connect.c \
	third-party/RTL8851bu/phl/phl_debug.c \
	third-party/RTL8851bu/phl/phl_dfs.c \
	third-party/RTL8851bu/phl/phl_diagnose.c \
	third-party/RTL8851bu/phl/phl_dm.c \
	third-party/RTL8851bu/phl/phl_ecsa.c \
	third-party/RTL8851bu/phl/phl_ext_tx_pwr_lmt.c \
	third-party/RTL8851bu/phl/phl_ie.c \
	third-party/RTL8851bu/phl/phl_init.c \
	third-party/RTL8851bu/phl/phl_led.c \
	third-party/RTL8851bu/phl/phl_mcc.c \
	third-party/RTL8851bu/phl/phl_mr.c \
	third-party/RTL8851bu/phl/phl_mr_coex.c \
	third-party/RTL8851bu/phl/phl_msg_hub.c \
	third-party/RTL8851bu/phl/phl_notify.c \
	third-party/RTL8851bu/phl/phl_p2pps.c \
	third-party/RTL8851bu/phl/phl_pkt_ofld.c \
	third-party/RTL8851bu/phl/phl_regulation.c \
	third-party/RTL8851bu/phl/phl_role.c \
	third-party/RTL8851bu/phl/phl_rx.c \
	third-party/RTL8851bu/phl/phl_rx_agg.c \
	third-party/RTL8851bu/phl/phl_scanofld.c \
	third-party/RTL8851bu/phl/phl_sec.c \
	third-party/RTL8851bu/phl/phl_sound.c \
	third-party/RTL8851bu/phl/phl_sound_cmd.c \
	third-party/RTL8851bu/phl/phl_sta.c \
	third-party/RTL8851bu/phl/phl_sw_cap.c \
	third-party/RTL8851bu/phl/phl_tdls.c \
	third-party/RTL8851bu/phl/phl_thermal.c \
	third-party/RTL8851bu/phl/phl_trx_mit.c \
	third-party/RTL8851bu/phl/phl_twt.c \
	third-party/RTL8851bu/phl/phl_tx.c \
	third-party/RTL8851bu/phl/phl_txpwr.c \
	third-party/RTL8851bu/phl/phl_util.c \
	third-party/RTL8851bu/phl/phl_watchdog.c \
	third-party/RTL8851bu/phl/phl_wow.c \
	third-party/RTL8851bu/phl/test/cmd_disp_test.c \
	third-party/RTL8851bu/phl/test/mp/phl_test_mp.c \
	third-party/RTL8851bu/phl/test/mp/phl_test_mp_cal.c \
	third-party/RTL8851bu/phl/test/mp/phl_test_mp_config.c \
	third-party/RTL8851bu/phl/test/mp/phl_test_mp_efuse.c \
	third-party/RTL8851bu/phl/test/mp/phl_test_mp_reg.c \
	third-party/RTL8851bu/phl/test/mp/phl_test_mp_rx.c \
	third-party/RTL8851bu/phl/test/mp/phl_test_mp_tx.c \
	third-party/RTL8851bu/phl/test/mp/phl_test_mp_txpwr.c \
	third-party/RTL8851bu/phl/test/mp/phl_test_mp_watchdog.c \
	third-party/RTL8851bu/phl/test/phl_dbg_cmd.c \
	third-party/RTL8851bu/phl/test/phl_ser_dbg_cmd.c \
	third-party/RTL8851bu/phl/test/test_module.c \
	third-party/RTL8851bu/phl/test/trx_test.c \
	third-party/RTL8851bu/phl/test/verify/dbcc/phl_test_dbcc.c \
	third-party/RTL8851bu/phl/test/verify/phl_test_verify.c \
	third-party/RTL8851bu/platform/platform_ops.c

# W1 adapter tier: no modeswitch (0bda:b851 enumerates directly, W0-B).
# [S5b W4] the lwip netif bridge joins the adapter tier under
# CONFIG_WLAN_NETIF (the sibling line's key): the wpa backend is still a
# later round, so the netif owns the only RX sink consumer.
RTL8851BU_NETIF_SRCS :=
ifeq ($(CONFIG_WLAN_NETIF),y)
ifeq ($(filter y,$(CONFIG_LWIP)),)
$(error CONFIG_WLAN_NETIF=y requires CONFIG_LWIP=y)
endif
RTL8851BU_NETIF_SRCS := port/adapters/rtl8851bu/wlan8851_netif.c
RTL8851BU_ADAPTER_EXTRA_CFLAGS := -DCONFIG_WLAN_NETIF=1 \
	-Ios/lwip/include -Ithird-party/lwip/src/include
else
RTL8851BU_ADAPTER_EXTRA_CFLAGS :=
endif

# [S5b-W4] the wpa_supplicant vendor backend (the S5a 8821 block
# mirrored): compiles when the component line is in the image.  The guard
# define flips the seam/drv_entry hooks and - with the netif bridge
# present - makes the wpa sink the single RX owner (EAPOL to the
# supplicant pump, the rest into wlan8851_wlan_netif_input).
RTL8851BU_WPA_BACKEND_SRCS :=
ifeq ($(CONFIG_WPA_SUPPLICANT),y)
RTL8851BU_WPA_BACKEND_SRCS := port/adapters/rtl8851bu/wpa_vendor_rtl8851bu.c
RTL8851BU_ADAPTER_EXTRA_CFLAGS += -DCONFIG_NIC_WPA_BACKEND=1 \
	-Ios/wpa_supplicant
endif

RTL8851BU_ADAPTER_SRCS := \
	port/adapters/rtl8851bu/drv_entry.c \
	port/adapters/rtl8851bu/wlan8851_cmds.c \
	port/adapters/rtl8851bu/wlan8851_seam.c \
	port/adapters/rtl8851bu/usbh_wlan_class.c \
	$(RTL8851BU_NETIF_SRCS) \
	$(RTL8851BU_WPA_BACKEND_SRCS)

SRCS := $(RTL8851BU_ADAPTER_SRCS) $(SRCS)
SRCS += $(RTL8851BU_VENDOR_SRCS)
CFLAGS += -DCONFIG_NIC_RTL8851BU=1

# [S5b-W3] stall forensics (os/threadx/mon.c): the thread/mutex/semaphore
# table dump every 10 s at osPriorityRealtime, printed through
# board_log_emergency so a wedged console cannot silence it.  Board-round
# diagnostic only - default off so the product image never carries it.
CONFIG_WLAN_MON ?= n
ifeq ($(filter $(CONFIG_WLAN_MON),y n),)
$(error CONFIG_WLAN_MON must be y or n, got '$(CONFIG_WLAN_MON)')
endif
ifeq ($(CONFIG_WLAN_MON),y)
SRCS += os/threadx/mon.c
CFLAGS += -DCONFIG_WLAN_MON=1
endif

# [S5b-W3] unassisted board-round driver (app/wlan8851/main.c): after a
# settle delay automatically runs up -> scan -> bss on a dedicated thread,
# so the acceptance chain reproduces without console typing and a stall
# cannot wedge the shell.  Board-round diagnostic only, default off.
CONFIG_WLAN_AUTORUN ?= n
ifeq ($(filter $(CONFIG_WLAN_AUTORUN),y n),)
$(error CONFIG_WLAN_AUTORUN must be y or n, got '$(CONFIG_WLAN_AUTORUN)')
endif
ifeq ($(CONFIG_WLAN_AUTORUN),y)
CFLAGS += -DCONFIG_WLAN_AUTORUN=1
endif

# Vendor build config: the W0-B board-proven macro set (kbuild .cmd of the
# linux-7.3 build, core/.rtw_cmd.o.cmd) minus kbuild internals and minus the
# cut macros - CONFIG_IOCTL_CFG80211/RTW_USE_CFG80211_STA_EVENT (the
# cfg80211 face is stubbed), CONFIG_PROC_DEBUG (no rtw_proc.c), CONFIG_RTW_
# NAPI/GRO/NETIF_SG (SG defaults y in the vendor Makefile, spec diff item 6),
# CONFIG_RADIO_WORK.  DM_ODM_SUPPORT_TYPE does not apply (PHL replaced
# PHYDM).  RTW_LOG_LEVEL is config-owned like the sibling; RTW_PHL_LOG_LEVEL
# rides at the W0-B value.
ifeq ($(CONFIG_RTW_LOG_LEVEL),)
$(error CONFIG_RTW_LOG_LEVEL must be set in the config (configs/ owns it))
endif
RTL8851BU_VENDOR_CFG := -DCONFIG_RTL8851B -DCONFIG_USB_HCI -DCONFIG_PHL_ARCH \
	-DCONFIG_WIFI_6 -DCONFIG_MP_INCLUDED -DCONFIG_BTC \
	-DCONFIG_EFUSE_CONFIG_FILE -DCONFIG_LITTLE_ENDIAN \
	-DCONFIG_LOAD_PHY_PARA_FROM_FILE \
	-DCONFIG_TXPWR_BY_RATE=1 -DCONFIG_TXPWR_BY_RATE_EN=1 \
	-DCONFIG_TXPWR_LIMIT=1 -DCONFIG_TXPWR_LIMIT_EN=0 \
	-DCONFIG_IEEE80211W -DCONFIG_80211D -DCONFIG_LAYER2_ROAMING \
	-DCONFIG_RTW_DEBUG \
	-DCONFIG_ROAMING_FLAG=0x3 -DCONFIG_RTW_80211K -DCONFIG_RTW_80211R \
	-DCONFIG_RTW_MBO -DCONFIG_RTW_WNM -DCONFIG_RTW_BTM_ROAM \
	-DCONFIG_CMD_DISP -DCONFIG_DBG_AX_CAM -DCONFIG_PHL_TEST_SUITE \
	-DPLATFORM_LINUX -DPHL_PLATFORM_LINUX -DUSE_TRUE_PHY \
	-DRTW_PHL_TX -DRTW_PHL_RX -DRTW_PHL_BCN \
	-DRTW_NETWORK_LINK_MAX=3 -DDTYPE=DEV_TYPE_LEGACY \
	-DHIGH_ACTIVE_HST2DEV=0 -DCONFIG_RTW_UP_MAPPING_RULE=0 \
	-DRTW_COUNTRY_IE_SLAVE_EN_MODE=0 -DRTW_COUNTRY_IE_SLAVE_FLAGS=0x01 \
	-DRTW_EDCCA_MODE_SEL=0 -DRTW_FT_DBG=0 -DRTW_MBO_DBG=0 -DRTW_WNM_DBG=0 \
	-DRTW_LOG_LEVEL=$(CONFIG_RTW_LOG_LEVEL) -DRTW_PHL_LOG_LEVEL=4 \
	-DREALTEK_CONFIG_PATH=\"\" -DEFUSE_MAP_PATH=\"\" -DWIFIMAC_PATH=\"\"
# Include order is load-bearing (sibling note): the linuxkpi headers shadow
# <linux/*.h> before the vendor tree comes in; -w is the frozen-import face.
RTL8851BU_CFLAGS := $(LKPI_CFLAGS) $(USB_CFLAGS) \
	-I$(RTL8851BU_ROOT)/include -I$(RTL8851BU_ROOT)/core/crypto \
	-I$(RTL8851BU_ROOT)/phl/hal_g6 -I$(RTL8851BU_ROOT)/platform \
	-Iport/adapters/rtl8851bu \
	$(RTL8851BU_VENDOR_CFG) -fgnu89-inline -w
$(BUILD)/$(RTL8851BU_ROOT)/%.o: EXTRAFLAGS := $(RTL8851BU_CFLAGS)
$(BUILD)/port/adapters/rtl8851bu/%.o: \
	EXTRAFLAGS := $(LKPI_CFLAGS) $(USB_CFLAGS) $(CSH_CFLAGS) \
		-I$(RTL8851BU_ROOT)/include -I$(RTL8851BU_ROOT)/core/crypto \
		-I$(RTL8851BU_ROOT)/phl/hal_g6 -I$(RTL8851BU_ROOT)/platform \
		$(RTL8851BU_VENDOR_CFG) -fgnu89-inline -DCONFIG_NIC_RTL8851BU=1 \
		$(RTL8851BU_ADAPTER_EXTRA_CFLAGS)
# app/wlan8851 rides the shell flags like app/wlan8821 does (CSH_CFLAGS
# carries -DCONFIG_SHELL=1 - without it the shellboot #ifdef compiles out).
# [S5b W4] LWIP_WLAN_APP_CFLAGS is recursive on purpose (defined in the
# lwip block below): the W4 data-plane image arms tcpip_init here - without
# -DCONFIG_LWIP=1 the branch compiles out and the first assoc callback
# asserts 'Invalid mbox' (board 2026-10-04).
$(BUILD)/app/wlan8851/%.o: \
	EXTRAFLAGS = $(LKPI_CFLAGS) $(USB_CFLAGS) $(CSH_CFLAGS) \
		-Iport/adapters/rtl8851bu -DCONFIG_NIC_RTL8851BU=1 \
		$(LWIP_WLAN_APP_CFLAGS)
endif

# lwip (SPEC S7/lwip-adapt §3.2/§4/P7-1a): the pinned upstream stack (2.2.1,
# zero-patch expectation) + the first-party os/lwip adapter. Phase 1 is the
# NO_SYS=1 polled model on the bare-metal base; the api/ files and dns.c
# join at P7-2a (lwipopts rows and the compile set move together). The
# compile set is LOCKED to this list - the P7-1a acceptance nm-checks the
# product for api/ absences. Include order is load-bearing: os/lwip/include
# FIRST so the first-party lwipopts.h / arch/cc.h shadow the tree (opt.h
# pulls lwipopts.h by bare name in every TU). Upstream sources compile -w
# (frozen import, zero-patch pin).
CONFIG_LWIP ?= n
ifeq ($(filter $(CONFIG_LWIP),y n),)
$(error CONFIG_LWIP must be y or n, got '$(CONFIG_LWIP)')
endif
ifeq ($(CONFIG_LWIP),y)
# GMAC port single choice (FR-9/KI-027): build-time, the other port never
# gets a clock or a register write. Values: 0 = gmac0, 1 = gmac1.
# W5: EMPTY means no GMAC at all - the wlan-only image (lwip-wlan.config,
# the KI-027 single-NIC form: wireless is the only netif in the product).
CONFIG_GMAC_PORT ?=
ifeq ($(CONFIG_GMAC_PORT),)
CONFIG_LWIP_GMAC := n
else
CONFIG_LWIP_GMAC := y
ifeq ($(filter $(CONFIG_GMAC_PORT),0 1),)
$(error CONFIG_GMAC_PORT must be 0, 1 or empty, got '$(CONFIG_GMAC_PORT)')
endif
endif

# GMAC RX data-path factors (N7 R3, one A/B key each - override on the make
# command line, clean-config between variants):
#   RX_HWCSUM   MAC IPC checksum offload: the driver drops RDES1-flagged bad
#               frames and lwipopts disables CHECKSUM_CHECK_IP/TCP/UDP in
#               lockstep (both sides must flip together).
#   RX_COALESCE RX descriptor IOC off + RX watchdog interrupt coalescing
#               (RI asserted on watchdog expiry instead of per frame).
#   RX_ZC       RX zero-copy: completed frames are lent up as custom pbufs
#               (no ISR copy, no staging ring); re-arm draws from a spare-
#               slot pool, exhaustion drops instead of parking.
#   RX_RIWT     watchdog reload, unit 256 DMA clock ticks (Linux default 0xa0).
CONFIG_GMAC_RX_HWCSUM ?= n
CONFIG_GMAC_RX_COALESCE ?= n
CONFIG_GMAC_RX_RIWT ?= 0xa0
CONFIG_GMAC_RX_ZC ?= n
# One-shot CPU cluster raise (N7 R5a): pairs a Linux rk3568 opp row (freq +
# vdd_cpu over i2c0->RK809) and relocks APLL before the scheduler starts.
# 0 = probe/log the inherited voltage only (no writes).
CONFIG_CPU_BOOST_MHZ ?= 0
ifeq ($(filter $(CONFIG_GMAC_RX_HWCSUM),y n),)
$(error CONFIG_GMAC_RX_HWCSUM must be y or n, got '$(CONFIG_GMAC_RX_HWCSUM)')
endif
ifeq ($(filter $(CONFIG_GMAC_RX_COALESCE),y n),)
$(error CONFIG_GMAC_RX_COALESCE must be y or n, got '$(CONFIG_GMAC_RX_COALESCE)')
endif
ifeq ($(filter $(CONFIG_GMAC_RX_ZC),y n),)
$(error CONFIG_GMAC_RX_ZC must be y or n, got '$(CONFIG_GMAC_RX_ZC)')
endif
ifeq ($(filter $(CONFIG_CPU_BOOST_MHZ),0 1104 1416 1608 1800),)
$(error CONFIG_CPU_BOOST_MHZ must be one of 0/1104/1416/1608/1800, got '$(CONFIG_CPU_BOOST_MHZ)')
endif
CFLAGS += -DCONFIG_GMAC_RX_HWCSUM=$(if $(filter y,$(CONFIG_GMAC_RX_HWCSUM)),1,0) \
	-DCONFIG_GMAC_RX_COALESCE=$(if $(filter y,$(CONFIG_GMAC_RX_COALESCE)),1,0) \
	-DCONFIG_GMAC_RX_RIWT=$(CONFIG_GMAC_RX_RIWT) \
	-DCONFIG_GMAC_RX_ZC=$(if $(filter y,$(CONFIG_GMAC_RX_ZC)),1,0) \
	-DCONFIG_CPU_BOOST_MHZ=$(CONFIG_CPU_BOOST_MHZ)

# N7 R5b: SCMI clock probe over SMC (BL31 carrier) + local CRU readback.
# Diagnostic shell surface only - nothing runs at boot; the productization
# decision (boot-time rate set vs U-Boot-side boost) is R5c's, gated on the
# board answers this probe produces.
CONFIG_SCMI_PROBE ?= n
ifeq ($(CONFIG_SCMI_PROBE),y)
ifneq ($(filter y,$(CONFIG_SHELL)),)
SRCS += board/$(BOARD)/drivers/scmi_clk.c
$(BUILD)/board/$(BOARD)/drivers/scmi_clk.o: EXTRAFLAGS := $(CSH_CFLAGS)
endif
endif

LWIP_DIR := third-party/lwip
# GMAC driver trio (P7-1b, spec §3.4): the board-verified EQOS MAC core +
# RTL8211F PHY + RK3568 glue/board table, re-arranged onto gicv3/cache/log.
# The unselected port (CONFIG_GMAC_PORT) is data-only - never initialized.
# W5: the whole trio + its ethnetif bridge exist only in a GMAC build; the
# wlan-only image (empty CONFIG_GMAC_PORT) carries none of them.
ifeq ($(CONFIG_LWIP_GMAC),y)
SRCS += \
	board/$(BOARD)/drivers/dwc_eqos.c \
	board/$(BOARD)/drivers/rtl8211f.c \
	board/$(BOARD)/drivers/eqos_rk3568.c
endif
LWIP_SRCS := \
	$(LWIP_DIR)/src/core/def.c \
	$(LWIP_DIR)/src/core/inet_chksum.c \
	$(LWIP_DIR)/src/core/init.c \
	$(LWIP_DIR)/src/core/ip.c \
	$(LWIP_DIR)/src/core/mem.c \
	$(LWIP_DIR)/src/core/memp.c \
	$(LWIP_DIR)/src/core/netif.c \
	$(LWIP_DIR)/src/core/pbuf.c \
	$(LWIP_DIR)/src/core/raw.c \
	$(LWIP_DIR)/src/core/stats.c \
	$(LWIP_DIR)/src/core/sys.c \
	$(LWIP_DIR)/src/core/tcp.c \
	$(LWIP_DIR)/src/core/tcp_in.c \
	$(LWIP_DIR)/src/core/tcp_out.c \
	$(LWIP_DIR)/src/core/timeouts.c \
	$(LWIP_DIR)/src/core/udp.c \
	$(LWIP_DIR)/src/core/ipv4/acd.c \
	$(LWIP_DIR)/src/core/ipv4/dhcp.c \
	$(LWIP_DIR)/src/core/ipv4/etharp.c \
	$(LWIP_DIR)/src/core/ipv4/icmp.c \
	$(LWIP_DIR)/src/core/ipv4/ip4.c \
	$(LWIP_DIR)/src/core/ipv4/ip4_addr.c \
	$(LWIP_DIR)/src/core/ipv4/ip4_frag.c \
	$(LWIP_DIR)/src/core/dns.c \
	$(LWIP_DIR)/src/api/err.c \
	$(LWIP_DIR)/src/api/api_lib.c \
	$(LWIP_DIR)/src/api/api_msg.c \
	$(LWIP_DIR)/src/api/netbuf.c \
	$(LWIP_DIR)/src/api/netdb.c \
	$(LWIP_DIR)/src/api/netifapi.c \
	$(LWIP_DIR)/src/api/sockets.c \
	$(LWIP_DIR)/src/api/tcpip.c \
	$(LWIP_DIR)/src/netif/ethernet.c \
	$(LWIP_DIR)/src/apps/lwiperf/lwiperf.c \
	os/lwip/cmsis/sys_arch.c \
	os/lwip/lwip_diag.c
ifeq ($(CONFIG_LWIP_GMAC),y)
LWIP_SRCS += os/lwip/ethnetif.c
endif
SRCS += $(LWIP_SRCS)

LWIP_CFLAGS := -Ios/lwip/include -I$(LWIP_DIR)/src/include
ifeq ($(CONFIG_LWIP_GMAC),y)
LWIP_CFLAGS += -DCONFIG_GMAC_PORT=$(CONFIG_GMAC_PORT)
endif
# the wlan app main arms tcpip_init when the stack is in the image (see
# $(BUILD)/app/wlan8821/%.o in the rtl8821cu block - recursive there)
LWIP_WLAN_APP_CFLAGS = -DCONFIG_LWIP=1 -Ios/lwip/include \
	-I$(LWIP_DIR)/src/include
$(BUILD)/third-party/lwip/%.o: EXTRAFLAGS := -w $(LWIP_CFLAGS)
$(BUILD)/os/lwip/%.o: EXTRAFLAGS := $(LWIP_CFLAGS)
# Recursive assignment on purpose: APP_AUTORUN_CFLAGS is defined later in
# the netutils block (CONFIG_SCMI_PROBE / CONFIG_IPERF2_AUTO_HOST), and a
# target-specific := would snapshot it empty here.
$(BUILD)/app/lwip_lan/%.o: EXTRAFLAGS = $(LWIP_CFLAGS) $(CSH_CFLAGS) \
	$(APP_AUTORUN_CFLAGS)
endif

# netutils (SPEC S7b/netutils-adapt §3.4/§3.6): the vendored tool subset
# (ping/tftp) + iperf3_embedded, over the lwip socket world (P7-2a). The
# shim shadow headers come FIRST on the netutils include path (they must
# beat the toolchain's declaration-only headers - the lwipopts shadow
# discipline); vendored sources compile -w (frozen import). The cherrysh
# adapter headers ride along for the command export face (CSH_*). Each tool
# is its own config sub-key so one factor is verified at a time.
CONFIG_NETUTILS ?= n
ifeq ($(filter $(CONFIG_NETUTILS),y n),)
$(error CONFIG_NETUTILS must be y or n, got '$(CONFIG_NETUTILS)')
endif
ifeq ($(CONFIG_NETUTILS),y)
ifeq ($(filter y,$(CONFIG_LWIP)),)
$(error CONFIG_NETUTILS=y requires CONFIG_LWIP=y (socket citizens))
endif
CONFIG_NETUTILS_PING ?= y
CONFIG_NETUTILS_TFTP ?= y
CONFIG_NETUTILS_IPERF3 ?= y
CONFIG_NETUTILS_IPERF2 ?= y

# N7 R5b boot autorun (unattended measurement, console-RX outage workaround):
# non-empty host = one 10 s lwiperf client burst after net READY; SCMI set
# 0 = boot probe is read-only, an OPP row = rate set through BL31.
CONFIG_IPERF2_AUTO_HOST ?=
CONFIG_SCMI_BOOT_SET_MHZ ?= 0
APP_AUTORUN_CFLAGS :=
ifneq ($(strip $(CONFIG_IPERF2_AUTO_HOST)),)
APP_AUTORUN_CFLAGS += -DCONFIG_IPERF2_AUTO_HOST='"$(CONFIG_IPERF2_AUTO_HOST)"' \
	-Ios/netutils
endif
ifneq ($(strip $(CONFIG_SCMI_BOOT_SET_MHZ)),0)
APP_AUTORUN_CFLAGS += -DCONFIG_SCMI_BOOT_SET_MHZ=$(CONFIG_SCMI_BOOT_SET_MHZ)
endif
ifeq ($(CONFIG_SCMI_PROBE),y)
APP_AUTORUN_CFLAGS += -DCONFIG_SCMI_PROBE=1
endif

NU_DIR := components/netutils
IPE_DIR := components/iperf3_embedded
NU_VENDORED_SRCS :=
NU_PORT_SRCS := os/netutils/netutils_shim.c os/netutils/tftp_port.c \
	os/netutils/iperf3_port.c os/netutils/iperf3_cmd.c
# lwiperf client front end (N7 R3): the no-copy TX measurement shape.
ifeq ($(CONFIG_NETUTILS_IPERF2),y)
NU_PORT_SRCS += os/netutils/iperf2_cmd.c
endif
ifeq ($(CONFIG_NETUTILS_PING),y)
NU_VENDORED_SRCS += $(NU_DIR)/ping/ping.c
endif
ifeq ($(CONFIG_NETUTILS_TFTP),y)
NU_VENDORED_SRCS += $(NU_DIR)/tftp/tftp_client.c $(NU_DIR)/tftp/tftp_server.c \
	$(NU_DIR)/tftp/tftp_xfer.c
endif
ifeq ($(CONFIG_NETUTILS_IPERF3),y)
NU_VENDORED_SRCS += $(IPE_DIR)/iperf3_embedded.c
endif
SRCS += $(NU_VENDORED_SRCS) $(NU_PORT_SRCS)

NU_CFLAGS := -Ios/netutils/shim -Ios/netutils \
	-I$(NU_DIR) -I$(NU_DIR)/tftp -I$(IPE_DIR) \
	-Iport/adapters/cherrysh -Icomponents/cherrysh \
	-include os/baremetal/minilibc.h \
	-DCONFIG_NETUTILS_PING=$(if $(filter y,$(CONFIG_NETUTILS_PING)),1,0) \
	-DCONFIG_NETUTILS_TFTP=$(if $(filter y,$(CONFIG_NETUTILS_TFTP)),1,0) \
	-DCONFIG_NETUTILS_IPERF3=$(if $(filter y,$(CONFIG_NETUTILS_IPERF3)),1,0) \
	-DCONFIG_NETUTILS_IPERF2=$(if $(filter y,$(CONFIG_NETUTILS_IPERF2)),1,0) \
	$(LWIP_CFLAGS) $(CSH_CFLAGS)
$(BUILD)/components/netutils/%.o: EXTRAFLAGS := -w $(NU_CFLAGS)
$(BUILD)/components/iperf3_embedded/%.o: EXTRAFLAGS := -w $(NU_CFLAGS)
$(BUILD)/os/netutils/%.o: EXTRAFLAGS := $(NU_CFLAGS)
endif

# wpa_supplicant (SPEC S7a/wpa-supplicant-rtos-adapt §3.5): the PSK-only
# compile set (no EAP/WPS/P2P/ctrl-iface/SME), the list the prior project
# board-proved on this fork's pin. Vendored sources compile with warnings
# silenced (WPA_CFG's -w): frozen imports, edited only through patches/
# (make modules replays them). The whole wpa world gets wpa_port_config.h
# force-included, which steers the fork's includes.h/build_config.h onto
# the plain-libc path (CONFIG_OS_EMBOX) and selects the driver slot in
# src/drivers/drivers.c (CONFIG_DRIVER_EMBOX; the ops live in the trunk
# skeleton os/wpa_supplicant/driver_vendor.c). The shim/ directory comes
# first for the wpa-world units: the toolchain libc has no net/if.h or
# netinet/in.h, and a real one would drag a BSD socket world in with it.
CONFIG_WPA_SUPPLICANT ?= n
ifeq ($(filter $(CONFIG_WPA_SUPPLICANT),y n),)
$(error CONFIG_WPA_SUPPLICANT must be y or n, got '$(CONFIG_WPA_SUPPLICANT)')
endif
ifeq ($(CONFIG_WPA_SUPPLICANT),y)
ifeq ($(filter y,$(CONFIG_SHELL)),)
$(error CONFIG_WPA_SUPPLICANT=y requires CONFIG_SHELL=y (wpa_cmd is the control face))
endif
WPA_DIR := third-party/wpa_supplicant

# Driver backend (FR-3 single-slot): "vendor" compiles the trunk
# skeleton against the registered wpa_vendor_port table (wlan configs);
# "none" compiles the vendored stub driver (smoke image, P7a-1).
CONFIG_WPA_BACKEND ?= none
ifeq ($(filter $(CONFIG_WPA_BACKEND),vendor none),)
$(error CONFIG_WPA_BACKEND must be vendor or none, got '$(CONFIG_WPA_BACKEND)')
endif
# l2_packet carrier: "direct" is the first-party EAPOL carrier (P7a-2);
# "none" is the vendored stub (P7a-1 smoke).
CONFIG_WPA_L2 ?= direct
ifeq ($(filter $(CONFIG_WPA_L2),direct none),)
$(error CONFIG_WPA_L2 must be direct or none, got '$(CONFIG_WPA_L2)')
endif
CONFIG_WPA_THREAD_STACK ?= 65536
# [W7 print-clean 2026-10-03] configs/ owns the knob (Makefile is a pure
# consumer - no defaults here); 2 = MSG_DEBUG chatter, 3 = MSG_INFO keeps
# the anchors (CTRL-EVENT-CONNECTED, disassociation, errors)
ifeq ($(CONFIG_WPA_SUPP_DEBUG_LEVEL),)
$(error CONFIG_WPA_SUPP_DEBUG_LEVEL must be set in the config (configs/ owns it))
endif

WPA_CORE_SRCS := \
	$(WPA_DIR)/src/common/wpa_common.c \
	$(WPA_DIR)/src/common/ieee802_11_common.c \
	$(WPA_DIR)/src/common/hw_features_common.c \
	$(WPA_DIR)/src/drivers/driver_common.c \
	$(WPA_DIR)/src/drivers/drivers.c \
	$(WPA_DIR)/src/rsn_supp/wpa.c \
	$(WPA_DIR)/src/rsn_supp/wpa_ie.c \
	$(WPA_DIR)/src/rsn_supp/pmksa_cache.c \
	$(WPA_DIR)/src/rsn_supp/preauth.c \
	$(WPA_DIR)/src/utils/common.c \
	$(WPA_DIR)/src/utils/wpabuf.c \
	$(WPA_DIR)/src/utils/base64.c \
	$(WPA_DIR)/src/utils/bitfield.c \
	$(WPA_DIR)/src/utils/wpa_debug.c \
	$(WPA_DIR)/src/crypto/crypto_internal.c \
	$(WPA_DIR)/src/crypto/aes-internal.c \
	$(WPA_DIR)/src/crypto/aes-internal-dec.c \
	$(WPA_DIR)/src/crypto/aes-internal-enc.c \
	$(WPA_DIR)/src/crypto/aes-wrap.c \
	$(WPA_DIR)/src/crypto/aes-unwrap.c \
	$(WPA_DIR)/src/crypto/aes-omac1.c \
	$(WPA_DIR)/src/crypto/sha1.c \
	$(WPA_DIR)/src/crypto/sha1-internal.c \
	$(WPA_DIR)/src/crypto/sha1-prf.c \
	$(WPA_DIR)/src/crypto/sha1-pbkdf2.c \
	$(WPA_DIR)/src/crypto/md5.c \
	$(WPA_DIR)/src/crypto/md5-internal.c \
	$(WPA_DIR)/src/crypto/rc4.c \
	$(WPA_DIR)/src/crypto/sha256.c \
	$(WPA_DIR)/src/crypto/sha256-internal.c \
	$(WPA_DIR)/src/crypto/sha256-prf.c \
	$(WPA_DIR)/src/crypto/tls_none.c \
	$(WPA_DIR)/wpa_supplicant/wpa_supplicant.c \
	$(WPA_DIR)/wpa_supplicant/events.c \
	$(WPA_DIR)/wpa_supplicant/scan.c \
	$(WPA_DIR)/wpa_supplicant/bss.c \
	$(WPA_DIR)/wpa_supplicant/config.c \
	$(WPA_DIR)/wpa_supplicant/config_none.c \
	$(WPA_DIR)/wpa_supplicant/notify.c \
	$(WPA_DIR)/wpa_supplicant/wpas_glue.c \
	$(WPA_DIR)/wpa_supplicant/bssid_ignore.c \
	$(WPA_DIR)/wpa_supplicant/eap_register.c \
	$(WPA_DIR)/wpa_supplicant/op_classes.c \
	$(WPA_DIR)/wpa_supplicant/rrm.c \
	$(WPA_DIR)/wpa_supplicant/robust_av.c

WPA_DRIVER_SRCS := os/wpa_supplicant/driver_vendor.c
WPA_L2_SRCS := os/wpa_supplicant/l2_packet_direct.c
WPA_BACKEND_KEY := -DCONFIG_WPA_BACKEND_VENDOR=1 \
	-DWPA_PORT_DRIVER_NAME='"embox"'
ifeq ($(CONFIG_WPA_BACKEND),none)
WPA_DRIVER_SRCS := $(WPA_DIR)/src/drivers/driver_none.c
WPA_BACKEND_KEY := -DCONFIG_WPA_BACKEND_NONE=1 \
	-DWPA_PORT_DRIVER_NAME='"none"'
endif
ifeq ($(CONFIG_WPA_L2),none)
WPA_L2_SRCS := $(WPA_DIR)/src/l2_packet/l2_packet_none.c
endif

WPA_PORT_SRCS := \
	os/wpa_supplicant/wpa_vendor_port.c \
	os/wpa_supplicant/os_port.c \
	os/wpa_supplicant/eloop_port.c \
	os/wpa_supplicant/supp_main.c \
	os/wpa_supplicant/wpa_cmd.c

SRCS += $(WPA_CORE_SRCS) $(WPA_DRIVER_SRCS) $(WPA_L2_SRCS) $(WPA_PORT_SRCS)
# the freestanding libc completion in os/baremetal/minilibc.c is gated on
# this key so configs without wpa keep their exact image size (P7a-2
# zero-intrusion criterion)
CFLAGS += -DCONFIG_LIBC_COMPLETION=1

WPA_INC := -Ios/wpa_supplicant/shim \
	-I$(WPA_DIR) -I$(WPA_DIR)/src -I$(WPA_DIR)/src/utils \
	-I$(WPA_DIR)/src/drivers -I$(WPA_DIR)/src/l2_packet \
	-I$(WPA_DIR)/wpa_supplicant -Ios/wpa_supplicant
# The vendor driver (rtw_security.c) bundles its own hostap-derived
# omac1_aes_128; the supplicant's copy is renamed at the compile flag so
# the two worlds coexist in one image without touching either tree.
# [S5b-W4] the 8851 (V5) tree goes further: core/crypto/ is a full
# hostap-derived crypto library (rtw_crypto_wrap.c + aes-internal* +
# sha256* + aes-omac1) that collides on 19 symbols - the same rename
# treatment extends to the whole intersection (link evidence
# runs/20261004-rtl8851bu-w4, all names WPA-world-only by grep).
WPA_CFG := -include stdarg.h \
	-include os/wpa_supplicant/wpa_port_config.h \
	$(WPA_BACKEND_KEY) \
	-Domac1_aes_128=wpa_rtos_omac1_aes_128 \
	-Domac1_aes_256=wpa_rtos_omac1_aes_256 \
	-DTe0=wpa_rtos_Te0 -DTd0=wpa_rtos_Td0 \
	-DTd4s=wpa_rtos_Td4s -Drcons=wpa_rtos_rcons \
	-Domac1_aes_vector=wpa_rtos_omac1_aes_vector \
	-Domac1_aes_128_vector=wpa_rtos_omac1_aes_128_vector \
	-DrijndaelKeySetupEnc=wpa_rtos_rijndaelKeySetupEnc \
	-Daes_encrypt=wpa_rtos_aes_encrypt \
	-Daes_encrypt_init=wpa_rtos_aes_encrypt_init \
	-Daes_encrypt_deinit=wpa_rtos_aes_encrypt_deinit \
	-Dsha256_vector=wpa_rtos_sha256_vector \
	-Dsha256_process=wpa_rtos_sha256_process \
	-Dsha256_done=wpa_rtos_sha256_done \
	-Dsha256_prf=wpa_rtos_sha256_prf \
	-Dsha256_prf_bits=wpa_rtos_sha256_prf_bits \
	-Dhmac_sha256=wpa_rtos_hmac_sha256 \
	-Dhmac_sha256_vector=wpa_rtos_hmac_sha256_vector \
	-Dforced_memzero=wpa_rtos_forced_memzero \
	-Dwpa_printf=wpa_rtos_wpa_printf \
	-Dwpa_hexdump=wpa_rtos_wpa_hexdump \
	-Dwpa_hexdump_key=wpa_rtos_wpa_hexdump_key \
	-Dos_memdup=wpa_rtos_os_memdup \
	-Dos_memcmp_const=wpa_rtos_os_memcmp_const \
	-Dglobal_op_class=wpa_rtos_global_op_class \
	-Dhex2byte=wpa_rtos_hex2byte \
	-Dhexstr2bin=wpa_rtos_hexstr2bin \
	-Dbin_clear_free=wpa_rtos_bin_clear_free \
	-Dsscanf=wpa_port_sscanf \
	-DCONFIG_WPA_THREAD_STACK=$(CONFIG_WPA_THREAD_STACK) \
	-DCONFIG_WPA_SUPP_DEBUG_LEVEL=$(CONFIG_WPA_SUPP_DEBUG_LEVEL)
# vendored units silence warnings (-w, frozen import); the first-party
# port layer compiles warning-honest under the global -Wall -Wextra
$(BUILD)/third-party/wpa_supplicant/%.o: \
	EXTRAFLAGS := -w $(WPA_CFG) $(WPA_INC)
# the port layer adds the shell include set (wpa_cmd exports the command)
$(BUILD)/os/wpa_supplicant/%.o: \
	EXTRAFLAGS := $(WPA_CFG) $(WPA_INC) $(CSH_CFLAGS)
endif

OBJS := $(patsubst %.c,$(BUILD)/%.o,$(filter %.c,$(SRCS))) \
	$(patsubst %.S,$(BUILD)/%.o,$(filter %.S,$(SRCS)))
DEPS := $(OBJS:.o=.d)

LDFLAGS := -nostdlib -static -T arch/aarch64/baremetal.ld \
	-Wl,--build-id=none -Wl,--no-warn-rwx-segments -Wl,-Map=$(TARGET).map

.DEFAULT_GOAL := all
.PHONY: all gates check deploy clean modules FORCE

# Submodule materialization (patches/cherryusb/README.md): update, set the
# cherryusb sparse-checkout (non-cone; the phytium blob subdir and the
# demo/tests/docs bulk stay out), then replay every patches/<comp>/*.patch
# into its submodule work tree. Idempotent: an already-applied patch fails
# apply --check and is reported "kept". A fresh environment that cannot
# fetch the cherryusb pin (upstream dev force-pushed it away) seeds per
# patches/cherryusb/README.md first.
modules:
	@git submodule update --init third-party/threadx third-party/cherryusb \
		third-party/lwip third-party/rtl8821cu \
		third-party/wpa_supplicant || \
		echo 'note: submodule update failed; see patches/cherryusb/README.md (pin seeding)'
	@if [ -d third-party/cherryusb ]; then \
		git -C third-party/cherryusb sparse-checkout set --no-cone \
			'/*' '!/*/' '/core/' '/common/' \
			'/class/' '!/class/*/' '/class/hub/' '/class/msc/' \
			'/port/' '!/port/*/' '/port/ehci/' \
			'/port/xhci/' '!/port/xhci/*/' \
			'/osal/' || \
			echo 'note: cherryusb sparse-checkout not set (kept full checkout)'; \
	fi
	@for p in patches/*/*.patch; do \
		[ -e "$$p" ] || continue; \
		comp=$$(printf '%s' "$$p" | cut -d/ -f2); \
		if git -C third-party/$$comp apply --check "$$PWD/$$p" 2>/dev/null; then \
			git -C third-party/$$comp apply "$$PWD/$$p" && echo "applied $$p"; \
		else \
			echo "kept    $$p (already applied or inapplicable)"; \
		fi; \
	done

all: $(TARGET).bin

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRAFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRAFLAGS) -MMD -MP -c $< -o $@

$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	@printf 'image: %s (%s bytes)\n' $@ "$$(stat -c%s $@)"

# Merge gates (SPEC v3 §3.8.3):
#  1. cleanroom scan - vendor-trace identifiers must not appear anywhere in
#     the source tree (adapted sources are rewritten, references cite the
#     mechanism, never the vendor naming). The ft_ token is word-bounded:
#     standalone cleanroom-prefixed identifiers still fail, while Linux API
#     names that merely contain the substring (cfg80211_ft_event - linkage
#     fixed by the vendor header macro, S5b-W3) pass;
#  2. print-discipline scan - a literal carriage return in a source file
#     means someone wrote "\r\n" instead of letting the sink own the wire
#     format (the log.c sink is CRLF-literate; sources are not).
gates:
	@if grep -rnE 'phytium|fparameters|fgic|\bft_|fboot|fcrt0' \
		arch board os app port configs 2>/dev/null; then \
		echo 'gates: cleanroom-scan FAIL'; exit 1; \
	else echo 'gates: cleanroom-scan PASS'; fi
	@if grep -rlP '\r' --include='*.c' --include='*.h' --include='*.S' \
		arch board os app port 2>/dev/null; then \
		echo 'gates: crlf-scan FAIL'; exit 1; \
	else echo 'gates: crlf-scan PASS'; fi

# Build self-checks (SPEC v3 §4/R1 判据 1):
#  - _start must sit at the load address and be the image's first
#    instruction (`go` jumps at the load address without reading any header);
#  - .bss must be NOBITS (the linker script marks it NOLOAD; the MMU page
#    tables live there as plain arrays);
#  - no libgcc helper may leak into the link (A64 has hardware integer
#    division; a missing __udivdi3 is a link error, a leaked one is a
#    dependency nobody vendored).
# CMSIS scheduler-class symbol gate, split by CONFIG_OS (SPEC S3 v2.1
# §3.8): baremetal must export NONE; threadx legitimately defines some via
# the shim, so the check is provenance - only cmsis2_threadx.o may define
# them.
ifeq ($(CONFIG_OS),threadx)
CHECK_CMSIS_CMD := @if $(NM) -o $(filter-out %cmsis2_threadx.o,$(OBJS)) | \
	grep -E ' [TDBR] (osThread|osSemaphore|osMutex|osEventFlags|osTimer|osMessage|osMemoryPool)'; then \
	echo 'check: CMSIS scheduler symbol outside cmsis2_threadx.o - FAIL'; exit 1; \
	else echo 'check: CMSIS scheduler symbols confined to cmsis2_threadx.o PASS'; fi
else
CHECK_CMSIS_CMD := @if $(NM) $(TARGET).elf | \
	grep -E ' (osThread|osSemaphore|osMutex|osEventFlags|osTimer|osMessage|osMemoryPool)'; then \
	echo 'check: scheduler-class CMSIS symbol exported - FAIL'; exit 1; \
	else echo 'check: no scheduler-class CMSIS symbols PASS'; fi
endif

check: $(TARGET).elf
	@if [ "$$($(NM) $(TARGET).elf | \
		awk '$$3 == "_start" {print substr($$1, length($$1)-7)}')" \
		!= "0a000000" ]; then \
		echo 'check: _start not at 0x0a000000 - FAIL'; exit 1; fi
	@echo 'check: _start at 0x0a000000 PASS'
	@$(OBJDUMP) -d $(TARGET).elf | sed -n '/<_start>:/,+2p'
	@if $(READELF) -S -W $(TARGET).elf | grep '[[:space:]]\.bss[[:space:]]' \
		| grep -q NOBITS; then \
		echo 'check: .bss NOBITS PASS'; \
	else \
		echo 'check: .bss not NOBITS - FAIL'; exit 1; fi
	@if $(NM) $(TARGET).elf | grep -E '__udivdi3|__aeabi_|__muldi3'; then \
		echo 'check: libgcc helper leaked - FAIL'; exit 1; \
	else echo 'check: no libgcc helpers PASS'; fi
	$(CHECK_CMSIS_CMD)

# Deploy to the TFTP root under the name the board's boot profile expects.
# The deploy report (size + crc32 + the sha256 chain) is what the
# boot_os transcript is checked against: "Bytes transferred" must equal the
# size and the board-side crc32 must equal the local crc32 - a mismatch is a
# DEPLOYMENT problem (stale TFTP file on the Windows side has happened) and
# debugging code before fixing the chain is ghost debugging.
deploy: $(TARGET).bin
	@printf 'before: '; sha256sum $(TFTP_FILE) 2>/dev/null || echo '(absent)'
	cp $(TARGET).bin $(TFTP_FILE)
	@printf 'after : '; sha256sum $(TFTP_FILE)
	@printf 'local : '; sha256sum $(TARGET).bin
	@printf 'deploy: %s bytes crc32=%08x\n' \
		"$$(stat -c%s $(TFTP_FILE))" \
		"$$(python3 -c 'import sys, zlib; print(zlib.crc32(open(sys.argv[1], "rb").read()) & 0xffffffff)' $(TFTP_FILE))"

clean:
	rm -rf $(BUILD)

-include $(DEPS)

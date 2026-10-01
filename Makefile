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
	port/adapters/linuxkpi/usbkpi_cmds.c \
	app/lkpi_udisk/lkpi_udisk_core.c \
	app/lkpi_udisk/lkpi_udisk_rtos.c \
	app/lkpi_hub/lkpi_hub_core.c \
	app/lkpi_hub/lkpi_hub_rtos.c
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
# TX include set rides along: the impl layer embeds ThreadX primitives
# (semaphores/timers) exactly like the cherryusb osal does (FR-3: tx
# headers stay inside adapter/implementation units).
LKPI_CFLAGS := $(USB_CFLAGS) $(TX_CFLAGS) -I$(LKPI_DIR)/include \
	-I$(LKPI_DIR) -include os/baremetal/minilibc.h
$(BUILD)/components/linuxkpi/%.o: EXTRAFLAGS := $(LKPI_CFLAGS)
$(BUILD)/port/adapters/linuxkpi/%.o: \
	EXTRAFLAGS := $(LKPI_CFLAGS) $(CSH_CFLAGS)
$(BUILD)/app/lkpi_udisk/%.o: EXTRAFLAGS := $(LKPI_CFLAGS) $(CSH_CFLAGS)
$(BUILD)/app/lkpi_hub/%.o: EXTRAFLAGS := $(LKPI_CFLAGS) $(CSH_CFLAGS)
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
CONFIG_GMAC_PORT ?= 1
ifeq ($(filter $(CONFIG_GMAC_PORT),0 1),)
$(error CONFIG_GMAC_PORT must be 0 or 1, got '$(CONFIG_GMAC_PORT)')
endif

LWIP_DIR := third-party/lwip
# GMAC driver trio (P7-1b, spec §3.4): the board-verified EQOS MAC core +
# RTL8211F PHY + RK3568 glue/board table, re-arranged onto gicv3/cache/log.
# The unselected port (CONFIG_GMAC_PORT) is data-only - never initialized.
SRCS += \
	board/$(BOARD)/drivers/dwc_eqos.c \
	board/$(BOARD)/drivers/rtl8211f.c \
	board/$(BOARD)/drivers/eqos_rk3568.c
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
	os/lwip/ethnetif.c \
	os/lwip/cmsis/sys_arch.c \
	os/lwip/lwip_diag.c
SRCS += $(LWIP_SRCS)

LWIP_CFLAGS := -Ios/lwip/include -I$(LWIP_DIR)/src/include \
	-DCONFIG_GMAC_PORT=$(CONFIG_GMAC_PORT)
$(BUILD)/third-party/lwip/%.o: EXTRAFLAGS := -w $(LWIP_CFLAGS)
$(BUILD)/os/lwip/%.o: EXTRAFLAGS := $(LWIP_CFLAGS)
$(BUILD)/app/lwip_lan/%.o: EXTRAFLAGS := $(LWIP_CFLAGS)
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

NU_DIR := components/netutils
IPE_DIR := components/iperf3_embedded
NU_VENDORED_SRCS :=
NU_PORT_SRCS := os/netutils/netutils_shim.c os/netutils/tftp_port.c \
	os/netutils/iperf3_port.c os/netutils/iperf3_cmd.c
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
	$(LWIP_CFLAGS) $(CSH_CFLAGS)
$(BUILD)/components/netutils/%.o: EXTRAFLAGS := -w $(NU_CFLAGS)
$(BUILD)/components/iperf3_embedded/%.o: EXTRAFLAGS := -w $(NU_CFLAGS)
$(BUILD)/os/netutils/%.o: EXTRAFLAGS := $(NU_CFLAGS)
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
		third-party/lwip || \
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
#     mechanism, never the vendor naming);
#  2. print-discipline scan - a literal carriage return in a source file
#     means someone wrote "\r\n" instead of letting the sink own the wire
#     format (the log.c sink is CRLF-literate; sources are not).
gates:
	@if grep -rnE 'phytium|fparameters|fgic|ft_|fboot|fcrt0' \
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

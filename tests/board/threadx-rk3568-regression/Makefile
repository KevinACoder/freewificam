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

# TCP/IP stack domain: the vendored import and the project's port layer. The
# port's include directory comes first on the include path: lwipopts.h and
# arch/cc.h are supplied by shadowing the stack's own include root (the
# standard mechanism lwIP reaches them through).
LWIP := $(shell sh $(ROOT)/tools/patches/replay.sh lwip)
LWIP_PORT := $(ROOT)/ports/lwip
LWIP_INCLUDES := \
	-I$(LWIP_PORT) \
	-I$(LWIP_PORT)/include \
	-I$(LWIP)/src/include \
	-I$(LWIP)/contrib/apps/ping

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

# The build tag is stamped into the banner, so it is a real build input: it is
# written to a stamp file and every object depends on it, or a tag that changes
# without a source change would leave the old tag inside a "fresh" image (the
# dirty-tree builds of this round did exactly that).
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
	-I$(ROOT)/components/ulog/src \
	-DULOG_ENABLED -DULOG_MAX_MESSAGE_LENGTH=256 \
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
	$(if $(EPK),-I$(TX)/utility/execution_profile_kit) \
	-DTX_INCLUDE_USER_DEFINE_FILE -DEL1=1 \
	-DCONFIG_TX_TICKS_PER_SECOND=1000 -DCONFIG_TX_TIMER_STACK=4096

# Optimization level is a knob: the regression loop runs -Og (fast builds,
# readable code), a measurement round passes OPT=-O2 - the small A55 core at
# -Og produces fake performance numbers (the prior line's lesson, recorded in
# the import spec's design notes), so throughput evidence must state and use
# the optimized tier.
# RX checksum offload is a lockstep switch: the driver enables the GMAC IPC
# block and drops RDES1-flagged frames while lwipopts turns the software
# verification off - so the define must reach every translation unit, both
# the driver's and the stack's (lwipopts.h keys its block on this macro).
# Measurement levers: the wired throughput ladder moves one arm at a time, so
# a treatment is a build knob rather than an edit (the pattern of the rtl8189
# suite's LEVER_CFLAGS block, tests/board/threadx-rk3568-rtl8189/Makefile:
# 101-121). Unset = the shipped defaults; the round's ledger names the value
# each boot carried.
LEVER_CFLAGS :=
ifneq ($(COALESCE),)
LEVER_CFLAGS += -DCONFIG_GMAC_RX_COALESCE=1 -DCONFIG_GMAC_RX_RIWT=$(COALESCE)
endif
ifneq ($(ZC),)
LEVER_CFLAGS += -DCONFIG_GMAC_RX_ZC=1
endif
ifneq ($(IRQ_BREAKER),)
LEVER_CFLAGS += -DCONFIG_GMAC_IRQ_BREAKER=1
endif
# TX ladder: one arm at a time. TX_RING widens the descriptor ring,
# TX_IOC_DIV shortens the completion interval, TX_NONBLOCK stops the sender
# parking the tcpip thread on a full ring.
ifneq ($(TX_RING),)
LEVER_CFLAGS += -DCONFIG_GMAC_TX_RING=$(TX_RING)
endif
ifneq ($(TX_IOC_DIV),)
LEVER_CFLAGS += -DCONFIG_GMAC_TX_IOC_DIV=$(TX_IOC_DIV)
endif
ifneq ($(TX_NONBLOCK),)
LEVER_CFLAGS += -DCONFIG_GMAC_TX_NONBLOCK=1
endif
# Interrupt-shape levers. RXPOLL removes every DMA interrupt (the RX thread
# polls). The bottom-half split (ISR signals, the RX thread sweeps) is the
# DEFAULT since 2026-10-17; RXBH=0 is the reverse lever back to the
# interrupt-context sweep. NAPI is the bottom half plus the RI mask and the
# frame budget; RXPRIO lifts the RX thread above the TCP/IP thread.
ifneq ($(RXPOLL),)
LEVER_CFLAGS += -DCONFIG_GMAC_RX_POLL=1
endif
ifneq ($(RXBH),)
LEVER_CFLAGS += -DCONFIG_GMAC_RX_BH=$(RXBH)
endif
ifneq ($(NAPI),)
LEVER_CFLAGS += -DCONFIG_GMAC_RX_NAPI=1
endif
ifneq ($(RXPRIO),)
LEVER_CFLAGS += -DCONFIG_GMAC_RX_PRIO_HI=1
endif
# lwIP input path: CORELOCKIN=1 runs the stack's input on the caller's (RX)
# thread under the core lock instead of one mailbox post per frame. The option
# has an #ifndef wrapper in ports/lwip/include/lwipopts.h so a -D can win.
# 2026-10-18 board-measured: +13% up (332 -> 375) and +22% down (461 -> 563)
# with zero loss - the first arm to move both directions. Adopted as this
# round's baseline.
ifneq ($(CORELOCKIN),)
LEVER_CFLAGS += -DLWIP_TCPIP_CORE_LOCKING_INPUT=1
endif
# TX checksum offload: the driver writes the pseudo-header sum and tags CIC=3
# so the MAC inserts the IPv4/TCP and IPv4/UDP checksums (dwc_eqos.c
# eqos_tx_csum_prepare; TRM Part2 ch.20 TDES3[17:16]). lwipopts.h flips the
# per-netif checksum control in lockstep and ethnetif.c clears the GEN bits
# for this netif; ICMP stays software on both sides. The peer's own TCP
# checksum validation is the natural red control: wrong insertion cannot
# complete a session.
ifneq ($(TX_CSUM),)
LEVER_CFLAGS += -DCONFIG_GMAC_TX_CSUM=1
endif
# RX batch drain: one core-lock acquisition for the whole staged-frame drain
# instead of two CMSIS mutex calls per frame (ethnetif.c ethnetif_poll; the
# per-frame cost shows in the bridgeinput bucket). Requires CORELOCKIN, since
# the batch hands frames to ethernet_input directly under the lock it took.
ifneq ($(BATCH),)
LEVER_CFLAGS += -DCONFIG_GMAC_RX_BATCH=1
endif
# TCP-gap instrument tier: compiles MIB2's TCP segment counters (out/in and
# fast retransmits) into the stack - lwipopts.h keys its MIB2_STATS block on
# this macro, so it must reach both the stack TUs and the console TU that
# prints them (`gmac tcp`). Not a treatment: measured rounds state it in the
# ledger like any other build input.
ifneq ($(TCPPROBE),)
LEVER_CFLAGS += -DCONFIG_GMAC_TCP_PROBE=1
endif
# Input-bucket dissection (2026-10-19 round 2): each received frame's input
# call is timed in two halves - the core-lock acquisition and the
# ethernet_input body - and both are indexed by frame class (data / pure ACK
# / dup ACK / window update / UDP / other); the TX send-gap histogram rides
# the same switch. The lock sequence is byte-for-byte tcpip_inpkt's under
# LWIP_TCPIP_CORE_LOCKING_INPUT (tcpip.c:254-262); two extra counter reads
# per frame are the only cost. Mutually exclusive with BATCH (the batch holds
# the lock across the drain; the core lock is not recursive).
ifneq ($(INPUTSPLIT),)
LEVER_CFLAGS += -DCONFIG_GMAC_INPUT_SPLIT=1
endif
# Reference writes on the TCP send path (PBREF=1, 2026-10-19 round 4): flips
# LWIP_NETIF_TX_SINGLE_PBUF to 0 so tcp_write takes a PBUF_ROM reference to
# the application's data instead of allocating PBUF_RAM and copying. On the
# uplink the application (lwiperf) writes from const buffers and declares
# "no copying needed"; the driver still copies into its transmit buffers.
# Measurement lever: proves how much of the per-callback cost is the
# allocation + copy half of the write path. Default off.
ifneq ($(PBREF),)
LEVER_CFLAGS += -DLWIP_NETIF_TX_SINGLE_PBUF=0
endif
# RX copy-chain cut (2026-10-19, the cost-bound fix): the bridge reads each
# staged frame straight into the pool pbuf, dropping the linear scratch + the
# pbuf_take copy. One less 1500-byte memcpy per received frame.
ifneq ($(RXDIRECT),)
LEVER_CFLAGS += -DCONFIG_GMAC_RX_DIRECT=1
endif
# Fine TCP-path probe (2026-10-19 round 3): patch 0002's LWIP_HOOK_TCP_PROBE
# is pointed at the bridge's site-pair accumulator, so one tcp_input entry's
# cost splits into segment processing / sent callback / queue walk / wire
# handoff (`gmac tcpfine`). FINE=1 rides the INPUTSPLIT instrument tier.
# Measurement-only: a counter read per site.
ifneq ($(FINE),)
LEVER_CFLAGS += -DCONFIG_GMAC_TCP_FINE=1
endif
# Execution Profile Kit (EPK=1): rebuilds the kernel + the A55 port with
# TX_EXECUTION_PROFILE_ENABLE so every thread switch and ISR boundary
# accumulates per-thread execution time from CNTVCT (tx_user.h supplies the
# source), adds utility/execution_profile_kit to the compile set, and arms the
# `gmac etime` report. Measurement-only: the switch changes the TX_THREAD
# layout and adds a counter read per switch, so the shipped image stays
# byte-for-byte without it.
ifneq ($(EPK),)
LEVER_CFLAGS += -DTX_EXECUTION_PROFILE_ENABLE=1
endif
# TCP send-buffer size lever (codex hypothesis 3: large unsent/unacked queues
# make tcp_write and tcp_output walk long segment lists on every ACK). Smaller
# buffers shorten the walks; the receive window is untouched, so the flight
# limit stays 512 KB. The queue length follows the 4*SND_BUF/MSS floor.
ifneq ($(SNDBUF),)
LEVER_CFLAGS += -DTCP_SND_BUF=$(SNDBUF) \
	-DTCP_SND_QUEUELEN=$(shell echo $$(( $(SNDBUF) / 365 + 2 )))
endif
# TCP receive-window lever (WND=<bytes>). The receive window must fit the
# driver's RX ring: a window larger than the ring depth (128 slots) lets the
# peer put more frames in flight than the DMA can hold, and the ring
# overflows at the MAC. 131072 = 90 frames, safely inside the ring; the
# default 524288 (chosen for the SDIO line's long loaded RTT) is 358.
ifneq ($(WND),)
LEVER_CFLAGS += -DTCP_WND=$(WND)
endif
# Delayed-ACK watermark lever (ACKDELAY=N; patch 0003, default 2). N=4 holds
# the ACK across four in-order data segments - the downlink's per-frame cost
# is half ACK send (B1: ctrl+dack ~= 3.45 us/frame); halving the ACK count
# attacks that directly, at the price of feedback latency. RFC 1122 4.2.3.2
# says SHOULD every second segment, so non-2 values are measurement builds.
ifneq ($(ACKDELAY),)
LEVER_CFLAGS += -DLWIP_TCP_ACK_DELAY_SEGMENTS=$(ACKDELAY)
endif
# RX completion-sweep shape. The DEFAULT is the whole-ring scan (every
# measurement this line has is taken with it). SCAN=2 selects the bounded walk,
# which is FALSIFIED (2026-10-17: downlink 481 -> 2.0-2.6 Mbit/s, ovf=173, with
# stale=0) and kept only as a lever for the tail-semantics question.
ifneq ($(SCAN),)
LEVER_CFLAGS += -DCONFIG_GMAC_RX_SCAN=$(SCAN)
endif
# Sweep probe shape: SWEEPWIDE=1 pulls every descriptor's status word in one
# store-free pass before processing, so the non-cacheable loads pipeline
# instead of serialising behind the arming writes. Semantics unchanged.
ifneq ($(SWEEPWIDE),)
LEVER_CFLAGS += -DCONFIG_GMAC_SWEEP_WIDE=1
endif
# RX ring depth: the sweep is O(depth) per interrupt and only ~1 frame completes
# per sweep at line rate, so depth is the probes-per-frame multiplier (128 ->
# 66.4 probes/frame measured). Shallow rings trade burst tolerance for a
# proportional cut in the dominant per-frame cost.
ifneq ($(RX_RING),)
LEVER_CFLAGS += -DCONFIG_GMAC_RX_RING=$(RX_RING)
endif

OPT ?= -Og
BASE_CFLAGS := $(OPT) -g3 -std=c11 -Wall -Wextra \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-march=armv8-a -mgeneral-regs-only -mstrict-align -mno-outline-atomics \
	-ffunction-sections -fdata-sections \
	-DFW_BUILD_TAG='"$(BUILD_TAG)"' \
	-DBOARD_SUITE_NAME='"$(SUITE_NAME)"' \
	-DCMSIS2_BACKEND_THREADX \
	-DCONFIG_GMAC_RX_HWCSUM=1 \
	$(LEVER_CFLAGS) \
	-DSHELL_PORT_HOST='"rk3568"'

# The freestanding libc surface is declared everywhere it is used: the vendored
# shell core calls strnlen without a prototype of its own, and the runner builds
# its start line with snprintf. Assembly files do not take the C header.
CFLAGS := $(BASE_CFLAGS) -include $(ROOT)/os/baremetal/minilibc.h \
	-DLOG_CMD_USB_KEY=0
ASM_CFLAGS := $(BASE_CFLAGS)

# The vendored shell core is byte-identical upstream and cannot be edited; it
# does trip -Wextra's sign-compare in one builtin. Its translation units are
# compiled at the -Wall level the upstream project itself uses. The imported
# USB and TCP/IP stacks' sources get the same treatment for the same reason -
# they are bytes this project does not own. (Recursive assignment: the
# imported lists are defined below this point.)
VENDORED_WARNED_SRCS = $(VENDORED_SRCS) $(USB_STACK_SRCS) $(LWIP_STACK_SRCS)
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
	$(ROOT)/board/rk3568-itx/reset.c \
	$(ROOT)/os/baremetal/memops.c \
	$(ROOT)/os/baremetal/minilibc.c \
	$(ROOT)/os/baremetal/log.c \
	$(ROOT)/components/ulog/src/ulog.c \
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
	$(ROOT)/os/threadx/cmsis2_ext.c \
	$(if $(EPK),$(TX)/utility/execution_profile_kit/tx_execution_profile.c)

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
	$(COMMON)/cases/kernel_up.c \
	$(COMMON)/cases/tick_ladder.c \
	$(COMMON)/cases/thread_sched.c \
	$(COMMON)/cases/cmsis2_prio.c \
	$(CURDIR)/tests/tlsf_heap.c \
	$(COMMON)/cases/os_timer.c \
	$(COMMON)/cases/usb_msc.c \
	$(COMMON)/cases/usb_xhci.c \
	$(COMMON)/cases/usb_ehci.c \
	$(COMMON)/cases/lwip_smoke.c \
	$(COMMON)/lwip_stack.c \
	$(COMMON)/cases/gmac_dataplane.c

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
	$(USB_PORT)/osal/cmsis2/osal_cmsis2.c \
	$(USB_PORT)/class/msc_app.c

USB_STACK_SRCS := \
	$(CHERRYUSB)/core/usbh_core.c \
	$(CHERRYUSB)/class/hub/usbh_hub.c \
	$(CHERRYUSB)/class/msc/usbh_msc.c \
	$(CHERRYUSB)/port/ehci/usb_hc_ehci.c \
	$(CHERRYUSB)/port/ehci/usb_hc_ehci_iso.c \
	$(CHERRYUSB)/port/xhci/usb_hc_xhci.c

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

# --- TCP/IP stack domain (imported stack + the project's port layer) --------
#
# The compile set mirrors src/Filelists.mk's groups, trimmed to what this
# project enables (IPv4, the sequential/socket APIs, lwiperf); the contrib
# ping is added when the data-plane round needs it. The port layer compiles
# under the project's own full warning set, the imported bytes under the
# upstream level.
LWIP_PORT_SRCS := \
	$(LWIP_PORT)/sys_arch.c \
	$(LWIP_PORT)/platform_hooks.c \
	$(LWIP_PORT)/ethnetif.c

LWIP_STACK_SRCS := \
	$(LWIP)/src/core/init.c \
	$(LWIP)/src/core/def.c \
	$(LWIP)/src/core/dns.c \
	$(LWIP)/src/core/inet_chksum.c \
	$(LWIP)/src/core/ip.c \
	$(LWIP)/src/core/mem.c \
	$(LWIP)/src/core/memp.c \
	$(LWIP)/src/core/netif.c \
	$(LWIP)/src/core/pbuf.c \
	$(LWIP)/src/core/raw.c \
	$(LWIP)/src/core/stats.c \
	$(LWIP)/src/core/sys.c \
	$(LWIP)/src/core/altcp.c \
	$(LWIP)/src/core/altcp_alloc.c \
	$(LWIP)/src/core/altcp_tcp.c \
	$(LWIP)/src/core/tcp.c \
	$(LWIP)/src/core/tcp_in.c \
	$(LWIP)/src/core/tcp_out.c \
	$(LWIP)/src/core/timeouts.c \
	$(LWIP)/src/core/udp.c \
	$(LWIP)/src/core/ipv4/acd.c \
	$(LWIP)/src/core/ipv4/autoip.c \
	$(LWIP)/src/core/ipv4/dhcp.c \
	$(LWIP)/src/core/ipv4/etharp.c \
	$(LWIP)/src/core/ipv4/icmp.c \
	$(LWIP)/src/core/ipv4/igmp.c \
	$(LWIP)/src/core/ipv4/ip4_frag.c \
	$(LWIP)/src/core/ipv4/ip4.c \
	$(LWIP)/src/core/ipv4/ip4_addr.c \
	$(LWIP)/src/api/api_lib.c \
	$(LWIP)/src/api/api_msg.c \
	$(LWIP)/src/api/err.c \
	$(LWIP)/src/api/if_api.c \
	$(LWIP)/src/api/netbuf.c \
	$(LWIP)/src/api/netdb.c \
	$(LWIP)/src/api/netifapi.c \
	$(LWIP)/src/api/sockets.c \
	$(LWIP)/src/api/tcpip.c \
	$(LWIP)/src/netif/ethernet.c \
	$(LWIP)/src/apps/lwiperf/lwiperf.c

# The contrib ping rides its own compile: the console anchors (one line per
# echo reply) exist only under the stack's debug macro, which is switched on
# for this one translation unit and stays off everywhere else.
LWIP_PING_SRCS := $(LWIP)/contrib/apps/ping/ping.c

# --- GMAC (DWC EQOS) domain --------------------------------------------------
#
# The board's own Ethernet drivers (MAC core, RK3568 SoC glue, PHY) live beside
# the console driver; their includes are the board directories already on
# INCLUDES, so no extra -I is needed.
EQOS_SRCS := \
	$(ROOT)/board/rk3568-itx/drivers/dwc_eqos.c \
	$(ROOT)/board/rk3568-itx/drivers/eqos_rk3568.c \
	$(ROOT)/board/rk3568-itx/drivers/rtl8211f.c

EQOS_INCLUDES :=

ALL_SRCS := $(BASE_SRCS) $(TX_SRCS) $(COMMON_SRCS) $(SHELL_SRCS) $(TESTS_SRCS) $(VENDORED_SRCS) $(ALLOC_SRCS) $(USB_BOARD_SRCS) $(USB_STACK_SRCS) $(LWIP_PORT_SRCS) $(LWIP_STACK_SRCS) $(LWIP_PING_SRCS) $(EQOS_SRCS)

# Object names mirror the path below ROOT: the kernel and the port share flat
# tx_*.c / tx_*.S basenames, so $(notdir) would collide.
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
	@$(CC) $(if $(filter %.S,$(1)),$(ASM_CFLAGS),$(if $(filter $(1),$(VENDORED_WARNED_SRCS)),$(VENDORED_CFLAGS),$(CFLAGS))) $(2) $(INCLUDES) $(if $(filter %.S,$(1)),,-MMD -MP) -c $$< -o $$@
endef

$(foreach s,$(PRINTF_SRCS),$(eval $(call COMPILE_RULE,$(s),$(PRINTF_CFLAGS))))
$(foreach s,$(BASE_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(TX_SRCS),$(eval $(call COMPILE_RULE,$(s),$(TX_CFLAGS))))
$(foreach s,$(COMMON_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(SHELL_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(filter-out $(COMMON)/cases/lwip_smoke.c $(COMMON)/lwip_stack.c $(COMMON)/cases/gmac_dataplane.c,$(TESTS_SRCS)),$(eval $(call COMPILE_RULE,$(s),$(USB_INCLUDES))))
$(eval $(call COMPILE_RULE,$(COMMON)/cases/lwip_smoke.c,$(LWIP_INCLUDES)))
$(eval $(call COMPILE_RULE,$(COMMON)/lwip_stack.c,$(LWIP_INCLUDES)))
$(eval $(call COMPILE_RULE,$(COMMON)/cases/gmac_dataplane.c,$(LWIP_INCLUDES)))
$(foreach s,$(VENDORED_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(ALLOC_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(USB_BOARD_SRCS),$(eval $(call COMPILE_RULE,$(s),$(USB_INCLUDES))))
$(foreach s,$(USB_STACK_SRCS),$(eval $(call COMPILE_RULE,$(s),$(USB_INCLUDES))))
$(foreach s,$(LWIP_PORT_SRCS),$(eval $(call COMPILE_RULE,$(s),$(LWIP_INCLUDES))))
$(foreach s,$(LWIP_STACK_SRCS),$(eval $(call COMPILE_RULE,$(s),$(LWIP_INCLUDES))))
$(eval $(call COMPILE_RULE,$(LWIP)/contrib/apps/ping/ping.c,$(LWIP_INCLUDES) -DLWIP_DEBUG))
$(foreach s,$(EQOS_SRCS),$(eval $(call COMPILE_RULE,$(s),$(EQOS_INCLUDES))))

# The stamp carries the compile flags as well as the build tag: make does not
# treat a flag change as a reason to recompile, so a lever build on top of a
# default build would otherwise ship the default binary under a new name (the
# 2026-10-15 wired round measured exactly that: COALESCE=0xa0 rebuilt nothing
# and produced the baseline's crc32).
$(TAG_STAMP): FORCE
	@mkdir -p $(BUILD)
	@printf '%s' '$(BUILD_TAG) $(LEVER_CFLAGS)' | cmp -s - $@ 2>/dev/null || printf '%s' '$(BUILD_TAG) $(LEVER_CFLAGS)' > $@

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
	@for s in $(LWIP_STACK_SRCS) $(LWIP_PORT_SRCS); do \
		case "$$s" in $(LWIP)/*|$(LWIP_PORT)/*) ;; \
		*) echo "FAIL: TCP/IP source outside the vendored tree or the port layer: $$s"; exit 1 ;; \
		esac; \
	done
	@first=$$(echo "$(LWIP_INCLUDES)" | tr ' ' '\n' | sed '/^$$/d' | head -2 | tr '\n' ' '); \
	[ "$$first" = "-I$(LWIP_PORT) -I$(LWIP_PORT)/include " ] \
		|| { echo "FAIL: the port include directories do not lead the lwIP -I list (first=$$first)"; exit 1; }
	@test -f $(LWIP_PORT)/include/lwipopts.h || { echo "FAIL: lwipopts.h missing"; exit 1; }
	@test -f $(LWIP_PORT)/include/arch/cc.h || { echo "FAIL: arch/cc.h missing"; exit 1; }
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
		         board_suite_run board_case_fail board_glob_match board_select_parse \
		         tcpip_init lwip_socket lwip_accept netif_init lwip_platform_assert; do \
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
	echo "fsymtab bytes=$$fsz (expect 9*40=360), vsymtab bytes=$$vsz (expect 2*24=48), board_tests bytes=$$btz"; \
	[ $$fsz -eq 360 ] || { echo "FAIL: FSymTab is not dense"; exit 1; }; \
	[ $$vsz -eq 48 ] || { echo "FAIL: VSymTab is not dense"; exit 1; }; \
	[ $$(( btz % 16 )) -eq 0 ] || { echo "FAIL: BOARD_TESTS is not a whole number of entries"; exit 1; }; \
	n=$$(( btz / 16 )); echo "board_tests entries=$$n"; \
	[ $$n -ge 11 ] || { echo "FAIL: fewer than the eleven historical cases registered"; exit 1; }; \
	echo "PASS: command and test tables dense (no inter-entry padding)"
	@echo "== case-name reconciliation (image / suite.mk / judge) =="
	@sh $(COMMON)/check-case-list.sh $(TARGET).elf $(CURDIR)/suite.mk $(CURDIR)/judge.py $(OBJCOPY) $(READELF) $(BUILD)
	@echo "== selection vectors (host replay) =="
	@$(HOSTCC) -DBOARD_SELECT_HOSTTEST -O1 -std=c11 -Wall -Wextra -c $(COMMON)/board_select.c -o $(BUILD)/board_select_hosttest.o
	@$(HOSTCC) $(BUILD)/board_select_hosttest.o -o $(BUILD)/select-hosttest
	@$(BUILD)/select-hosttest $(COMMON)/select-vectors.tsv
	@$(SIZE) $(TARGET).elf
	@echo "build-tag: $(BUILD_TAG)"

# Header dependencies: -MMD -MP writes one .d per object, so a header
# edit rebuilds every object that included it (a stale object would
# otherwise be flashed as the current build).
-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

clean:
	rm -rf $(BUILD)

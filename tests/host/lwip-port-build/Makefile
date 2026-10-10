# Static build gates for the lwIP port layer.
#
# Self contained; run it from anywhere with:
#   make -C <repo>/tests/host/lwip-port-build
#
# The lwIP component has no host runtime in this project (its OS coupling is
# the board's CMSIS-RTOS2, and a host stand-in is deliberately not built), so
# this directory holds the build-side checks only - the same role the
# net80211 port test's stack-compile/gates targets play, minus the host link:
#
#   compile-threadx   the lwIP compile set + the port layer, aarch64 cross,
#                     CMSIS2_BACKEND_THREADX, zero warnings
#   compile-freertos  the same sources, CMSIS2_BACKEND_FREERTOS, zero
#                     warnings - the port must build for both kernels from
#                     one source set
#   symbols           the sys_arch function contract: every entry of the
#                     port's function face defined, and the port objects'
#                     undefined symbols confined to the CMSIS prefix
#   gates             the kernel-isolation gates over the port objects and
#                     sources (tools/gates/check-kernel-isolation.sh)
#
# The imported component compiles from the vendored tree as-is (byte-identical
# to its pin); the port layer under ports/lwip/ is this project's own. The
# include order is the port's declared discipline: the port's include
# directory first (lwipopts.h and arch/cc.h are supplied by shadowing), then
# the component's own include root.

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT   := $(abspath $(MAKEFILE_DIR)/../../..)
LWIP   := $(ROOT)/third-party/lwip
PORT   := $(ROOT)/ports/lwip
CMSIS  := $(ROOT)/third-party/CMSIS_6

AARCH64_CROSS_PATH ?= $(HOME)/workspace/env/src/xpack-aarch64-none-elf-gcc-13.2.1-1.1
CROSS_COMPILE ?= $(AARCH64_CROSS_PATH)/bin/aarch64-none-elf-
CC  := $(CROSS_COMPILE)gcc
NM  := $(CROSS_COMPILE)nm
CROSS32 ?= arm-none-eabi-gcc

BUILD := $(MAKEFILE_DIR)/build

# ---- source groups (the import spec's compile set) --------------------------

STACK_SRCS := \
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
    $(LWIP)/src/apps/lwiperf/lwiperf.c \
    $(LWIP)/contrib/apps/ping/ping.c

PORT_SRCS := \
    $(PORT)/sys_arch.c \
    $(PORT)/platform_hooks.c \
    $(PORT)/wlannetif.c

# ---- flags ------------------------------------------------------------------

ARCH := -mcpu=cortex-a55 -mgeneral-regs-only -mstrict-align -mno-outline-atomics
COMMON := -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -g3 -O2 \
    -ffunction-sections -fdata-sections
# The port-supplied headers shadow the stack's own include root: the
# configuration header and the compiler abstraction are reached by these
# names only through this order. The repository root is on the path because
# the abstraction header reaches this project's own arch headers, and the
# presentation layer reaches the net80211 port's contract header.
INC := -I$(PORT)/include -I$(LWIP)/src/include -I$(LWIP)/contrib/apps/ping \
    -I$(ROOT)
PORT_INC := $(INC) -I$(ROOT)/components/net80211 -I$(ROOT)/os/cmsis2 \
    -I$(ROOT)/os/baremetal -I$(ROOT)/arch/aarch64 -I$(CMSIS)/CMSIS/RTOS2/Include

# The imported bytes are not ours to keep warning-clean beyond the level the
# upstream project itself takes; the port layer gets the full set. Both
# groups are built with -Werror, so "zero warnings" is mechanical.
STACK_CFLAGS := $(COMMON) -Wall -Werror
PORT_CFLAGS := $(COMMON) -Wall -Wextra -Werror

BACKEND_THX := -DCMSIS2_BACKEND_THREADX
BACKEND_FR := -DCMSIS2_BACKEND_FREERTOS

# ---- object naming (path below the repo root, folded) -----------------------

define OBJ
$(BUILD)/$(1)/$(subst /,_,$(patsubst $(ROOT)/%,%,$(2))).o
endef

STACK_THX := $(foreach s,$(STACK_SRCS),$(call OBJ,thx,$(s)))
PORT_THX  := $(foreach s,$(PORT_SRCS),$(call OBJ,thx,$(s)))
STACK_FR  := $(foreach s,$(STACK_SRCS),$(call OBJ,fr,$(s)))
PORT_FR   := $(foreach s,$(PORT_SRCS),$(call OBJ,fr,$(s)))

.PHONY: all compile-threadx compile-freertos symbols gates clean

all: compile-threadx compile-freertos symbols gates

# ---- rules ------------------------------------------------------------------

define STACK_RULE
$(call OBJ,$(1),$(2)): $(2) | $(BUILD)/$(1)
	$$(CC) $$(ARCH) $$(STACK_CFLAGS) $(3) $$(INC)  -MMD -MP -c $$< -o $$@
endef

define PORT_RULE
$(call OBJ,$(1),$(2)): $(2) | $(BUILD)/$(1)
	$$(CC) $$(ARCH) $$(PORT_CFLAGS) $(3) $$(PORT_INC)  -MMD -MP -c $$< -o $$@
endef

$(foreach s,$(STACK_SRCS),$(eval $(call STACK_RULE,thx,$(s),$(BACKEND_THX))))
$(foreach s,$(PORT_SRCS),$(eval $(call PORT_RULE,thx,$(s),$(BACKEND_THX))))
$(foreach s,$(STACK_SRCS),$(eval $(call STACK_RULE,fr,$(s),$(BACKEND_FR))))
$(foreach s,$(PORT_SRCS),$(eval $(call PORT_RULE,fr,$(s),$(BACKEND_FR))))

compile-threadx: $(STACK_THX) $(PORT_THX)
	@echo "compile-threadx: $(words $(STACK_THX)) stack + $(words $(PORT_THX)) port translation units, aarch64, THREADX backend, zero warnings"

compile-freertos: $(STACK_FR) $(PORT_FR)
	@echo "compile-freertos: $(words $(STACK_FR)) stack + $(words $(PORT_FR)) port translation units, aarch64, FREERTOS backend, zero warnings"

# The sys_arch function face: the entries of the port's contract table that
# are functions (the validity probes are macros in arch/sys_arch.h and are
# covered by the compile). Every one must be a defined text symbol, and the
# port objects' undefined symbols must be the CMSIS service prefix alone.
SYS_ARCH_ENTRIES := \
    sys_mutex_new sys_mutex_lock sys_mutex_unlock sys_mutex_free \
    sys_sem_new sys_sem_signal sys_arch_sem_wait sys_sem_free \
    sys_mbox_new sys_mbox_post sys_mbox_trypost sys_mbox_trypost_fromisr \
    sys_arch_mbox_fetch sys_arch_mbox_tryfetch sys_mbox_free \
    sys_thread_new \
    sys_init sys_jiffies sys_now sys_arch_msleep \
    sys_arch_protect sys_arch_unprotect

# The contract's macro layer: the six valid/set_invalid probe pairs live in
# the header by lwIP's own design ("This may also be a define, in which case
# the function is not prototyped" - sys.h), so they are checked there, and
# the stack's compilation above exercises every one.
SYS_ARCH_MACROS := \
    sys_mutex_valid sys_mutex_set_invalid \
    sys_sem_valid sys_sem_set_invalid \
    sys_mbox_valid sys_mbox_set_invalid

symbols: $(PORT_THX)
	@set -e; \
	for s in $(SYS_ARCH_ENTRIES); do \
	    $(NM) $(PORT_THX) | awk '{print $$2 " " $$3}' | grep -qx "T $$s" \
	        || { echo "FAIL: $$s is not a defined text symbol in the port objects"; exit 1; }; \
	done; \
	for m in $(SYS_ARCH_MACROS); do \
	    grep -qE "^#define[[:space:]]+$$m\(" $(PORT)/include/arch/sys_arch.h \
	        || { echo "FAIL: the contract macro $$m is not defined in arch/sys_arch.h"; exit 1; }; \
	done; \
	echo "symbols: all $(words $(SYS_ARCH_ENTRIES)) function entries defined (T) and all $(words $(SYS_ARCH_MACROS)) macro probes defined"; \
	undef=$$($(NM) -u $(PORT_THX) | sed -n 's/^ \{1,\}U //p' | sort -u); \
	foreign=$$(printf '%s\n' "$$undef" | grep -Ev '^(os[A-Z][A-Za-z0-9_]*|sys_[a-z_]+|board_log[A-Za-z_]*|lwip_[a-z_]+|pbuf_[a-z_]+|netif_[a-z_]+|etharp_output|tcpip_input|dhcp_[a-z_]+|wlan_(bridge|port)_[a-z_]+|vsnprintf|memcpy|memset|memmove|memchr|memcmp|strcmp|strncmp|strlen|strnlen|strncpy|strchr|strrchr|strstr|atoi|strtol|strtoul)$$' || true); \
	if [ -n "$$foreign" ]; then \
	    echo "FAIL: unexpected undefined symbols in the port objects:"; \
	    printf '%s\n' "$$foreign"; exit 1; \
	fi; \
	echo "symbols: port-object undefined symbols confined to the CMSIS service prefix and the platform seam"

# The isolation gates over this port: same script, same canary discipline as
# the other port trees. The stack objects come from the THREADX pass (the
# object bytes are backend-independent except for the define, and the port
# pass covers the other backend).
gates: $(STACK_THX) $(PORT_THX)
	PATH=$(dir $(CROSS_COMPILE)):$(dir $(shell command -v $(CROSS32) 2>/dev/null || echo /usr/bin/)):$$PATH \
	sh $(ROOT)/tools/gates/check-kernel-isolation.sh \
	    --stack-objs "$(BUILD)/thx" --port-objs "$(BUILD)/thx" \
	    --sources "$(PORT) $(MAKEFILE_DIR)" --lwip-port --canary

$(BUILD)/thx $(BUILD)/fr: | $(BUILD)
	mkdir -p $@

$(BUILD):
	mkdir -p $@

# Header dependencies: -MMD -MP writes one .d per object, so a
# header edit rebuilds every object that included it (a stale object
# here once linked two disagreeing struct layouts into one binary).
-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

clean:
	rm -rf $(BUILD)

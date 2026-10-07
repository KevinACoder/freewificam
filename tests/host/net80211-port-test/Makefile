# Host contract: the net80211 port layer on the CMSIS-RTOS2 API.
#
# Self contained; run it from anywhere with:
#   make -C <repo>/tests/host/net80211-port-test
#
# Targets:
#   stack-compile   the 17 imported stack translation units, compile only, zero warnings
#   link-threadx    stack + port layer + smoke + stub driver on the ThreadX host
#                   runtime: links and RUNS
#   link-freertos   the same set on the FreeRTOS host runtime: links only (the run
#                   is blocked by the upstream 64-bit handle truncation, see the
#                   run log; the link criterion stands on its own)
#   gates           the three kernel-isolation gates (tools/gates/check-kernel-isolation.sh)
#   all             all of the above
#
# The imported component is consumed as-is (byte-identical to its pin); the port
# layer under ports/net80211/ is this project's own. The include order below is the
# port's declared discipline: the port's machinery headers first, then the
# component's vocabulary headers, then the component root, then the toolchain.

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT         := $(abspath $(MAKEFILE_DIR)/../../..)

NET   := $(ROOT)/components/net80211
PORT  := $(ROOT)/ports/net80211
TX    := $(ROOT)/third-party/threadx
FR    := $(ROOT)/third-party/FreeRTOS-Kernel
CMM   := $(ROOT)/third-party/CMSIS_6
ADAPT := $(ROOT)/third-party/CMSIS-FreeRTOS

BUILD_DIR := $(MAKEFILE_DIR)/build
CC        ?= cc
CROSS     ?= arm-none-eabi-gcc
CROSS_PATH ?= $(HOME)/workspace/env/src/xpack-arm-none-eabi-gcc-13.2.1-1.1/bin

# ---- BSD-world flags (stack + port layer + smoke) ---------------------------
#
# -include prelude.h carries the NetBSD-world switches and the host-coexistence
# step; the two -Wno relaxations are the ones the component's own board build
# applies to the imported code (unreferenced registration helpers and char-pointer
# sign casts) and are the only relaxations here.
BSD_INCLUDES := -I$(PORT)/compat -I$(NET)/compat/netbsd -I$(NET)
BSD_CFLAGS := -std=gnu11 -Wall -Wno-unused-function -Wno-pointer-sign \
    -D_KERNEL -include $(PORT)/compat/prelude.h $(BSD_INCLUDES)

# CMSIS-RTOS2 API header (the API only; the implementation is the linked backend).
CMSIS_INC := -I$(CMM)/CMSIS/RTOS2/Include -I$(ROOT)

# ---- source groups ----------------------------------------------------------

STACK_SRCS := \
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

PORT_SRCS := \
    $(PORT)/osal/cmsis2/osal_memory.c \
    $(PORT)/osal/cmsis2/osal_cmsis2.c \
    $(PORT)/net/mbuf.c \
    $(PORT)/net/ifnet.c

SMOKE_SRCS := $(MAKEFILE_DIR)/main.c $(MAKEFILE_DIR)/seam_stub.c

TX_RUNTIME_SRCS := \
    $(ROOT)/os/threadx/tx_glue.c \
    $(ROOT)/os/threadx/heap.c \
    $(ROOT)/os/threadx/cmsis2_threadx.c \
    $(ROOT)/os/threadx/cmsis2_host.c \
    $(wildcard $(TX)/ports/linux/gnu/src/*.c) \
    $(wildcard $(TX)/common/src/*.c)

FR_RUNTIME_SRCS := \
    $(ROOT)/os/freertos/freertos_hooks.c \
    $(ROOT)/os/freertos/cmsis2_host.c \
    $(FR)/croutine.c $(FR)/event_groups.c $(FR)/list.c $(FR)/queue.c \
    $(FR)/stream_buffer.c $(FR)/tasks.c $(FR)/timers.c \
    $(FR)/portable/MemMang/heap_4.c \
    $(FR)/portable/ThirdParty/GCC/Posix/port.c \
    $(FR)/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c \
    $(ADAPT)/CMSIS/RTOS2/FreeRTOS/Source/cmsis_os2.c

TX_CFLAGS := -ggdb3 -O0 -Wall -D_GNU_SOURCE -DTX_LINUX_MULTI_CORE \
    -DTX_INCLUDE_USER_DEFINE_FILE -DCMSIS2_BACKEND_THREADX \
    -I$(ROOT) -I$(ROOT)/os/threadx -I$(ROOT)/os/cmsis2 \
    -I$(ROOT)/os/cmsis2/host-compat -I$(TX)/common/inc -I$(TX)/ports/linux/gnu/inc \
    -I$(CMM)/CMSIS/RTOS2/Include

FR_CFLAGS := -ggdb3 -O0 -Wall -ffunction-sections -fdata-sections \
    -DCMSIS2_BACKEND_FREERTOS \
    -DCMSIS_device_header='"cmsis_host_device.h"' \
    -DFREERTOS_HOOKS_TAG='"net80211-port"' \
    -I$(ROOT) -I$(ROOT)/os/freertos -I$(ROOT)/os/cmsis2 \
    -I$(ROOT)/os/cmsis2/host-compat \
    -I$(FR)/include -I$(FR)/portable/ThirdParty/GCC/Posix \
    -I$(FR)/portable/ThirdParty/GCC/Posix/utils \
    -I$(CMM)/CMSIS/RTOS2/Include \
    -I$(ADAPT)/CMSIS/RTOS2/FreeRTOS/Include

TX_LDFLAGS := -pthread -lrt
# Unreferenced sections are dropped so the adapter's device-tick path (which
# needs the board's OS_Tick_* functions) stays out of the image instead of being
# stubbed; the same switch the FreeRTOS smoke of this repository uses.
FR_LDFLAGS := -pthread -Wl,--gc-sections

# ---- object naming (path below the repo root, folded) -----------------------

define OBJ
$(BUILD_DIR)/$(1)/$(subst /,_,$(patsubst $(ROOT)/%,%,$(2))).o
endef

STACK_OBJS := $(foreach s,$(STACK_SRCS),$(call OBJ,stack,$(s)))
PORT_OBJS  := $(foreach s,$(PORT_SRCS),$(call OBJ,port,$(s)))
TX_OBJS    := $(foreach s,$(SMOKE_SRCS),$(call OBJ,tx,$(s))) \
              $(foreach s,$(TX_RUNTIME_SRCS),$(call OBJ,tx,$(s)))
FR_OBJS    := $(foreach s,$(SMOKE_SRCS),$(call OBJ,fr,$(s))) \
              $(foreach s,$(FR_RUNTIME_SRCS),$(call OBJ,fr,$(s)))

TX_BIN := $(BUILD_DIR)/net80211-port-threadx
FR_BIN := $(BUILD_DIR)/net80211-port-freertos

.PHONY: all stack-compile headers link-threadx link-freertos gates clean

all: stack-compile headers link-threadx link-freertos gates

# ---- rules ------------------------------------------------------------------

define STACK_RULE
$(call OBJ,stack,$(1)): $(1) | $(BUILD_DIR)/stack
	$$(CC) $$(BSD_CFLAGS) -c $$< -o $$@
endef

define PORT_RULE
$(call OBJ,port,$(1)): $(1) | $(BUILD_DIR)/port
	$$(CC) $$(BSD_CFLAGS) $$(CMSIS_INC) -c $$< -o $$@
endef

define TX_RULE
$(call OBJ,tx,$(1)): $(1) | $(BUILD_DIR)/tx
	$$(CC) $$(TX_CFLAGS) -c $$< -o $$@
endef

define FR_RULE
$(call OBJ,fr,$(1)): $(1) | $(BUILD_DIR)/fr
	$$(CC) $$(FR_CFLAGS) -c $$< -o $$@
endef

$(foreach s,$(STACK_SRCS),$(eval $(call STACK_RULE,$(s))))
$(foreach s,$(PORT_SRCS),$(eval $(call PORT_RULE,$(s))))
$(foreach s,$(TX_RUNTIME_SRCS),$(eval $(call TX_RULE,$(s))))
$(foreach s,$(FR_RUNTIME_SRCS),$(eval $(call FR_RULE,$(s))))

# The smoke sources are BSD-world (they build mbufs and drive the shells) while the
# runtime keeps its own flags: the compat include order must not shadow the host
# headers the kernel port needs. The backend define selects the CMSIS-RTOS2 backend
# at link time, so the smoke is compiled once per backend.
$(call OBJ,tx,$(MAKEFILE_DIR)/main.c): $(MAKEFILE_DIR)/main.c | $(BUILD_DIR)/tx
	$(CC) $(TX_CFLAGS) $(BSD_CFLAGS) $(CMSIS_INC) -I$(PORT) -c $< -o $@

$(call OBJ,tx,$(MAKEFILE_DIR)/seam_stub.c): $(MAKEFILE_DIR)/seam_stub.c | $(BUILD_DIR)/tx
	$(CC) $(TX_CFLAGS) $(BSD_CFLAGS) -I$(PORT) -c $< -o $@

$(call OBJ,fr,$(MAKEFILE_DIR)/main.c): $(MAKEFILE_DIR)/main.c | $(BUILD_DIR)/fr
	$(CC) $(FR_CFLAGS) $(BSD_CFLAGS) $(CMSIS_INC) -I$(PORT) -c $< -o $@

$(call OBJ,fr,$(MAKEFILE_DIR)/seam_stub.c): $(MAKEFILE_DIR)/seam_stub.c | $(BUILD_DIR)/fr
	$(CC) $(FR_CFLAGS) $(BSD_CFLAGS) -I$(PORT) -c $< -o $@

stack-compile: $(STACK_OBJS)
	@echo "stack-compile: $(words $(STACK_OBJS)) translation units compiled (-c only)"

# The include discipline, mechanically: across the whole stack set, every name the
# contract cares about must resolve where the port's search order says it should -
# the NetBSD shape names to the component's vocabulary headers and the machinery
# names to the port's own. The cdefs shim is checked with a probe instead of the
# -H table: the compiler does not list a header reached by include_next from a
# force-included file (a -H quirk), so a translation unit that asserts the shim's
# guard (and the host cdefs behind it) is the honest check.
headers: | $(BUILD_DIR)
	@: > $(BUILD_DIR)/headers.raw
	@for s in $(STACK_SRCS); do \
	    $(CC) $(BSD_CFLAGS) -H -fsyntax-only $$s 2>> $(BUILD_DIR)/headers.raw || exit 1; \
	done
	@grep -E '^\.+ ' $(BUILD_DIR)/headers.raw | sed -e 's/^\.* //' | sort -u > $(BUILD_DIR)/headers.txt
	@for n in net/if.h net/route.h sys/param.h; do \
	    grep -q "components/net80211/compat/netbsd/$$n$$" $(BUILD_DIR)/headers.txt || \
	    { echo "headers: FAIL $$n must come from the component vocabulary"; exit 1; }; \
	done
	@for n in sys/mutex.h sys/intr.h sys/atomic.h sys/once.h sys/systm.h \
	    sys/kernel.h sys/cpu.h sys/proc.h sys/errno.h; do \
	    grep -q "ports/net80211/compat/$$n$$" $(BUILD_DIR)/headers.txt || \
	    { echo "headers: FAIL $$n must come from the port"; exit 1; }; \
	done
	@printf '#include <sys/cdefs.h>\n#ifndef _WLAN_SYS_CDEFS_H_\n#error the port'"'"'s cdefs shim is not the one in reach\n#endif\n#ifndef __THROW\n#error the host cdefs did not load through include_next\n#endif\n' > $(BUILD_DIR)/cdefs-probe.c
	@$(CC) $(BSD_CFLAGS) -fsyntax-only $(BUILD_DIR)/cdefs-probe.c || \
	    { echo "headers: FAIL the cdefs shim probe"; exit 1; }
	@echo "headers: shape names <- component vocabulary, machinery <- port, cdefs shim + host cdefs <- probe ($$(wc -l < $(BUILD_DIR)/headers.txt) headers resolved)"

link-threadx: $(STACK_OBJS) $(PORT_OBJS) $(TX_OBJS)
	$(CC) $^ $(TX_LDFLAGS) -o $(TX_BIN)
	$(TX_BIN)

link-freertos: $(STACK_OBJS) $(PORT_OBJS) $(FR_OBJS)
	$(CC) $^ $(FR_LDFLAGS) -o $(FR_BIN)
	@echo "link-freertos: linked (run blocked by the upstream handle-truncation issue)"

gates: $(STACK_OBJS) $(PORT_OBJS)
	PATH=$(CROSS_PATH):$$PATH sh $(ROOT)/tools/gates/check-kernel-isolation.sh \
	    --stack-objs "$(BUILD_DIR)/stack" --port-objs "$(BUILD_DIR)/port" \
	    --sources "$(PORT) $(MAKEFILE_DIR)" --canary

# The build root is its own target: the header gate and the compile gates have
# parallel prerequisites, so a cold parallel run must not depend on a sibling
# target having created the directory first.
$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/stack $(BUILD_DIR)/port $(BUILD_DIR)/tx $(BUILD_DIR)/fr: | $(BUILD_DIR)
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

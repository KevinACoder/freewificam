# Host mock suite: the vwifi virtual driver and the net80211 protocol paths.
#
# Self contained; run it from anywhere with:
#   make -C <repo>/tests/host/net80211-frame-test
#
# Targets:
#   stack-compile   the 17 imported stack translation units, compile only
#   smoke           the suite binary for the ThreadX host runtime
#   run             build and run the suite (anchor lines on stdout)
#   link-freertos   the same set on the FreeRTOS host runtime: links only (the
#                   run is blocked upstream, the link criterion stands on its own)
#   all             all of the above
#
# The suite drives the imported stack through scan -> auth -> assoc -> data with
# a fake driver (if_start capture plus ieee80211_input injection) and a scripted
# AP responder. Expectations come from the 802.11 frame formats and the vector
# tables of this suite, never from the stack under test.
#
# The imported component is consumed as-is (byte-identical to its pin); the port
# layer under ports/net80211/ is this project's own. The include order is the
# port's declared discipline: port machinery headers first, then the component's
# vocabulary headers, then the component root, then the toolchain.

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

# ---- BSD-world flags (stack + port layer + suite harness) -------------------

BSD_INCLUDES := -I$(PORT)/compat -I$(NET)/compat/netbsd -I$(NET)
BSD_CFLAGS := -std=gnu11 -g -Wall -Wno-unused-function -Wno-pointer-sign \
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

SUITE_SRCS := \
    $(MAKEFILE_DIR)/main.c \
    $(MAKEFILE_DIR)/vwifi.c \
    $(MAKEFILE_DIR)/skbuff_seam.c

# The library's own two host tests, re-anchored onto this port (upstream/).
UPSTREAM_COMMON_SRCS := $(MAKEFILE_DIR)/upstream/host_stubs.c \
    $(PORT)/net/mbuf.c $(PORT)/osal/cmsis2/osal_memory.c
UPSTREAM_MBUF_SRCS := $(MAKEFILE_DIR)/upstream/mbuf_trim.c \
    $(UPSTREAM_COMMON_SRCS)
UPSTREAM_CCM_SRCS := $(MAKEFILE_DIR)/upstream/ccm.c \
    $(UPSTREAM_COMMON_SRCS) \
    $(NET)/crypto/aes/aes_bear.c $(NET)/crypto/aes/aes_ct.c \
    $(NET)/crypto/aes/aes_ct_enc.c $(NET)/crypto/aes/aes_ct_dec.c \
    $(NET)/crypto/aes/aes_ccm.c $(NET)/crypto/aes/aes_ccm_mbuf.c \
    $(NET)/port/aes_impl_compat.c

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
    -DFREERTOS_HOOKS_TAG='"net80211-frame-test"' \
    -I$(ROOT) -I$(ROOT)/os/freertos -I$(ROOT)/os/cmsis2 \
    -I$(ROOT)/os/cmsis2/host-compat \
    -I$(FR)/include -I$(FR)/portable/ThirdParty/GCC/Posix \
    -I$(FR)/portable/ThirdParty/GCC/Posix/utils \
    -I$(CMM)/CMSIS/RTOS2/Include \
    -I$(ADAPT)/CMSIS/RTOS2/FreeRTOS/Include

TX_LDFLAGS := -pthread -lrt
FR_LDFLAGS := -pthread -Wl,--gc-sections

# ---- object naming (path below the repo root, folded) -----------------------

define OBJ
$(BUILD_DIR)/$(1)/$(subst /,_,$(patsubst $(ROOT)/%,%,$(2))).o
endef

STACK_OBJS := $(foreach s,$(STACK_SRCS),$(call OBJ,stack,$(s)))
PORT_OBJS  := $(foreach s,$(PORT_SRCS),$(call OBJ,port,$(s)))
UPSTREAM_ALL_SRCS := $(sort $(UPSTREAM_MBUF_SRCS) $(UPSTREAM_CCM_SRCS))
UP_MBUF_OBJS := $(foreach s,$(UPSTREAM_MBUF_SRCS),$(call OBJ,up,$(s)))
UP_CCM_OBJS  := $(foreach s,$(UPSTREAM_CCM_SRCS),$(call OBJ,up,$(s)))

TX_OBJS    := $(foreach s,$(SUITE_SRCS),$(call OBJ,tx,$(s))) \
              $(foreach s,$(TX_RUNTIME_SRCS),$(call OBJ,tx,$(s)))
FR_OBJS    := $(foreach s,$(SUITE_SRCS),$(call OBJ,fr,$(s))) \
              $(foreach s,$(FR_RUNTIME_SRCS),$(call OBJ,fr,$(s)))

TX_BIN := $(BUILD_DIR)/net80211-frame-test
FR_BIN := $(BUILD_DIR)/net80211-frame-test-freertos
UP_MBUF_BIN := $(BUILD_DIR)/upstream-mbuf-trim
UP_CCM_BIN  := $(BUILD_DIR)/upstream-ccm

.PHONY: all stack-compile smoke run link-freertos upstream clean

all: stack-compile smoke run link-freertos upstream

# ---- rules ------------------------------------------------------------------

define STACK_RULE
$(call OBJ,stack,$(1)): $(1) | $(BUILD_DIR)/stack
	$$(CC) $$(BSD_CFLAGS) -c $$< -o $$@
endef

define PORT_RULE
$(call OBJ,port,$(1)): $(1) | $(BUILD_DIR)/port
	$$(CC) $$(BSD_CFLAGS) $$(CMSIS_INC) -c $$< -o $$@
endef

define SUITE_TX_RULE
$(call OBJ,tx,$(1)): $(1) | $(BUILD_DIR)/tx
	$$(CC) $$(TX_CFLAGS) $$(BSD_CFLAGS) $$(CMSIS_INC) -I$$(PORT) -I$$(ROOT)/components -I$$(MAKEFILE_DIR) -c $$< -o $$@
endef

define SUITE_FR_RULE
$(call OBJ,fr,$(1)): $(1) | $(BUILD_DIR)/fr
	$$(CC) $$(FR_CFLAGS) $$(BSD_CFLAGS) $$(CMSIS_INC) -I$$(PORT) -I$$(ROOT)/components -I$$(MAKEFILE_DIR) -c $$< -o $$@
endef

define TX_RUNTIME_RULE
$(call OBJ,tx,$(1)): $(1) | $(BUILD_DIR)/tx
	$$(CC) $$(TX_CFLAGS) -c $$< -o $$@
endef

define FR_RUNTIME_RULE
$(call OBJ,fr,$(1)): $(1) | $(BUILD_DIR)/fr
	$$(CC) $$(FR_CFLAGS) -c $$< -o $$@
endef

$(foreach s,$(STACK_SRCS),$(eval $(call STACK_RULE,$(s))))
$(foreach s,$(PORT_SRCS),$(eval $(call PORT_RULE,$(s))))
$(foreach s,$(SUITE_SRCS),$(eval $(call SUITE_TX_RULE,$(s))))
$(foreach s,$(SUITE_SRCS),$(eval $(call SUITE_FR_RULE,$(s))))
$(foreach s,$(TX_RUNTIME_SRCS),$(eval $(call TX_RUNTIME_RULE,$(s))))
$(foreach s,$(FR_RUNTIME_SRCS),$(eval $(call FR_RUNTIME_RULE,$(s))))

stack-compile: $(STACK_OBJS)
	@echo "stack-compile: $(words $(STACK_OBJS)) translation units compiled (-c only)"

smoke: $(TX_BIN)

$(TX_BIN): $(STACK_OBJS) $(PORT_OBJS) $(TX_OBJS)
	$(CC) $^ $(TX_LDFLAGS) -o $@

run: $(TX_BIN)
	$(TX_BIN)

link-freertos: $(FR_BIN)

$(FR_BIN): $(STACK_OBJS) $(PORT_OBJS) $(FR_OBJS)
	$(CC) $^ $(FR_LDFLAGS) -o $@
	@echo "link-freertos: linked (run blocked upstream; the link criterion stands)"

# The library's test sources are byte-identical to the pin and take their libc
# declarations from the shims they used to force-include; here that is just
# <string.h>, force-included so the sources stay untouched.
define UP_RULE
$(call OBJ,up,$(1)): $(1) | $(BUILD_DIR)/up
	$$(CC) $$(BSD_CFLAGS) -include string.h -c $$< -o $$@
endef

$(foreach s,$(UPSTREAM_ALL_SRCS),$(eval $(call UP_RULE,$(s))))

upstream: $(UP_MBUF_BIN) $(UP_CCM_BIN)
	$(UP_MBUF_BIN)
	$(UP_CCM_BIN)

$(UP_MBUF_BIN): $(UP_MBUF_OBJS)
	$(CC) $^ -o $@

$(UP_CCM_BIN): $(UP_CCM_OBJS)
	$(CC) $^ -o $@

$(BUILD_DIR)/stack $(BUILD_DIR)/port $(BUILD_DIR)/tx $(BUILD_DIR)/fr $(BUILD_DIR)/up:
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

# SPDX-License-Identifier: BSD-3-Clause
#
# @file    Makefile
# @brief   Host build for the cfg80211 substitute suite: the substitute body, the
#          net-device registry it looks its interface up in and the skbuff pool,
#          linked against the real linuxkpi core on the FreeRTOS host runtime.
# @design  host-suite build recipe, mirroring the sibling body suite
#          (tests/host/linuxkpi-body-test/Makefile:1-136): the same runtime source
#          list, include set and flags, plus the two bodies this seam needs and
#          the host platform hooks the pool declares.
# @date    12.10.2026
#
# Host suite: the cfg80211 substitute linked and driven through its downward seam.
#
# Self contained; build it from anywhere with:
#   make -C <repo>/tests/host/linuxkpi-cfg80211-test
#
# One binary, linuxkpi-cfg80211-test: the cfg80211 substitute body
# (linuxkpi_cfg80211.c), the net-device registry it looks an interface up in
# (linuxkpi_netdev.c) and the skbuff pool its vendor-reply surface allocates from,
# linked against the real linuxkpi core on the FreeRTOS host runtime, and driven
# by a stand-in driver whose ops table records what each op was called with.
#
# The pool body's platform hooks (lkpi_spl_*) have host bodies in the suite's
# main.c, so the pool is compiled with LKPI_SKB_POOL_NO_TARGET_HOOKS - the same
# switch the sibling linuxkpi-body-test build uses for the same reason.
#
# Targets:
#   all   the binary (default)
#   run   build, then run it
#   clean remove the build directory

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT         := $(abspath $(MAKEFILE_DIR)/../../..)

KERNEL  := $(ROOT)/third-party/FreeRTOS-Kernel
CMSIS   := $(ROOT)/third-party/CMSIS_6
ADAPTER := $(shell sh $(ROOT)/tools/patches/replay.sh CMSIS-FreeRTOS)
ifeq ($(ADAPTER),)
$(error tools/patches/replay.sh returned no source root for CMSIS-FreeRTOS)
endif

BUILD_DIR := $(MAKEFILE_DIR)/build
BIN       := $(BUILD_DIR)/linuxkpi-cfg80211-test

CFLAGS := -ggdb3 -O0 -Wall -Wextra -ffunction-sections -fdata-sections \
          -DCMSIS2_BACKEND_FREERTOS \
          -DCMSIS_device_header='"cmsis_host_device.h"' \
          -DFREERTOS_HOOKS_TAG='"lkpi-cfg80211"' \
          -I$(ROOT) \
          -I$(ROOT)/components/ulog/src \
          -I$(ROOT)/os/freertos \
          -I$(ROOT)/os/cmsis2 \
          -I$(ROOT)/os/cmsis2/host-compat \
          -I$(ROOT)/components/linuxkpi \
          -I$(ROOT)/components/linuxkpi/include \
          -I$(KERNEL)/include \
          -I$(KERNEL)/portable/ThirdParty/GCC/Posix \
          -I$(KERNEL)/portable/ThirdParty/GCC/Posix/utils \
          -I$(CMSIS)/CMSIS/RTOS2/Include \
          -I$(MAKEFILE_DIR) \
          -I$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Include

LDFLAGS := -pthread -Wl,--gc-sections

# The bodies under test: the cfg80211 substitute, the net-device registry it
# looks its interface up in, and the real linuxkpi core both stand on.
BODY_SRCS := \
    $(ROOT)/components/linuxkpi/linuxkpi_core.c \
    $(ROOT)/components/linuxkpi/linuxkpi_cfg80211.c \
    $(ROOT)/components/linuxkpi/linuxkpi_netdev.c

# The vendor-reply surface of the substitute allocates skbs, so the real pool is
# linked and its host platform hooks are supplied by main.c.
POOL_SRC := $(ROOT)/components/linuxkpi/skbuff/skbuff_pool.c

APP_SRCS := $(MAKEFILE_DIR)/main.c

RUNTIME_SRCS := \
    $(ROOT)/os/freertos/freertos_hooks.c \
    $(ROOT)/os/freertos/cmsis2_host.c \
    $(ROOT)/os/freertos/cmsis2_ext.c \
    $(KERNEL)/croutine.c \
    $(KERNEL)/event_groups.c \
    $(KERNEL)/list.c \
    $(KERNEL)/queue.c \
    $(KERNEL)/stream_buffer.c \
    $(KERNEL)/tasks.c \
    $(KERNEL)/timers.c \
    $(KERNEL)/portable/MemMang/heap_4.c \
    $(KERNEL)/portable/ThirdParty/GCC/Posix/port.c \
    $(KERNEL)/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c \
    $(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Source/cmsis_os2.c

# Object paths carry the source path (the sibling suites' shape).
define OBJ
$(BUILD_DIR)/obj_$(subst /,_,$(patsubst $(ROOT)/%,%,$(1))).o
endef

SRCS := $(BODY_SRCS) $(POOL_SRC) $(APP_SRCS) $(RUNTIME_SRCS)
OBJS := $(foreach s,$(SRCS),$(call OBJ,$(s)))

.PHONY: all run clean

all: $(BIN)

$(BUILD_DIR):
	mkdir -p $@

define COMPILE_RULE
$(call OBJ,$(1)): $(1) | $(BUILD_DIR)
	$$(CC) $$(CFLAGS) -MMD -MP -c $$< -o $$@
endef

$(foreach src,$(filter-out $(POOL_SRC),$(SRCS)),$(eval $(call COMPILE_RULE,$(src))))

# the pool body, with the host's platform hooks instead of the target's
$(call OBJ,$(POOL_SRC)): $(POOL_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -DLKPI_SKB_POOL_NO_TARGET_HOOKS -MMD -MP -c $< -o $@

$(BIN): $(OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

run: $(BIN)
	$(BIN)

# Header dependencies: -MMD -MP writes one .d per object, so a header edit
# rebuilds every object that included it.
-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)

clean:
	rm -rf $(BUILD_DIR)

# Host contract harness: the ops-shape dispatch of the mac80211 substitute.
#
# Self contained; build it from anywhere with:
#   make -C <repo>/tests/host/linuxkpi-80211-shape-test
#
# One binary, linuxkpi-80211-shape-test: the substitute body
# (components/linuxkpi/linuxkpi_80211.c) driven by scripted drivers that
# declare each of the two station/interface op families the interface allows,
# on the FreeRTOS host runtime.  What is under test is which op is called, in
# what order, and with what mask - not what a real driver does with it.
#
# ENGINE_EXTRA_SRC lets the wrapper's red control build the harness against a
# mutated copy of the substitute: the case that pins the mutation must then go
# red, which is what proves it can fail at all.
#
# The pool's platform hooks are supplied by this suite (platform_host.c), so
# the pool compiles with LKPI_SKB_POOL_NO_TARGET_HOOKS - the same split the
# bridge and txq suites use.

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT         := $(abspath $(MAKEFILE_DIR)/../../..)

KERNEL  := $(ROOT)/third-party/FreeRTOS-Kernel
CMSIS   := $(ROOT)/third-party/CMSIS_6
ADAPTER := $(shell sh $(ROOT)/tools/patches/replay.sh CMSIS-FreeRTOS)

BUILD_DIR := $(MAKEFILE_DIR)/build
BIN       := $(BUILD_DIR)/linuxkpi-80211-shape-test

ENGINE_EXTRA_SRC ?= $(ROOT)/components/linuxkpi/linuxkpi_80211.c

SANITIZE := -fsanitize=address,undefined -fno-sanitize-recover=all \
            -fno-omit-frame-pointer

CFLAGS := -ggdb3 -O1 -Wall -Wextra -ffunction-sections -fdata-sections \
          $(SANITIZE) \
          -DLKPI_SKB_POOL_NO_TARGET_HOOKS \
          -DCMSIS2_BACKEND_FREERTOS \
          -DCMSIS_device_header='"cmsis_host_device.h"' \
          -DFREERTOS_HOOKS_TAG='"lkpi-80211-shape"' \
          -I$(ROOT) \
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

LDFLAGS := -pthread -Wl,--gc-sections $(SANITIZE)

SRCS := \
    $(ROOT)/components/linuxkpi/linuxkpi_core.c \
    $(ROOT)/components/linuxkpi/skbuff/skbuff_pool.c \
    $(ROOT)/components/linuxkpi/linuxkpi_txq.c \
    $(ENGINE_EXTRA_SRC) \
    $(MAKEFILE_DIR)/main.c \
    $(ROOT)/tests/host/common/pool_platform_host.c \
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

define OBJ
$(BUILD_DIR)/obj_$(subst /,_,$(patsubst $(ROOT)/%,%,$(1))).o
endef

OBJS := $(foreach s,$(SRCS),$(call OBJ,$(s)))

.PHONY: all clean

all: $(BIN)

$(BUILD_DIR):
	mkdir -p $@

define COMPILE_RULE
$(call OBJ,$(1)): $(1) | $(BUILD_DIR)
	$$(CC) $$(CFLAGS) -MMD -MP -c $$< -o $$@
endef

$(foreach src,$(SRCS),$(eval $(call COMPILE_RULE,$(src))))

$(BIN): $(OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

# Header dependencies: -MMD -MP writes one .d per object, so a header edit
# rebuilds every object that included it.
-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)

clean:
	rm -rf $(BUILD_DIR)

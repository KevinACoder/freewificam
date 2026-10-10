# Host tests: FreeRTOS (POSIX port) behind the CMSIS-RTOS2 API.
#
# Self contained; run them from anywhere with:
#   make -C <repo>/tests/host/freertos-cmsis2-smoke
#
# Two binaries share the same kernel and adapter objects:
#   freertos-cmsis2-smoke   queue + two threads, the runtime smoke scenario
#   c9-handle-trunc         objects whose handle must live above 4 GiB (one case per run)
#
# The local glue lives in os/freertos/ (kernel configuration, hooks, lifecycle bridge) and
# os/cmsis2/ (backend facade plus the host stand-ins for the CMSIS Core headers).  The
# kernel is compiled as vendored; the CMSIS-FreeRTOS adapter is compiled through the patch
# channel (tools/patches/replay.sh), and PATCHES=off forces the pinned bytes, which is how
# the suite's red control runs (tools/mock/freertos-cmsis2-smoke/README.md).

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT         := $(abspath $(MAKEFILE_DIR)/../../..)

KERNEL  := $(ROOT)/third-party/FreeRTOS-Kernel
CMSIS   := $(ROOT)/third-party/CMSIS_6

# With patches declared for CMSIS-FreeRTOS the replay tool materialises a patched copy and
# prints it; with none it prints the vendored pin, so a repository without patches builds
# exactly as before.
ifeq ($(PATCHES),off)
ADAPTER := $(ROOT)/third-party/CMSIS-FreeRTOS
else
ADAPTER := $(shell sh $(ROOT)/tools/patches/replay.sh CMSIS-FreeRTOS)
endif
ifeq ($(ADAPTER),)
$(error tools/patches/replay.sh returned no source root for CMSIS-FreeRTOS)
endif

# The pinned mode gets its own object directory: sharing one would let a stale object from
# the other mode satisfy the build and quietly test the wrong adapter bytes.
BUILD_DIR := $(MAKEFILE_DIR)/build$(if $(filter off,$(PATCHES)),/pinned,)
SMOKE     := $(BUILD_DIR)/freertos-cmsis2-smoke
C9        := $(BUILD_DIR)/c9-handle-trunc

# os/cmsis2/host-compat carries the host stand-ins for the CMSIS Core headers; the CMSIS
# Core include directory itself is deliberately absent so no ARM-only header is reached.
INCLUDES := \
    -I$(ROOT) \
    -I$(ROOT)/os/freertos \
    -I$(ROOT)/os/cmsis2/host-compat \
    -I$(KERNEL)/include \
    -I$(KERNEL)/portable/ThirdParty/GCC/Posix \
    -I$(KERNEL)/portable/ThirdParty/GCC/Posix/utils \
    -I$(CMSIS)/CMSIS/RTOS2/Include \
    -I$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Include

CFLAGS := -ggdb3 -O0 -Wall -ffunction-sections -fdata-sections \
          -DCMSIS2_BACKEND_FREERTOS \
          -DCMSIS_device_header='"cmsis_host_device.h"' \
          -DFREERTOS_HOOKS_TAG='"os2-smoke"' \
          $(INCLUDES)

# Unreferenced sections are dropped, so the adapter's system-timer path (which needs the
# device-side OS_Tick_* functions) stays out of the image instead of being stubbed.
LDFLAGS := -pthread -Wl,--gc-sections

COMMON_SRCS := \
    $(ROOT)/os/freertos/freertos_hooks.c \
    $(ROOT)/os/freertos/cmsis2_host.c \
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

COMMON_OBJS := $(addprefix $(BUILD_DIR)/,$(notdir $(COMMON_SRCS:.c=.o)))

.PHONY: all clean

all: $(SMOKE) $(C9)

define COMPILE_RULE
$(BUILD_DIR)/$(notdir $(1:.c=.o)): $(1) | $(BUILD_DIR)
	$(CC) $(CFLAGS)  -MMD -MP -c $$< -o $$@
endef

$(foreach src,$(COMMON_SRCS),$(eval $(call COMPILE_RULE,$(src))))
$(foreach src,$(MAKEFILE_DIR)/main.c $(MAKEFILE_DIR)/c9-handle-trunc.c,$(eval $(call COMPILE_RULE,$(src))))

$(BUILD_DIR):
	mkdir -p $@

$(SMOKE): $(COMMON_OBJS) $(BUILD_DIR)/main.o
	$(CC) $^ $(LDFLAGS) -o $@

$(C9): $(COMMON_OBJS) $(BUILD_DIR)/c9-handle-trunc.o
	$(CC) $^ $(LDFLAGS) -o $@

# Header dependencies: -MMD -MP writes one .d per object, so a
# header edit rebuilds every object that included it (a stale object
# here once linked two disagreeing struct layouts into one binary).
-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)

clean:
	rm -rf $(BUILD_DIR)

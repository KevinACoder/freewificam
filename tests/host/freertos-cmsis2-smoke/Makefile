# Host smoke test: FreeRTOS (POSIX port) behind the CMSIS-RTOS2 API.
#
# Self contained; run it from anywhere with:
#   make -C <repo>/tests/host/freertos-cmsis2-smoke
#
# Everything under third-party/ is compiled as-is, without local patches. The local glue
# lives in os/freertos/ (kernel configuration, hooks, lifecycle bridge) and os/cmsis2/
# (backend facade plus the host stand-ins for the CMSIS Core headers).

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT         := $(abspath $(MAKEFILE_DIR)/../../..)

KERNEL  := $(ROOT)/third-party/FreeRTOS-Kernel
CMSIS   := $(ROOT)/third-party/CMSIS_6
ADAPTER := $(ROOT)/third-party/CMSIS-FreeRTOS

BUILD_DIR := $(MAKEFILE_DIR)/build
TARGET    := $(BUILD_DIR)/freertos-cmsis2-smoke

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

SRCS := \
    $(MAKEFILE_DIR)/main.c \
    $(ROOT)/os/freertos/freertos_hooks.c \
    $(ROOT)/os/freertos/freertos_kernel.c \
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

OBJS := $(addprefix $(BUILD_DIR)/,$(notdir $(SRCS:.c=.o)))

.PHONY: all clean

all: $(TARGET)

define COMPILE_RULE
$(BUILD_DIR)/$(notdir $(1:.c=.o)): $(1) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $$< -o $$@
endef

$(foreach src,$(SRCS),$(eval $(call COMPILE_RULE,$(src))))

$(BUILD_DIR):
	mkdir -p $@

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

clean:
	rm -rf $(BUILD_DIR)

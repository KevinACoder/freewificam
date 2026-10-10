# Host contract harness: the serializing wiphy work family.
#
# Self contained; build it from anywhere with:
#   make -C <repo>/tests/host/linuxkpi-wiphy-work-test
#
# One binary, linuxkpi-wiphy-work-test: the contract header's polling macros
# (components/linuxkpi/include/linux/iopoll.h) and the completion object
# (linuxkpi_core.h) run against the real linuxkpi body on the FreeRTOS host
# runtime, so the semantics under test are the ones the target builds.
#
# ENGINE_EXTRA_SRC and WIPHY_EXTRA_INC let the wrapper's red control build the
# harness against a mutated copy of the engine: the same cases must then go red,
# which is what proves they can fail at all.

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT         := $(abspath $(MAKEFILE_DIR)/../../..)

KERNEL  := $(ROOT)/third-party/FreeRTOS-Kernel
CMSIS   := $(ROOT)/third-party/CMSIS_6
ADAPTER := $(shell sh $(ROOT)/tools/patches/replay.sh CMSIS-FreeRTOS)

BUILD_DIR ?= $(MAKEFILE_DIR)/build
BIN       := $(BUILD_DIR)/linuxkpi-wiphy-work-test

ENGINE_EXTRA_SRC ?= $(ROOT)/components/linuxkpi/linuxkpi_wiphy_work.c
# The core body is a knob too: a red control builds against a copy whose timer
# setup is not idempotent, which is what the timer-object case must catch.
CORE_EXTRA_SRC ?= $(ROOT)/components/linuxkpi/linuxkpi_core.c
WIPHY_EXTRA_INC ?=

SANITIZE := -fsanitize=address,undefined -fno-sanitize-recover=all \
            -fno-omit-frame-pointer

CFLAGS := -ggdb3 -O1 -Wall -Wextra -ffunction-sections -fdata-sections \
          $(SANITIZE) \
          -DCMSIS2_BACKEND_FREERTOS \
          -DCMSIS_device_header='"cmsis_host_device.h"' \
          -DFREERTOS_HOOKS_TAG='"lkpi-wiphy-work"' \
          $(WIPHY_EXTRA_INC) \
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
    $(CORE_EXTRA_SRC) \
    $(ENGINE_EXTRA_SRC) \
    $(MAKEFILE_DIR)/main.c \
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

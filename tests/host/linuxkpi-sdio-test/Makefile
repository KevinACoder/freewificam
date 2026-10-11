# Host tests: the linuxkpi SDIO layer over its backend seam.
#
# Self contained; build it from anywhere with:
#   make -C <repo>/tests/host/linuxkpi-sdio-test
#   make -C <repo>/tests/host/linuxkpi-sdio-test run
#
# One binary, linuxkpi-sdio-test: the SDIO engine (components/linuxkpi/sdio)
# runs against the synthetic card and the host runtime underneath. The suite's
# wrapper (tools/mock/linuxkpi-sdio-test) drives it; the cases assert the
# interface's semantics call by call rather than a card's behaviour.
#
# The kernel and the CMSIS-RTOS2 backend objects are shared with the sibling host
# tests, so the runtime a suite exercises is the same one the rest of the mock
# tier uses. The link uses a small fragment (lkpi_sections.ld) that brackets both
# module sections, which is what the coexistence case reads.

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

BUILD_DIR := $(MAKEFILE_DIR)/build$(if $(filter off,$(PATCHES)),/pinned,)
BIN       := $(BUILD_DIR)/linuxkpi-sdio-test

# The flag set matches the sibling host test directories (the imported linuxkpi
# headers carry two known warnings, so Werror is not used).
CFLAGS := -ggdb3 -O0 -Wall -Wextra -ffunction-sections -fdata-sections \
          -DCMSIS2_BACKEND_FREERTOS \
          -DCMSIS_device_header='"cmsis_host_device.h"' \
          -DFREERTOS_HOOKS_TAG='"linuxkpi-sdio-test"' \
          -I$(ROOT) \
          -I$(ROOT)/components/ulog/src \
          -I$(ROOT)/os/freertos \
          -I$(ROOT)/os/cmsis2 \
          -I$(ROOT)/os/cmsis2/host-compat \
          -I$(ROOT)/components/linuxkpi \
          -I$(ROOT)/components/linuxkpi/include \
          -I$(ROOT)/components/linuxkpi/sdio \
          -I$(KERNEL)/include \
          -I$(KERNEL)/portable/ThirdParty/GCC/Posix \
          -I$(KERNEL)/portable/ThirdParty/GCC/Posix/utils \
          -I$(CMSIS)/CMSIS/RTOS2/Include \
          -I$(MAKEFILE_DIR) \
          -I$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Include

LDFLAGS := -pthread -Wl,--gc-sections \
           -Wl,-T,$(MAKEFILE_DIR)/lkpi_sections.ld

# The engine under test, the seam's own dispatchers, and the real service bodies
# the engine rides on.
ENGINE_SRCS := \
    $(ROOT)/components/linuxkpi/sdio/linuxkpi_sdio.c \
    $(ROOT)/components/linuxkpi/sdio/lkpi_sdio_be.c \
    $(ROOT)/components/linuxkpi/linuxkpi_core.c

# The test double: the synthetic card behind the seam.
MOCK_SRCS := \
    $(MAKEFILE_DIR)/mock_sdio_card.c

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

SRCS := $(ENGINE_SRCS) $(MOCK_SRCS) $(APP_SRCS) $(RUNTIME_SRCS)

# Object paths carry the source path so two files with the same base name in
# different directories cannot collide (the sibling host tests use the same shape).
define OBJ
$(BUILD_DIR)/obj_$(subst /,_,$(patsubst $(ROOT)/%,%,$(1))).o
endef

OBJS := $(foreach s,$(SRCS),$(call OBJ,$(s)))

.PHONY: all run clean

all: $(BIN)

$(BUILD_DIR):
	mkdir -p $@

define COMPILE_RULE
$(call OBJ,$(1)): $(1) | $(BUILD_DIR)
	$$(CC) $$(CFLAGS)  -MMD -MP -c $$< -o $$@
endef

$(foreach src,$(SRCS),$(eval $(call COMPILE_RULE,$(src))))

$(BIN): $(OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

run: $(BIN)
	$(BIN)

# Header dependencies: -MMD -MP writes one .d per object, so a header edit
# rebuilds every object that included it (a stale object here once linked two
# disagreeing struct layouts into one binary).
-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)

clean:
	rm -rf $(BUILD_DIR)

# Host stress harness: the linuxkpi USB urb engine under injected faults.
#
# Self contained; build it from anywhere with:
#   make -C <repo>/tests/host/linuxkpi-usb-urb-stress
#
# One binary, linuxkpi-usb-urb-stress: the engine (components/linuxkpi/usb) runs against
# the synthetic controller and the scripted device model of the sibling host
# suite (tests/host/linuxkpi-usb-test), behind a harness-side wrapper that owns
# the injection knobs (completion timing, completion order, per-transfer error,
# double completion).  The FreeRTOS-based host runtime underneath is the same
# object set the sibling host tests compile, so the runtime this suite
# exercises is the one the rest of the mock tier uses.
#
# The build is a sanitizer build: AddressSanitizer + UndefinedBehaviorSanitizer,
# and a sanitizer report is a hard failure (-fno-sanitize-recover=all makes a
# UBSan report abort instead of continuing).

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT         := $(abspath $(MAKEFILE_DIR)/../../..)
# The sibling host suite whose controller, device model, command-driver
# stand-in and linuxkpi core services this harness reuses unchanged.
ROUND_A      := $(MAKEFILE_DIR)/../linuxkpi-usb-test

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
BIN       := $(BUILD_DIR)/linuxkpi-usb-urb-stress

SANITIZE := -fsanitize=address,undefined -fno-sanitize-recover=all \
            -fno-omit-frame-pointer

CFLAGS := -ggdb3 -O1 -Wall -Wextra -ffunction-sections -fdata-sections \
          $(SANITIZE) \
          -DCMSIS2_BACKEND_FREERTOS \
          -DCMSIS_device_header='"cmsis_host_device.h"' \
          -DFREERTOS_HOOKS_TAG='"lkpi-usb-stress-mock"' \
          -I$(ROOT) \
          -I$(ROOT)/os/freertos \
          -I$(ROOT)/os/cmsis2 \
          -I$(ROOT)/os/cmsis2/host-compat \
          -I$(ROOT)/components/linuxkpi \
          -I$(ROOT)/components/linuxkpi/include \
          -I$(ROOT)/components/linuxkpi/usb \
          -I$(KERNEL)/include \
          -I$(KERNEL)/portable/ThirdParty/GCC/Posix \
          -I$(KERNEL)/portable/ThirdParty/GCC/Posix/utils \
          -I$(CMSIS)/CMSIS/RTOS2/Include \
          -I$(ROUND_A) \
          -I$(MAKEFILE_DIR) \
          -I$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Include

LDFLAGS := -pthread -Wl,--gc-sections $(SANITIZE)

# The engine and its platform services. The services are the real body now
# (components/linuxkpi/linuxkpi_core.c): the host stand-in the sibling suite
# used to carry was retired when the real body landed.  ENGINE_DIR is
# overridable so the wrapper's red control can build the same harness against
# a mutated copy of the engine (a gate that cannot go red is itself broken).
ENGINE_DIR ?= $(ROOT)/components/linuxkpi/usb
ENGINE_SRCS := \
    $(ENGINE_DIR)/linuxkpi_usb.c \
    $(ROOT)/components/linuxkpi/linuxkpi_core.c

# The test double, reused verbatim from the sibling suite: controller, scripted
# device model, clean-room command-driver stand-in.
MOCK_SRCS := \
    $(ROUND_A)/mock_hcd.c \
    $(ROUND_A)/usb_msc_driver.c

# The harness: environment wrapper, judgement rules, matrix, seeded stream, entry.
APP_SRCS := \
    $(MAKEFILE_DIR)/engine_env.c \
    $(MAKEFILE_DIR)/judge.c \
    $(MAKEFILE_DIR)/matrix.c \
    $(MAKEFILE_DIR)/fuzz.c \
    $(MAKEFILE_DIR)/main.c

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

.PHONY: all clean

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

# Header dependencies: -MMD -MP writes one .d per object, so a
# header edit rebuilds every object that included it (a stale object
# here once linked two disagreeing struct layouts into one binary).
-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)

clean:
	rm -rf $(BUILD_DIR)

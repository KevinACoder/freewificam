# Host tests: the linuxkpi PCI layer over its backend seam.
#
# Self contained; build it from anywhere with:
#   make -C <repo>/tests/host/linuxkpi-pcie-test
#
# One binary, linuxkpi-pcie-test: the engine (components/linuxkpi/pci) runs against
# the synthetic host and the scripted device model, with the FreeRTOS-based host
# runtime underneath. The suite's wrapper (tools/mock/linuxkpi-pcie-test) drives
# it; this Makefile also owns the compile-time contract of the new PCI face headers
# (contract-check, below) - they must compile standalone against the in-repo
# closure.
#
# The kernel and the CMSIS-RTOS2 backend objects are shared with the sibling host
# tests, so the runtime a suite exercises is the same one the rest of the mock tier
# uses.

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
BIN       := $(BUILD_DIR)/linuxkpi-pcie-test

# The flag set matches the sibling host test directories; Werror is not used so an
# imported byte-identical unit cannot fail the build.
CFLAGS := -ggdb3 -O0 -Wall -ffunction-sections -fdata-sections \
          -DCMSIS2_BACKEND_FREERTOS \
          -DCMSIS_device_header='"cmsis_host_device.h"' \
          -DFREERTOS_HOOKS_TAG='"linuxkpi-pcie-test"' \
          -I$(ROOT) \
          -I$(ROOT)/components/ulog/src \
          -I$(ROOT)/os/freertos \
          -I$(ROOT)/os/cmsis2 \
          -I$(ROOT)/os/cmsis2/host-compat \
          -I$(ROOT)/components/linuxkpi \
          -I$(ROOT)/components/linuxkpi/include \
          -I$(ROOT)/components/linuxkpi/pci \
          -I$(KERNEL)/include \
          -I$(KERNEL)/portable/ThirdParty/GCC/Posix \
          -I$(KERNEL)/portable/ThirdParty/GCC/Posix/utils \
          -I$(CMSIS)/CMSIS/RTOS2/Include \
          -I$(MAKEFILE_DIR) \
          -I$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Include

LDFLAGS := -pthread -Wl,--gc-sections

# The engine and its platform services (the real body: components/linuxkpi/
# linuxkpi_core.c).
ENGINE_SRCS := \
    $(ROOT)/components/linuxkpi/pci/linuxkpi_pci.c \
    $(ROOT)/components/linuxkpi/linuxkpi_core.c

# The test double: the synthetic host and the scripted device model.
MOCK_SRCS := \
    $(MAKEFILE_DIR)/mock_pcie_hcd.c

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

.PHONY: all contract-check clean

all: $(BIN) contract-check

$(BUILD_DIR):
	mkdir -p $@

define COMPILE_RULE
$(call OBJ,$(1)): $(1) | $(BUILD_DIR)
	$$(CC) $$(CFLAGS)  -MMD -MP -c $$< -o $$@
endef

$(foreach src,$(SRCS),$(eval $(call COMPILE_RULE,$(src))))

$(BIN): $(OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

# ---- compile-time contract of the new face headers ----------------------------
#
# The three headers this round adds to the face must compile standalone against the
# in-repo closure plus libc: a header that reaches into the machine or a package
# would break a consumer build, and a header that needs a translation unit's
# context would break the driver that includes it first.
LKPI     := $(ROOT)/components/linuxkpi
PCI_HDRS := $(LKPI)/include/linux/pci.h \
            $(LKPI)/include/linux/device-id/pci.h \
            $(LKPI)/include/linux/dma-mapping.h
LKPI_CFLAGS := -std=gnu11 -Wall -I$(LKPI)/include -I$(LKPI) -I$(ROOT)/components/ulog/src

contract-check: | $(BUILD_DIR)
	@for h in $(PCI_HDRS); do \
	    $(CC) $(LKPI_CFLAGS) -x c -fsyntax-only $$h || exit 1; \
	done
	@echo "contract-check: pci face headers compile standalone"

# Header dependencies: -MMD -MP writes one .d per object, so a header edit rebuilds
# every object that included it.
-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)

clean:
	rm -rf $(BUILD_DIR)

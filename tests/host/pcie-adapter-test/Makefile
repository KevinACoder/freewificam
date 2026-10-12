# Host tests: the PCI adapter and the message-interrupt domain over scripted
# board drivers.
#
# Self contained; build it from anywhere with:
#   make -C <repo>/tests/host/pcie-adapter-test
#
# One binary, pcie-adapter-test: the engine drives the board adapter
# (ports/pcie/lkpi_pcie_port.c) end to end against the scripted root complex and
# MSI programming in mock_board.c, and the real message-interrupt domain
# (arch/aarch64/gicv3_msi.c) and arch registry (arch/aarch64/gicv3.c) run against
# the scripted ITS primitives. The FreeRTOS-based host runtime sits underneath.
#
# The board's own MSI / MSI-X programming (dwc_msi.c / dwc_msix.c) is NOT linked
# here: it is the test double's job to stand in for it, so the adapter's mapping is
# what is under test. dwc_msi.c is compile-gated by the pcie-backend-build suite.

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT         := $(abspath $(MAKEFILE_DIR)/../../..)

KERNEL  := $(ROOT)/third-party/FreeRTOS-Kernel
CMSIS   := $(ROOT)/third-party/CMSIS_6

ifeq ($(PATCHES),off)
ADAPTER := $(ROOT)/third-party/CMSIS-FreeRTOS
else
ADAPTER := $(shell sh $(ROOT)/tools/patches/replay.sh CMSIS-FreeRTOS)
endif
ifeq ($(ADAPTER),)
$(error tools/patches/replay.sh returned no source root for CMSIS-FreeRTOS)
endif

BUILD_DIR := $(MAKEFILE_DIR)/build$(if $(filter off,$(PATCHES)),/pinned,)
BIN       := $(BUILD_DIR)/pcie-adapter-test

CFLAGS := -ggdb3 -O0 -Wall -ffunction-sections -fdata-sections \
          -DCMSIS2_BACKEND_FREERTOS \
          -DCMSIS_device_header='"cmsis_host_device.h"' \
          -DFREERTOS_HOOKS_TAG='"pcie-adapter-test"' \
          -I$(ROOT) \
          -I$(ROOT)/components/ulog/src \
          -I$(ROOT)/os/freertos \
          -I$(ROOT)/os/cmsis2 \
          -I$(ROOT)/os/cmsis2/host-compat \
          -I$(ROOT)/os/baremetal \
          -I$(ROOT)/components/linuxkpi \
          -I$(ROOT)/components/linuxkpi/include \
          -I$(ROOT)/components/linuxkpi/pci \
          -I$(ROOT)/ports/pcie \
          -I$(ROOT)/arch/aarch64 \
          -I$(ROOT)/board/rk3568-itx \
          -I$(ROOT)/board/rk3568-itx/drivers \
          -I$(ROOT)/board/rk3568-itx/drivers/pcie \
          -I$(KERNEL)/include \
          -I$(KERNEL)/portable/ThirdParty/GCC/Posix \
          -I$(KERNEL)/portable/ThirdParty/GCC/Posix/utils \
          -I$(CMSIS)/CMSIS/RTOS2/Include \
          -I$(CMSIS)/CMSIS/Driver/Include \
          -I$(CMSIS)/CMSIS/Core/Include \
          -I$(MAKEFILE_DIR) \
          -I$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Include

LDFLAGS := -pthread -Wl,--gc-sections

ENGINE_SRCS := \
    $(ROOT)/components/linuxkpi/pci/linuxkpi_pci.c \
    $(ROOT)/components/linuxkpi/linuxkpi_core.c

ADAPTER_SRCS := \
    $(ROOT)/ports/pcie/lkpi_pcie_port.c

ARCH_SRCS := \
    $(ROOT)/arch/aarch64/gicv3_msi.c

MOCK_SRCS := \
    $(MAKEFILE_DIR)/mock_board.c

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

SRCS := $(ENGINE_SRCS) $(ADAPTER_SRCS) $(ARCH_SRCS) $(MOCK_SRCS) $(APP_SRCS) \
        $(RUNTIME_SRCS)

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

-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)

clean:
	rm -rf $(BUILD_DIR)

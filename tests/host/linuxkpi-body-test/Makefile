# Host suite: the linuxkpi body linked as one program.
#
# Self contained; build it from anywhere with:
#   make -C <repo>/tests/host/linuxkpi-body-test
#
# The point of this suite is link closure and the bring-up path: the linuxkpi
# service bodies, the skbuff pool, the firmware registry, the USB engine and a
# driver registered through the module-section mechanism all land in one host
# binary, and the cases drive them the way a board bring-up does (section walk,
# class-hook claim, probe, urb traffic, firmware lookup). Where the sibling
# suites model one mechanism each behind a double, this one runs the real
# translation units together - a missing body or a mismatched signature is a
# link error here rather than a board surprise.
#
# Two seams get host implementations, the same way the board supplies them:
#   - the platform hooks the skbuff pool declares (lkpi_spl_*): host bodies here
#     (single-threaded critical section, libc heap);
#   - the USB backend seam: the sibling suite's synthetic HCD and scripted
#     device model (tests/host/linuxkpi-usb-test), reused source-for-source the
#     way the urb-stress round reuses them.
#
# Targets:
#   all   the binary (default)
#   run   build, then run it
#   clean remove the build directory

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

# The synthetic HCD and the scripted device model are the sibling suite's; this
# suite reuses the files, it does not copy them.
USB_ROUND := $(ROOT)/tests/host/linuxkpi-usb-test

BUILD_DIR := $(MAKEFILE_DIR)/build$(if $(filter off,$(PATCHES)),/pinned,)
BIN       := $(BUILD_DIR)/linuxkpi-body-test

CFLAGS := -ggdb3 -O0 -Wall -Wextra -ffunction-sections -fdata-sections \
          -DCMSIS2_BACKEND_FREERTOS \
          -DCMSIS_device_header='"cmsis_host_device.h"' \
          -DFREERTOS_HOOKS_TAG='"linuxkpi-body-test"' \
          -I$(ROOT) \
          -I$(ROOT)/os/freertos \
          -I$(ROOT)/os/cmsis2 \
          -I$(ROOT)/os/cmsis2/host-compat \
          -I$(ROOT)/components \
          -I$(ROOT)/components/linuxkpi \
          -I$(ROOT)/components/linuxkpi/include \
          -I$(ROOT)/components/linuxkpi/usb \
          -I$(KERNEL)/include \
          -I$(KERNEL)/portable/ThirdParty/GCC/Posix \
          -I$(KERNEL)/portable/ThirdParty/GCC/Posix/utils \
          -I$(CMSIS)/CMSIS/RTOS2/Include \
          -I$(MAKEFILE_DIR) \
          -I$(USB_ROUND) \
          -I$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Include

LDFLAGS := -pthread -Wl,--gc-sections \
           -Wl,-T,$(MAKEFILE_DIR)/lkpi_module.ld

# The bodies under test: the real service bodies, the real pool, the real
# firmware registry and blob, the real USB engine. No doubles here.
BODY_SRCS := \
    $(ROOT)/components/linuxkpi/linuxkpi_core.c \
    $(ROOT)/components/linuxkpi/skbuff/skbuff_pool.c \
    $(ROOT)/components/linuxkpi/fw_registry.c \
    $(ROOT)/components/linuxkpi/fw/rtl8188eufw_blob.c \
    $(ROOT)/components/linuxkpi/usb/linuxkpi_usb.c \
    $(MAKEFILE_DIR)/lkpi_platform_host.c

# The synthetic controller and the device model (reused), plus this suite's
# stand-in wireless driver registered through the module section.
MOCK_SRCS := \
    $(USB_ROUND)/mock_hcd.c \
    $(MAKEFILE_DIR)/wlan_driver_stub.c

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

# The pool's platform hooks are the host's: the definition turns off the pool's
# own target bodies (the same switch the stress suite's real-pool build uses).
POOL_SRC := $(ROOT)/components/linuxkpi/skbuff/skbuff_pool.c
POOL_OBJ := $(call OBJ,$(POOL_SRC))

SRCS := $(BODY_SRCS) $(MOCK_SRCS) $(APP_SRCS) $(RUNTIME_SRCS)
OBJS := $(foreach s,$(SRCS),$(call OBJ,$(s)))

.PHONY: all run clean

all: $(BIN)

$(BUILD_DIR):
	mkdir -p $@

define COMPILE_RULE
$(call OBJ,$(1)): $(1) | $(BUILD_DIR)
	$$(CC) $$(CFLAGS) -c $$< -o $$@
endef

$(foreach src,$(filter-out $(POOL_SRC),$(SRCS)),$(eval $(call COMPILE_RULE,$(src))))

# the pool body, with the host's platform hooks instead of the target's
$(POOL_OBJ): $(POOL_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -DLKPI_SKB_POOL_NO_TARGET_HOOKS -c $< -o $@

$(BIN): $(OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

run: $(BIN)
	$(BIN)

clean:
	rm -rf $(BUILD_DIR)$(if $(filter off,$(PATCHES)),,/pinned)

# Host tests: the linuxkpi USB layer over its backend seam.
#
# Self contained; build it from anywhere with:
#   make -C <repo>/tests/host/linuxkpi-usb-test
#
# One binary, linuxkpi-usb-test: the engine (components/linuxkpi/usb) runs against
# the synthetic controller and the scripted device model, with the FreeRTOS-based
# host runtime underneath.  The suite's wrapper (tools/mock/linuxkpi-usb-test)
# drives it; this Makefile also owns the compile-time contract of the imported
# linuxkpi and skbuff units (contract-check, below) - those units have no project
# of their own.
#
# The kernel and the CMSIS-RTOS2 backend objects are shared with the sibling host
# tests, so the runtime a suite exercises is the same one the rest of the mock
# tier uses.

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
BIN       := $(BUILD_DIR)/linuxkpi-usb-test

# The two known warnings of the imported linuxkpi headers (recorded in
# components/linuxkpi/PROVENANCE.md: the __stringify redefinition and the mutex
# pointer passed to the spinlock pair) stay visible here; Werror is not used so
# a byte-identical import cannot fail the build.  The flag set matches the
# sibling host test directories.
CFLAGS := -ggdb3 -O0 -Wall -ffunction-sections -fdata-sections \
          -DCMSIS2_BACKEND_FREERTOS \
          -DCMSIS_device_header='"cmsis_host_device.h"' \
          -DFREERTOS_HOOKS_TAG='"linuxkpi-usb-test"' \
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
          -I$(MAKEFILE_DIR) \
          -I$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Include

LDFLAGS := -pthread -Wl,--gc-sections

# The engine and its platform services.
ENGINE_SRCS := \
    $(ROOT)/components/linuxkpi/usb/linuxkpi_usb.c \
    $(MAKEFILE_DIR)/lkpi_core_host.c

# The test double: controller, device model, command-driver stand-in, loader.
MOCK_SRCS := \
    $(MAKEFILE_DIR)/mock_hcd.c \
    $(MAKEFILE_DIR)/usb_msc_driver.c \
    $(MAKEFILE_DIR)/mockvec.c

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
	$$(CC) $$(CFLAGS) -c $$< -o $$@
endef

$(foreach src,$(SRCS),$(eval $(call COMPILE_RULE,$(src))))

$(BIN): $(OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

# ---- compile-time contract of the imported units ------------------------------
#
# The linuxkpi contract header must keep compiling as a translation unit whose
# include closure is the in-repo headers plus libc (a header that reaches into
# the machine or a package would break a consumer build), and the skbuff unit's
# two self-contained headers must stay syntax-clean on their own.

LKPI     := $(ROOT)/components/linuxkpi
SKBUFF   := $(ROOT)/components/skbuff
SKBUFF_HEADERS := $(SKBUFF)/skbuff.h $(SKBUFF)/freertos/freertos_skbuff.h
LKPI_CFLAGS := -std=gnu11 -Wall -I$(LKPI)/include -I$(LKPI)

contract-check: | $(BUILD_DIR)
	@$(CC) $(LKPI_CFLAGS) -c $(MAKEFILE_DIR)/lkpi_contract_tu.c -o $(BUILD_DIR)/lkpi_contract_tu.o
	@$(CC) $(LKPI_CFLAGS) -H -fsyntax-only $(MAKEFILE_DIR)/lkpi_contract_tu.c 2> $(BUILD_DIR)/lkpi-closure.raw
	@grep -E '^\.+ ' $(BUILD_DIR)/lkpi-closure.raw | sed -e 's/^\.* //' | sort -u > $(BUILD_DIR)/lkpi-closure.txt
	@grep -v -e '/usr/' -e 'components/linuxkpi' -e 'lkpi_contract_tu.c' \
	    $(BUILD_DIR)/lkpi-closure.txt > $(BUILD_DIR)/lkpi-closure-outside.txt || true
	@if [ -s $(BUILD_DIR)/lkpi-closure-outside.txt ]; then \
	    echo "contract-check: FAILED - headers outside the repository closure:"; \
	    cat $(BUILD_DIR)/lkpi-closure-outside.txt; exit 1; \
	fi
	@for h in $(SKBUFF_HEADERS); do \
	    $(CC) -x c -std=gnu11 -Wall -fsyntax-only -I$(SKBUFF) $$h || exit 1; \
	done
	@echo "contract-check: linuxkpi contract TU compiled, closure is in-repo + libc ($$(wc -l < $(BUILD_DIR)/lkpi-closure.txt) headers), skbuff headers clean"

clean:
	rm -rf $(BUILD_DIR)$(if $(filter off,$(PATCHES)),,/pinned)

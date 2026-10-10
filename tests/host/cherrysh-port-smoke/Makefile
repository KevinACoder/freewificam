# Host smoke: the CherrySH console shell on both CMSIS-RTOS2 host runtimes.
#
# Self contained; run it from anywhere with:
#   make -C <repo>/tests/host/cherrysh-port-smoke
#
# Two binaries share the same shell sources and the same input vector:
#   build/threadx/cherrysh-smoke-threadx     ThreadX (Linux userspace port)
#   build/freertos/cherrysh-smoke-freertos   FreeRTOS (POSIX port)
#
# The shell side (the imported subset under components/cherrysh, the port under
# ports/cherrysh, this directory's entry point) is compiled once per backend,
# because the CMSIS backend macro and the config header are compile inputs.
# Each binary links its own kernel's host runtime; everything under
# third-party/ is compiled as-is. The command and variable table boundaries
# come from symtab.ld: the host has no linker script of its own, so the
# fragment carries the snippet the upstream porting notes put into a board
# linker script, inserted into the default script.
#
#   make cross32   compile-only check of the shell side for a 32-bit target

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT         := $(abspath $(MAKEFILE_DIR)/../../..)

TX     := $(ROOT)/third-party/threadx
KERNEL := $(ROOT)/third-party/FreeRTOS-Kernel
CMSIS  := $(ROOT)/third-party/CMSIS_6

# The CMSIS-FreeRTOS adapter is compiled through the patch channel, like the
# other FreeRTOS host builds.
ADAPTER := $(shell sh $(ROOT)/tools/patches/replay.sh CMSIS-FreeRTOS)
ifeq ($(ADAPTER),)
$(error tools/patches/replay.sh returned no source root for CMSIS-FreeRTOS)
endif

SYMTAB_LD := $(MAKEFILE_DIR)/symtab.ld
CROSS     ?= arm-none-eabi-gcc

# ---- sources -----------------------------------------------------------------

# The shell side: the imported subset, the port layer, the smoke entry point.
SHELL_SRCS := \
    $(MAKEFILE_DIR)/main.c \
    $(ROOT)/components/cherrysh/chry_shell.c \
    $(ROOT)/components/cherrysh/cherryrl/chry_readline.c \
    $(ROOT)/components/cherrysh/builtin/clear.c \
    $(ROOT)/components/cherrysh/builtin/help.c \
    $(ROOT)/components/cherrysh/builtin/shsize.c \
    $(ROOT)/ports/cherrysh/shell_port.c \
    $(ROOT)/ports/cherrysh/console_host.c

SHELL_INCLUDES := \
    -I$(ROOT) \
    -I$(ROOT)/os/cmsis2 \
    -I$(ROOT)/os/cmsis2/host-compat \
    -I$(ROOT)/components/cherrysh \
    -I$(ROOT)/ports/cherrysh \
    -I$(CMSIS)/CMSIS/RTOS2/Include

THREADX_SRCS := \
    $(ROOT)/os/threadx/tx_glue.c \
    $(ROOT)/os/threadx/heap.c \
    $(ROOT)/os/threadx/cmsis2_threadx.c \
    $(ROOT)/os/threadx/cmsis2_host.c \
    $(wildcard $(TX)/ports/linux/gnu/src/*.c) \
    $(wildcard $(TX)/common/src/*.c)

FREERTOS_SRCS := \
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

# -----------------------------------------------------------------------------
# Flags
#
# -malign-data=abi: the shell's command and variable tables are read back as
# flat, packed arrays; x86-64 GCC by default over-aligns objects of 16 bytes
# and up (measured: a single 40-byte table entry asks for 32-byte alignment),
# which pads the linker sections and hides every entry after the first. The
# ABI alignment keeps the sections dense, the way they naturally are on the
# 32-bit target (measured with arm-none-eabi: 4-byte objects, dense).
# -----------------------------------------------------------------------------

THREADX_CFLAGS := -ggdb3 -O0 -Wall -malign-data=abi \
    -D_GNU_SOURCE \
    -DTX_LINUX_MULTI_CORE \
    -DTX_INCLUDE_USER_DEFINE_FILE \
    -DCMSIS2_BACKEND_THREADX \
    $(SHELL_INCLUDES) \
    -I$(ROOT)/os/threadx \
    -I$(TX)/common/inc \
    -I$(TX)/ports/linux/gnu/inc

FREERTOS_CFLAGS := -ggdb3 -O0 -Wall -malign-data=abi -ffunction-sections -fdata-sections \
    -DCMSIS2_BACKEND_FREERTOS \
    -DCMSIS_device_header='"cmsis_host_device.h"' \
    -DFREERTOS_HOOKS_TAG='"cherrysh-port-smoke"' \
    $(SHELL_INCLUDES) \
    -I$(ROOT)/os/freertos \
    -I$(KERNEL)/include \
    -I$(KERNEL)/portable/ThirdParty/GCC/Posix \
    -I$(KERNEL)/portable/ThirdParty/GCC/Posix/utils \
    -I$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Include

THREADX_LDFLAGS  := -pthread -lrt -Wl,-T,$(SYMTAB_LD)
FREERTOS_LDFLAGS := -pthread -Wl,--gc-sections -Wl,-T,$(SYMTAB_LD)

# ---- targets -----------------------------------------------------------------

THREADX_BUILD   := $(MAKEFILE_DIR)/build/threadx
FREERTOS_BUILD  := $(MAKEFILE_DIR)/build/freertos
THREADX_TARGET  := $(THREADX_BUILD)/cherrysh-smoke-threadx
FREERTOS_TARGET := $(FREERTOS_BUILD)/cherrysh-smoke-freertos

.PHONY: all cross32 clean

all: $(THREADX_TARGET) $(FREERTOS_TARGET)

# Object names fold in the path below the repository root: the port, the kernel
# and the vendored component share flat source names, so bare names would
# collide in the build directory.
flat = $(subst /,_,$(patsubst $(ROOT)/%,%,$(1)))

define COMPILE_RULE
$(1)/$(call flat,$(2)).o: $(2) | $(1)
	$(CC) $(3)  -MMD -MP -c $$< -o $$@
endef

THREADX_APP_OBJS  := $(foreach src,$(SHELL_SRCS),$(THREADX_BUILD)/app/$(call flat,$(src)).o)
THREADX_RT_OBJS   := $(foreach src,$(THREADX_SRCS),$(THREADX_BUILD)/runtime/$(call flat,$(src)).o)
FREERTOS_APP_OBJS := $(foreach src,$(SHELL_SRCS),$(FREERTOS_BUILD)/app/$(call flat,$(src)).o)
FREERTOS_RT_OBJS  := $(foreach src,$(FREERTOS_SRCS),$(FREERTOS_BUILD)/runtime/$(call flat,$(src)).o)

$(foreach src,$(SHELL_SRCS),$(eval $(call COMPILE_RULE,$(THREADX_BUILD)/app,$(src),$(THREADX_CFLAGS))))
$(foreach src,$(THREADX_SRCS),$(eval $(call COMPILE_RULE,$(THREADX_BUILD)/runtime,$(src),$(THREADX_CFLAGS))))
$(foreach src,$(SHELL_SRCS),$(eval $(call COMPILE_RULE,$(FREERTOS_BUILD)/app,$(src),$(FREERTOS_CFLAGS))))
$(foreach src,$(FREERTOS_SRCS),$(eval $(call COMPILE_RULE,$(FREERTOS_BUILD)/runtime,$(src),$(FREERTOS_CFLAGS))))

$(THREADX_BUILD)/app $(THREADX_BUILD)/runtime \
$(FREERTOS_BUILD)/app $(FREERTOS_BUILD)/runtime:
	mkdir -p $@

$(THREADX_TARGET): $(THREADX_APP_OBJS) $(THREADX_RT_OBJS)
	$(CC) $^ $(THREADX_LDFLAGS) -o $@

$(FREERTOS_TARGET): $(FREERTOS_APP_OBJS) $(FREERTOS_RT_OBJS)
	$(CC) $^ $(FREERTOS_LDFLAGS) -o $@

# The port layer and the imported translation units compile for a 32-bit
# target (arm-none-eabi, cortex-m4), syntax only: the "no 64-bit assumptions"
# rule made mechanical for this component. The shared gate script's cross32
# section names the net80211 contract, so this project carries its own; the
# console host backend stays out - it is host I/O by definition.
CROSS32_SRCS := \
    $(ROOT)/components/cherrysh/chry_shell.c \
    $(ROOT)/components/cherrysh/cherryrl/chry_readline.c \
    $(ROOT)/components/cherrysh/builtin/clear.c \
    $(ROOT)/components/cherrysh/builtin/help.c \
    $(ROOT)/components/cherrysh/builtin/shsize.c \
    $(ROOT)/ports/cherrysh/shell_port.c

cross32:
	$(CROSS) -std=gnu11 -Wall -Wno-unused-function -mcpu=cortex-m4 -mthumb \
	    -fsyntax-only $(SHELL_INCLUDES) $(CROSS32_SRCS)

# Header dependencies: -MMD -MP writes one .d per object, so a
# header edit rebuilds every object that included it (a stale object
# here once linked two disagreeing struct layouts into one binary).
-include $(shell find $(MAKEFILE_DIR)/build -name '*.d' 2>/dev/null)

clean:
	rm -rf $(MAKEFILE_DIR)/build

# Host suite: the SDIO adapter against a scripted stack.
#
# Self contained; run it from anywhere with:
#   make -C <repo>/tests/host/fsl-sdmmc-adapter-test        # build
#   make -C <repo>/tests/host/fsl-sdmmc-adapter-test run    # build and run
#
# The suite links the adapter and the seam's own dispatchers against a stub that
# implements the nine entry points of the imported stack the adapter uses. The
# imported stack itself is not compiled: what is under test is the mapping and
# the length discipline, and a scripted stack is what makes them assertable call
# by call.

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT  := $(abspath $(MAKEFILE_DIR)/../../..)
VEND  := $(ROOT)/components/fsl_sdmmc
PORT  := $(ROOT)/ports/fsl_sdmmc
SEAM  := $(ROOT)/components/linuxkpi/sdio
LKPI  := $(ROOT)/components/linuxkpi

BUILD_DIR := $(MAKEFILE_DIR)/build
CC ?= cc

INCLUDES := \
    -I$(MAKEFILE_DIR) \
    -I$(PORT) -I$(PORT)/shadow \
    -I$(SEAM) \
    -I$(VEND)/common -I$(VEND)/sdio -I$(VEND)/osa \
    -I$(LKPI) -I$(LKPI)/include \
    -I$(ROOT) -I$(ROOT)/os/cmsis2 -I$(ROOT)/os/baremetal -I$(ROOT)/arch/aarch64 \
    -I$(ROOT)/components/ulog/src

CFLAGS := -std=c11 -g -O1 -Wall -Wextra \
    -DCONFIG_FSL_SDMMC_ENABLE_SDIO=1 -DULOG_ENABLED -DULOG_MAX_MESSAGE_LENGTH=256 \
    -DLKPI_ABS_PLATFORM_PROVIDED -DCMSIS_device_header='"cmsis_os2.h"'

# Header dependencies are tracked: a header edit must rebuild every object that
# included it, or the run would exercise a binary whose units disagree about the
# very layouts the headers state.
DEPFLAGS := -MMD -MP

SUITE_SRCS := \
    $(MAKEFILE_DIR)/main.c \
    $(MAKEFILE_DIR)/fsl_stub.c \
    $(MAKEFILE_DIR)/host_shim.c

PORT_SRCS := \
    $(PORT)/lkpi_sdio_fsl.c \
    $(SEAM)/lkpi_sdio_be.c

ALL_SRCS := $(SUITE_SRCS) $(PORT_SRCS)

define OBJ
$(BUILD_DIR)/$(subst /,_,$(patsubst $(ROOT)/%,%,$(1))).o
endef

OBJS := $(foreach s,$(ALL_SRCS),$(call OBJ,$(s)))
BIN := $(BUILD_DIR)/fsl-sdmmc-adapter-test

.PHONY: all run clean

all: $(BIN)

define COMPILE_RULE
$(call OBJ,$(1)): $(1) | $(BUILD_DIR)
	$$(CC) $$(CFLAGS) $$(DEPFLAGS) $$(INCLUDES) -c $$< -o $$@
endef

$(foreach s,$(ALL_SRCS),$(eval $(call COMPILE_RULE,$(s))))

$(BIN): $(OBJS)
	$(CC) $^ -o $@

run: $(BIN)
	$(BIN)

$(BUILD_DIR):
	mkdir -p $@

-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)

clean:
	rm -rf $(BUILD_DIR)

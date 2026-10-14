# Host suite: the eMMC line over a scripted controller.
#
# Self contained; run it from anywhere with:
#   make -C <repo>/tests/host/fsl-sdmmc-mmc-test        # build
#   make -C <repo>/tests/host/fsl-sdmmc-mmc-test run    # build and run
#
# The suite links the REAL imported MMC card layer, the REAL port over it (the
# OS abstraction, the card service and the host glue) and a scripted controller
# (scripted_mshc.c) in place of the board driver. The controller is the only
# thing scripted: the ladder, the width and clock programming and the block
# transfers run the same bytes the board image runs.

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT  := $(abspath $(MAKEFILE_DIR)/../../..)
VEND  := $(ROOT)/components/fsl_sdmmc
PORT  := $(ROOT)/ports/fsl_sdmmc
BOARD := $(ROOT)/board/rk3568-itx/drivers
CMSIS := $(ROOT)/third-party/CMSIS_6

BUILD_DIR := $(MAKEFILE_DIR)/build
CC ?= cc

INCLUDES := \
    -I$(MAKEFILE_DIR) \
    -I$(BOARD) \
    -I$(VEND)/common -I$(VEND)/mmc -I$(VEND)/osa \
    -I$(PORT) -I$(PORT)/shadow \
    -I$(ROOT) -I$(ROOT)/os/cmsis2 -I$(ROOT)/os/baremetal -I$(ROOT)/arch/aarch64 \
    -I$(ROOT)/components/ulog/src \
    -I$(CMSIS)/CMSIS/Core/Include/a-profile \
    -I$(CMSIS)/CMSIS/RTOS2/Include

CFLAGS := -std=c11 -g -O1 -Wall -Wextra \
    -DCONFIG_FSL_SDMMC_ENABLE_MMC=1 -DULOG_ENABLED -DULOG_MAX_MESSAGE_LENGTH=256 \
    -DCMSIS2_BACKEND_FREERTOS -DCMSIS_device_header='"cmsis_os2.h"'

# Header dependencies are tracked: a header edit must rebuild every object that
# included it, or the run would exercise a binary whose units disagree about the
# very layouts the headers state.
DEPFLAGS := -MMD -MP

SUITE_SRCS := \
    $(MAKEFILE_DIR)/main.c \
    $(MAKEFILE_DIR)/scripted_mshc.c \
    $(MAKEFILE_DIR)/host_cmsis.c \
    $(MAKEFILE_DIR)/host_shim.c

PORT_SRCS := \
    $(PORT)/fsl_sdmmc_osa.c \
    $(PORT)/sdmmc_mmc_card.c \
    $(PORT)/sdmmc_host_dwcmshc.c

VENDOR_SRCS := \
    $(VEND)/common/fsl_sdmmc_common.c \
    $(VEND)/mmc/fsl_mmc.c

ALL_SRCS := $(SUITE_SRCS) $(PORT_SRCS) $(VENDOR_SRCS)

define OBJ
$(BUILD_DIR)/$(subst /,_,$(patsubst $(ROOT)/%,%,$(1))).o
endef

OBJS := $(foreach s,$(ALL_SRCS),$(call OBJ,$(s)))
BIN := $(BUILD_DIR)/fsl-sdmmc-mmc-test

.PHONY: all run red-control clean

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

# The red control: one expectation is deliberately wrong, so the run must fail.
# A battery that cannot go red cannot prove anything green.
red-control: $(BIN)
	! $(BIN) --fault expect-tamper

$(BUILD_DIR):
	mkdir -p $@

-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)

clean:
	rm -rf $(BUILD_DIR)

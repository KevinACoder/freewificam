# Host suite: the storage stack against a scripted NVMe controller.
#
# Self contained; run it from anywhere with:
#   make -C <repo>/tests/host/pcie-storage-test        # build
#   make -C <repo>/tests/host/pcie-storage-test run    # build and run
#
# The suite links the REAL adapter (ports/fatfs/: the shadow config header, the
# block-device seam, diskio, the OS glue and the mount face) and the REAL
# vendored FatFs (components/fatfs/: ff.c and ffunicode.c), with a scripted NVMe
# controller (scripted_nvme.c) in place of the board driver and a minimal host
# CMSIS-RTOS2 backend (host_cmsis.c) in place of the RTOS. The controller is the
# only thing scripted: the mapping, the mount, the format and the round trip run
# the same bytes the board image runs.

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT   := $(abspath $(MAKEFILE_DIR)/../../..)
CMSIS  := $(ROOT)/third-party/CMSIS_6
FATFS  := $(ROOT)/components/fatfs
PORT   := $(ROOT)/ports/fatfs
NVME   := $(ROOT)/board/rk3568-itx/drivers/nvme

BUILD_DIR := $(MAKEFILE_DIR)/build
CC ?= cc

INCLUDES := \
    -I$(MAKEFILE_DIR) \
    -I$(FATFS) -I$(PORT) \
    -I$(NVME) \
    -I$(ROOT) -I$(ROOT)/os/cmsis2 \
    -I$(CMSIS)/CMSIS/Driver/Include \
    -I$(CMSIS)/CMSIS/RTOS2/Include

# The RTOS backend key is required by the CMSIS-RTOS2 facade the OS glue includes
# (os/cmsis2/cmsis2.h); the backend itself is the host stand-in in host_cmsis.c,
# because the RTOS is not what this suite tests.
CFLAGS := -std=c11 -g -O1 -Wall -Wextra -DCMSIS2_BACKEND_FREERTOS

# Header dependencies are tracked: a header edit must rebuild every object that
# included it, or the run would exercise a binary whose units disagree about the
# very layouts the headers state.
DEPFLAGS := -MMD -MP

SUITE_SRCS := \
    $(MAKEFILE_DIR)/main.c \
    $(MAKEFILE_DIR)/scripted_nvme.c \
    $(MAKEFILE_DIR)/host_cmsis.c

PORT_SRCS := \
    $(PORT)/blkdev.c \
    $(PORT)/diskio.c \
    $(PORT)/fatfs_os.c \
    $(PORT)/fs.c

VENDOR_SRCS := \
    $(FATFS)/ff.c \
    $(FATFS)/ffunicode.c

ALL_SRCS := $(SUITE_SRCS) $(PORT_SRCS) $(VENDOR_SRCS)

define OBJ
$(BUILD_DIR)/$(subst /,_,$(patsubst $(ROOT)/%,%,$(1))).o
endef

OBJS := $(foreach s,$(ALL_SRCS),$(call OBJ,$(s)))
BIN := $(BUILD_DIR)/pcie-storage-test

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

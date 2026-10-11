# Host smoke test: ThreadX (Linux userspace port) behind the CMSIS-RTOS2 API.
#
# Self contained; run it from anywhere with:
#   make -C <repo>/tests/host/threadx-cmsis2-smoke
#
# Everything under third-party/ is compiled as-is, without local patches. The local glue
# lives in os/threadx/ (kernel configuration, glue, heap seam, CMSIS-RTOS2 backend) and
# os/cmsis2/ (backend facade plus the host stand-ins for the CMSIS Core headers).

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT         := $(abspath $(MAKEFILE_DIR)/../../..)

TX    := $(ROOT)/third-party/threadx
CMSIS := $(ROOT)/third-party/CMSIS_6

BUILD_DIR := $(MAKEFILE_DIR)/build
TARGET    := $(BUILD_DIR)/threadx-cmsis2-smoke

# os/cmsis2/host-compat carries the host stand-ins for the CMSIS Core headers; the CMSIS
# Core include directory itself is deliberately absent so no ARM-only header is reached.
INCLUDES := \
    -I$(ROOT) \
    -I$(ROOT)/components/ulog/src \
    -I$(ROOT)/os/threadx \
    -I$(ROOT)/os/cmsis2 \
    -I$(ROOT)/os/cmsis2/host-compat \
    -I$(TX)/common/inc \
    -I$(TX)/ports/linux/gnu/inc \
    -I$(CMSIS)/CMSIS/RTOS2/Include

# _GNU_SOURCE and TX_LINUX_MULTI_CORE are requirements of the Linux userspace port (the
# latter keeps the process pinned to one core), TX_INCLUDE_USER_DEFINE_FILE pulls in
# os/threadx/tx_user.h. The example build's debug and trace switches are deliberately not
# passed - they add logging noise, not behaviour.
CFLAGS := -ggdb3 -O0 -Wall \
          -D_GNU_SOURCE \
          -DTX_LINUX_MULTI_CORE \
          -DTX_INCLUDE_USER_DEFINE_FILE \
          -DCMSIS2_BACKEND_THREADX \
          $(INCLUDES)

# The static object tables are sized per image (the board images pass their own
# numbers on the compile line).  The smoke pins the smallest useful timer table
# so its timer case can prove the two properties a caller depends on - the table
# holds exactly this many, and a released slot is reusable - in milliseconds.
SMOKE_TIMERS ?= 4
CFLAGS += -DCMSIS2_MAX_TIMERS=$(SMOKE_TIMERS)

LDFLAGS := -pthread -lrt

# The kernel and the port together are the complete runtime, so every translation unit is
# linked - there is no archive to pull members from.
SRCS := \
    $(MAKEFILE_DIR)/main.c \
    $(ROOT)/os/threadx/tx_glue.c \
    $(ROOT)/os/threadx/heap.c \
    $(ROOT)/os/threadx/cmsis2_threadx.c \
    $(ROOT)/os/threadx/cmsis2_host.c \
    $(wildcard $(TX)/ports/linux/gnu/src/*.c) \
    $(wildcard $(TX)/common/src/*.c)

# Object names fold in the path below the repository root: the port and the kernel share
# a flat source naming scheme, so the bare file names would collide in build/.
OBJS := $(foreach src,$(SRCS),$(BUILD_DIR)/$(subst /,_,$(patsubst $(ROOT)/%,%,$(src))).o)

.PHONY: all clean

all: $(TARGET)

define COMPILE_RULE
$(BUILD_DIR)/$(subst /,_,$(patsubst $(ROOT)/%,%,$(1))).o: $(1) | $(BUILD_DIR)
	$(CC) $(CFLAGS)  -MMD -MP -c $$< -o $$@
endef

$(foreach src,$(SRCS),$(eval $(call COMPILE_RULE,$(src))))

$(BUILD_DIR):
	mkdir -p $@

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

# Header dependencies: -MMD -MP writes one .d per object, so a
# header edit rebuilds every object that included it (a stale object
# here once linked two disagreeing struct layouts into one binary).
-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)

clean:
	rm -rf $(BUILD_DIR)

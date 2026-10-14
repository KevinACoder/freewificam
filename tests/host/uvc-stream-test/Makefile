# Host suite: the UVC stream engine against a scripted device.
#
# The suite compiles the engine the UVC patch adds
# (class/video/usbh_uvc_stream.c, replayed from patches/cherryusb/0004) as one
# program with host doubles for the class-facing seams (stubs.c: the camera's
# descriptor ladder, the probe/open helpers and the urb transport) and drives
# it packet by packet the way the device would (stream_case.c).
#
# Targets: all (default) | run | clean

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT         := $(abspath $(MAKEFILE_DIR)/../../..)

CU := $(shell sh $(ROOT)/tools/patches/replay.sh cherryusb)
ifeq ($(CU),)
$(error tools/patches/replay.sh returned no source root for cherryusb)
endif

# The engine source is overridable so the red control can point the build at a
# mutated copy (tools/mock/uvc-stream-test/check.sh).
STREAM_SRC ?= $(CU)/class/video/usbh_uvc_stream.c
BUILD_DIR  ?= $(MAKEFILE_DIR)/build
BIN        := $(BUILD_DIR)/uvc-stream-test

CFLAGS := -ggdb3 -O0 -Wall -Wextra -ffunction-sections -fdata-sections \
          -I$(CU)/common -I$(CU)/core -I$(CU)/class/video -I$(CU)/class/hub \
          -I$(ROOT)/ports/cherryusb/board/rk3568-itx

LDFLAGS := -pthread -Wl,--gc-sections

SRCS := \
    $(STREAM_SRC) \
    $(MAKEFILE_DIR)/stubs.c \
    $(MAKEFILE_DIR)/stream_case.c

define OBJ
$(BUILD_DIR)/obj_$(subst /,_,$(notdir $(1))).o
endef

OBJS := $(foreach s,$(SRCS),$(call OBJ,$(s)))

.PHONY: all run clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

define COMPILE_RULE
$(call OBJ,$(1)): $(1) | $(BUILD_DIR)
	$$(CC) $$(CFLAGS) -MMD -MP -c $$< -o $$@
endef

$(foreach src,$(SRCS),$(eval $(call COMPILE_RULE,$(src))))

$(BUILD_DIR):
	mkdir -p $@

run: $(BIN)
	$(BIN)

# Header dependencies: -MMD -MP writes one .d per object, so a header edit
# rebuilds every object that included it.
-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)

clean:
	rm -rf $(BUILD_DIR)

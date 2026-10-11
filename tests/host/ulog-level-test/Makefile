# Host contract harness: the console level mechanism.
#
# Self contained; build it from anywhere with:
#   make -C <repo>/tests/host/ulog-level-test
#
# One binary, ulog-level-test. It links the vendored engine
# (components/ulog/src/ulog.c) and the shell (os/baremetal/log.c) unchanged, so
# the cases judge the engine's level semantics and the shell's line assembly,
# stamp, repeat gate and never-filtered promotion exactly as the boards run
# them. Two board ends the shell needs come from host_shim.c (the wire is
# captured, the counter is driven); the shim/ directory supplies host spellings
# of the three arch headers the shell includes (arch_asm.h, generic_timer.h,
# uart.h) - the board's versions are aarch64-only.
#
# LOG_SRC lets the wrapper's red control build the same harness against a
# mutated copy of the shell (a copy lives beside its own headers, so the mutation
# is in the family table the promotion reads or in the line discipline itself):
# the same cases must then go red, which is what proves they can fail at all.

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT         := $(abspath $(MAKEFILE_DIR)/../../..)

BUILD_DIR := $(MAKEFILE_DIR)/build
BIN       := $(BUILD_DIR)/ulog-level-test

LOG_SRC ?= $(ROOT)/os/baremetal/log.c

SANITIZE := -fsanitize=address,undefined -fno-sanitize-recover=all \
            -fno-omit-frame-pointer

CFLAGS := -ggdb3 -O1 -std=c11 -Wall -Wextra -ffunction-sections -fdata-sections \
          $(SANITIZE) \
          -DULOG_ENABLED -DULOG_MAX_MESSAGE_LENGTH=256 \
          -I$(MAKEFILE_DIR)/shim \
          -I$(ROOT)/os/baremetal \
          -I$(ROOT)/components/ulog/src \
          -I$(ROOT)

LDFLAGS := -Wl,--gc-sections $(SANITIZE)

SRCS := \
    $(ROOT)/components/ulog/src/ulog.c \
    $(LOG_SRC) \
    $(MAKEFILE_DIR)/host_shim.c \
    $(MAKEFILE_DIR)/main.c

define OBJ
$(BUILD_DIR)/obj_$(subst /,_,$(patsubst $(ROOT)/%,%,$(patsubst $(MAKEFILE_DIR)/%,%,$(1)))).o
endef

OBJS := $(foreach s,$(SRCS),$(call OBJ,$(s)))

.PHONY: all clean

all: $(BIN)

$(BUILD_DIR):
	mkdir -p $@

define COMPILE_RULE
$(call OBJ,$(1)): $(1) | $(BUILD_DIR)
	$$(CC) $$(CFLAGS) -MMD -MP -c $$< -o $$@
endef

$(foreach src,$(SRCS),$(eval $(call COMPILE_RULE,$(src))))

$(BIN): $(OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

# Header dependencies: -MMD -MP writes one .d per object, so a header edit
# rebuilds every object that included it.
-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)

clean:
	rm -rf $(BUILD_DIR)

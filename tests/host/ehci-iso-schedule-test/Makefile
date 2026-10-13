# Host suite: the EHCI isochronous engine against a controller in RAM.
#
# The suite compiles the vendored EHCI port and the patched iso engine as one
# program (the tree tools/patches/replay.sh materialises), with the
# controller's register window living in .bss and the port's platform side
# supplied by host_stubs.c.
#
# The binary is 32-bit on purpose: the port programs 32-bit physical addresses
# into its descriptors and truncates pointers when it links them - the address
# model of the target. A 64-bit host build would link heap addresses the
# controller could never carry (and the truncation would break the chains).
#
# Targets: all (default) | run | clean

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT         := $(abspath $(MAKEFILE_DIR)/../../..)

CU := $(shell sh $(ROOT)/tools/patches/replay.sh cherryusb)
ifeq ($(CU),)
$(error tools/patches/replay.sh returned no source root for cherryusb)
endif

# The engine source is overridable so the red control can point the build at a
# mutated copy (tools/mock/ehci-iso-schedule-test/check.sh).
ISO_SRC   ?= $(CU)/port/ehci/usb_hc_ehci_iso.c
BUILD_DIR ?= $(MAKEFILE_DIR)/build
BIN       := $(BUILD_DIR)/ehci-iso-schedule-test

CFLAGS := -m32 -ggdb3 -O0 -Wall -Wextra -ffunction-sections -fdata-sections \
          -DCONFIG_USB_EHCI_ISO=1 \
          -I$(CU)/common -I$(CU)/core -I$(CU)/class/hub -I$(CU)/port/ehci \
          -I$(ROOT)/ports/cherryusb/board/rk3568-itx

LDFLAGS := -m32 -pthread -Wl,--gc-sections

SRCS := \
    $(CU)/port/ehci/usb_hc_ehci.c \
    $(ISO_SRC) \
    $(MAKEFILE_DIR)/host_stubs.c \
    $(MAKEFILE_DIR)/iso_case.c

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

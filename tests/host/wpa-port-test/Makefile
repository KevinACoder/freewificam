# Host suite: the wpa_supplicant port against vectors and a scripted peer.
#
# Self contained; run it from anywhere with:
#   make -C <repo>/tests/host/wpa-port-test
#
# Two binaries share the same wpa-world sources and differ in the CMSIS-RTOS2
# backend they compile and link against: the port layer must build and run for
# both kernels from one source set (the repository's single-CMSIS-adapter
# rule). Every source that names the facade - the vendored compile set, the
# port layer, the suite - is compiled once per backend tree, exactly as the
# net80211 port test does it.
#
# Targets:
#   all       the suite on the ThreadX and FreeRTOS host runtimes
#   symbols   the port+vendored objects' undefined symbols confined to the
#             documented face
#   cross32   the vendored compile set + port layer syntax-only for
#             arm-none-eabi / cortex-m4 (the 32-bit face)
#   gates     kernel-isolation gates over the port sources
#
# The imported component compiles from the pinned tree as-is, with the
# declared patch replayed (tools/patches/replay.sh prints the patched copy;
# the patch makes the vendored events.c float-free, which is also what lets
# the cross32 pass below be honest).

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT := $(abspath $(MAKEFILE_DIR)/../../..)

WPA   := $(shell sh $(ROOT)/tools/patches/replay.sh wpa_supplicant)
PORT  := $(ROOT)/ports/wpa_supplicant
TX    := $(ROOT)/third-party/threadx
FRK   := $(ROOT)/third-party/FreeRTOS-Kernel
# The CMSIS-FreeRTOS adapter takes the declared patch (64-bit handle
# laundering): the build must compile the replayed copy, not the pinned tree.
ADC   := $(shell sh $(ROOT)/tools/patches/replay.sh CMSIS-FreeRTOS)
CMSIS := $(ROOT)/third-party/CMSIS_6

AARCH64_CROSS_PATH ?= $(HOME)/workspace/env/src/xpack-aarch64-none-elf-gcc-13.2.1-1.1
CROSS_COMPILE ?= $(AARCH64_CROSS_PATH)/bin/aarch64-none-elf-
NM  := $(CROSS_COMPILE)nm
CROSS32 ?= arm-none-eabi-gcc

BUILD := $(MAKEFILE_DIR)/build

# ---- source groups ----------------------------------------------------------

WPA_CORE_SRCS := \
    $(WPA)/src/common/wpa_common.c \
    $(WPA)/src/common/ieee802_11_common.c \
    $(WPA)/src/common/hw_features_common.c \
    $(WPA)/src/drivers/driver_common.c \
    $(WPA)/src/drivers/drivers.c \
    $(WPA)/src/rsn_supp/wpa.c \
    $(WPA)/src/rsn_supp/wpa_ie.c \
    $(WPA)/src/rsn_supp/pmksa_cache.c \
    $(WPA)/src/rsn_supp/preauth.c \
    $(WPA)/src/utils/common.c \
    $(WPA)/src/utils/wpabuf.c \
    $(WPA)/src/utils/base64.c \
    $(WPA)/src/utils/bitfield.c \
    $(WPA)/src/utils/wpa_debug.c \
    $(WPA)/src/crypto/crypto_internal.c \
    $(WPA)/src/crypto/aes-internal.c \
    $(WPA)/src/crypto/aes-internal-dec.c \
    $(WPA)/src/crypto/aes-internal-enc.c \
    $(WPA)/src/crypto/aes-wrap.c \
    $(WPA)/src/crypto/aes-unwrap.c \
    $(WPA)/src/crypto/aes-omac1.c \
    $(WPA)/src/crypto/sha1.c \
    $(WPA)/src/crypto/sha1-internal.c \
    $(WPA)/src/crypto/sha1-prf.c \
    $(WPA)/src/crypto/sha1-pbkdf2.c \
    $(WPA)/src/crypto/md5.c \
    $(WPA)/src/crypto/md5-internal.c \
    $(WPA)/src/crypto/rc4.c \
    $(WPA)/src/crypto/sha256.c \
    $(WPA)/src/crypto/sha256-internal.c \
    $(WPA)/src/crypto/sha256-prf.c \
    $(WPA)/src/crypto/tls_none.c \
    $(WPA)/wpa_supplicant/wpa_supplicant.c \
    $(WPA)/wpa_supplicant/events.c \
    $(WPA)/wpa_supplicant/scan.c \
    $(WPA)/wpa_supplicant/bss.c \
    $(WPA)/wpa_supplicant/config.c \
    $(WPA)/wpa_supplicant/config_none.c \
    $(WPA)/wpa_supplicant/notify.c \
    $(WPA)/wpa_supplicant/wpas_glue.c \
    $(WPA)/wpa_supplicant/bssid_ignore.c \
    $(WPA)/wpa_supplicant/eap_register.c \
    $(WPA)/wpa_supplicant/op_classes.c \
    $(WPA)/wpa_supplicant/rrm.c \
    $(WPA)/wpa_supplicant/robust_av.c

# the vendor backend is the skeleton against the registered contract; the l2
# carrier is the direct one (this suite registers a backend, so the vendored
# none stubs must NOT be linked)
WPA_DRIVER_SRCS := $(PORT)/driver_vendor.c
WPA_L2_SRCS     := $(PORT)/l2_packet_direct.c

PORT_SRCS := \
    $(PORT)/wpa_vendor_port.c \
    $(PORT)/os_port.c \
    $(PORT)/eloop_port.c \
    $(PORT)/supp_main.c

SUITE_SRCS := $(MAKEFILE_DIR)/main.c $(MAKEFILE_DIR)/backend.c

# every source that names the CMSIS-RTOS2 facade is compiled per backend
BACKENDED_SRCS := $(WPA_CORE_SRCS) $(WPA_DRIVER_SRCS) $(WPA_L2_SRCS) \
    $(PORT_SRCS) $(SUITE_SRCS)

# ---- flags -------------------------------------------------------------------

WPA_BACKEND_KEY := -DCONFIG_WPA_BACKEND_VENDOR=1 \
    -DWPA_PORT_DRIVER_NAME='"embox"'

WPA_INC := -I$(PORT)/shim -I$(WPA) -I$(WPA)/src -I$(WPA)/src/utils \
    -I$(WPA)/src/drivers -I$(WPA)/src/l2_packet -I$(WPA)/wpa_supplicant \
    -I$(PORT) -I$(ROOT) -I$(ROOT)/os/cmsis2 -I$(CMSIS)/CMSIS/RTOS2/Include

WPA_CFG := -include stdarg.h -include $(PORT)/wpa_port_config.h \
    $(WPA_BACKEND_KEY) \
    -DCONFIG_WPA_THREAD_STACK=65536 \
    -DCONFIG_WPA_SUPP_DEBUG_LEVEL=2 \
    -Dsscanf=wpa_port_sscanf

COMMON := -std=gnu11 -g3 -O1 -Wall -Wextra -Wno-unused-parameter

# the vendored bytes compile at the level the import allows (-w); the port
# layer and the suite are warning-honest
WPA_TU_CFLAGS := $(COMMON) -w $(WPA_CFG) $(WPA_INC)
PORT_TU_CFLAGS := $(COMMON) -Werror $(WPA_CFG) $(WPA_INC)
SUITE_TU_CFLAGS := $(COMMON) -Werror $(WPA_CFG) $(WPA_INC)

THX_BACKEND := -DCMSIS2_BACKEND_THREADX
FR_BACKEND := -DCMSIS2_BACKEND_FREERTOS \
    -DCMSIS_device_header='"cmsis_host_device.h"' \
    -I$(ADC)/CMSIS/RTOS2/FreeRTOS/Include

# runtime flags (the kernels themselves are backend-specific build products)
TX_RT_CFLAGS := $(COMMON) -w -D_GNU_SOURCE -DTX_LINUX_MULTI_CORE \
    -DTX_INCLUDE_USER_DEFINE_FILE -DCMSIS2_BACKEND_THREADX \
    -I$(ROOT) -I$(ROOT)/os/threadx -I$(ROOT)/os/cmsis2 -I$(ROOT)/os/cmsis2/host-compat \
    -I$(TX)/common/inc -I$(TX)/ports/linux/gnu/inc -I$(CMSIS)/CMSIS/RTOS2/Include
TX_LDFLAGS := -pthread -lrt

FR_RT_CFLAGS := $(COMMON) -w -ffunction-sections -fdata-sections \
    -DCMSIS2_BACKEND_FREERTOS \
    -I$(ROOT) -I$(ROOT)/os/freertos -I$(ROOT)/os/cmsis2 -I$(ROOT)/os/cmsis2/host-compat \
    -I$(FRK)/include -I$(FRK)/portable/ThirdParty/GCC/Posix \
    -I$(FRK)/portable/ThirdParty/GCC/Posix/utils -I$(CMSIS)/CMSIS/RTOS2/Include \
    -I$(ADC)/CMSIS/RTOS2/FreeRTOS/Include \
    -DCMSIS_device_header='"cmsis_host_device.h"' -DFREERTOS_HOOKS_TAG='"wpa-port"'
FR_LDFLAGS := -pthread -Wl,--gc-sections

# ---- object naming (path below the repo root, folded; per backend tree) -------

define OBJ
$(BUILD)/$(1)/$(subst /,_,$(patsubst $(ROOT)/%,%,$(2))).o
endef

THX_OBJS := $(foreach s,$(BACKENDED_SRCS),$(call OBJ,thx,$(s)))
FR_OBJS  := $(foreach s,$(BACKENDED_SRCS),$(call OBJ,fr,$(s)))

TX_RT_SRCS := $(ROOT)/os/threadx/tx_glue.c $(ROOT)/os/threadx/heap.c \
    $(ROOT)/os/threadx/cmsis2_threadx.c $(ROOT)/os/threadx/cmsis2_host.c \
    $(wildcard $(TX)/ports/linux/gnu/src/*.c) $(wildcard $(TX)/common/src/*.c)
TX_RT_OBJS := $(foreach s,$(TX_RT_SRCS),$(call OBJ,txrt,$(s)))

FR_RT_SRCS := $(ROOT)/os/freertos/freertos_hooks.c $(ROOT)/os/freertos/cmsis2_host.c \
    $(FRK)/croutine.c $(FRK)/event_groups.c $(FRK)/list.c $(FRK)/queue.c \
    $(FRK)/stream_buffer.c $(FRK)/tasks.c $(FRK)/timers.c \
    $(FRK)/portable/MemMang/heap_4.c \
    $(FRK)/portable/ThirdParty/GCC/Posix/port.c \
    $(FRK)/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c \
    $(ADC)/CMSIS/RTOS2/FreeRTOS/Source/cmsis_os2.c
FR_RT_OBJS := $(foreach s,$(FR_RT_SRCS),$(call OBJ,frrt,$(s)))

.PHONY: all symbols cross32 gates clean

all: $(BUILD)/wpa-port-test-threadx $(BUILD)/wpa-port-test-freertos

# ---- rules ---------------------------------------------------------------------

$(BUILD)/thx $(BUILD)/fr $(BUILD)/txrt $(BUILD)/frrt:
	mkdir -p $@

# which group a source belongs to decides its warning level
WPA_SET := $(WPA_CORE_SRCS) $(WPA_DRIVER_SRCS) $(WPA_L2_SRCS)
PORT_SET := $(PORT_SRCS)
SUITE_SET := $(SUITE_SRCS)

define THX_RULE
$(call OBJ,thx,$(1)): $(1) | $(BUILD)/thx
	$(CC) $(2) $(THX_BACKEND) -c $$< -o $$@
endef

define FR_RULE
$(call OBJ,fr,$(1)): $(1) | $(BUILD)/fr
	$(CC) $(2) $(FR_BACKEND) -c $$< -o $$@
endef

$(foreach s,$(WPA_SET),$(eval $(call THX_RULE,$(s),$(WPA_TU_CFLAGS))))
$(foreach s,$(PORT_SET),$(eval $(call THX_RULE,$(s),$(PORT_TU_CFLAGS))))
$(foreach s,$(SUITE_SET),$(eval $(call THX_RULE,$(s),$(SUITE_TU_CFLAGS))))
$(foreach s,$(WPA_SET),$(eval $(call FR_RULE,$(s),$(WPA_TU_CFLAGS))))
$(foreach s,$(PORT_SET),$(eval $(call FR_RULE,$(s),$(PORT_TU_CFLAGS))))
$(foreach s,$(SUITE_SET),$(eval $(call FR_RULE,$(s),$(SUITE_TU_CFLAGS))))

define TX_RT_RULE
$(call OBJ,txrt,$(1)): $(1) | $(BUILD)/txrt
	$(CC) $(TX_RT_CFLAGS) -c $$< -o $$@
endef

define FR_RT_RULE
$(call OBJ,frrt,$(1)): $(1) | $(BUILD)/frrt
	$(CC) $(FR_RT_CFLAGS) -c $$< -o $$@
endef

$(foreach s,$(TX_RT_SRCS),$(eval $(call TX_RT_RULE,$(s))))
$(foreach s,$(FR_RT_SRCS),$(eval $(call FR_RT_RULE,$(s))))

$(BUILD)/wpa-port-test-threadx: $(THX_OBJS) $(TX_RT_OBJS)
	$(CC) $^ $(TX_LDFLAGS) -o $@

$(BUILD)/wpa-port-test-freertos: $(FR_OBJS) $(FR_RT_OBJS)
	$(CC) $^ $(FR_LDFLAGS) -o $@

# ---- symbol contract ------------------------------------------------------------

# The port and vendored objects may not reach another kernel's API; their
# undefined symbols must be the CMSIS service prefix, the C library face the
# port documents, and intra-suite symbols.
symbols: $(THX_OBJS)
	@set -e; \
	undef=$$($(NM) -u $(THX_OBJS) 2>/dev/null | sed -n 's/^ \{1,\}U //p' | sort -u); \
	foreign=$$(printf '%s\n' "$$undef" | grep -Ev '^(os[A-Z][A-Za-z0-9_]*|cmsis2_[a-z_]+|wpa_[a-z0-9_]+|wpa_|eloop_[a-z_]+|l2_packet_[a-z_]+|wlan_[a-z_]+|malloc|free|calloc|realloc|memcpy|memset|memmove|memcmp|strlen|strcmp|strncmp|strchr|strstr|strdup|strcasecmp|snprintf|vsnprintf|sprintf|printf|vfprintf|sscanf|strtol|strtoul|qsort|abs|inet_ntop|__[a-z_0-9]+|_[a-z_]+|abort|exit)$$' || true); \
	nfail=0; \
	if [ -n "$$foreign" ]; then \
	    echo "FAIL: unexpected undefined symbols:"; printf '%s\n' "$$foreign"; nfail=1; \
	fi; \
	[ "$$nfail" = 0 ] && echo "symbols: port+vendored undefined symbols confined to the documented face"

# ---- cross32 (the 32-bit face of the vendored set) ------------------------------

# 45 vendored units + the port layer, syntax-only for cortex-m4: the
# repository's "no 64-bit assumptions" rule. Same include order and config
# as the host build; only the target changes.
cross32:
	@set -e; \
	for f in $(WPA_SET) $(PORT_SET); do \
	    out=$$($(CROSS32) -mcpu=cortex-m4 -mthumb -fsyntax-only \
	        $(WPA_CFG) $(WPA_INC) -I$(ROOT)/os/baremetal \
	        -DCMSIS2_BACKEND_THREADX "$$f" 2>&1 | grep 'error:' || true); \
	    if [ -n "$$out" ]; then echo "FAIL: $$f"; echo "$$out"; exit 1; fi; \
	done; \
	echo "cross32: vendored compile set + port layer syntax-clean for cortex-m4"

# ---- isolation gates --------------------------------------------------------------

gates:
	PATH=$(dir $(CROSS_COMPILE)):$$PATH \
	sh $(ROOT)/tools/gates/check-kernel-isolation.sh \
	    --stack-objs "$(BUILD)/thx" --port-objs "$(BUILD)/thx" \
	    --sources "$(PORT)" --root "$(ROOT)" --canary

clean:
	rm -rf $(BUILD)

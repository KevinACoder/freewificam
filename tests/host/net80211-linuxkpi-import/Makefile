# Import build contract: net80211, linuxkpi and skbuff.
#
# Self contained; run it from anywhere with:
#   make -C <repo>/tests/host/net80211-linuxkpi-import
#
# Everything under components/ is imported content and is consumed as-is (no local
# edits, no patches). Nothing here links or runs production code: every target is a
# compile-level contract. The only executed suite is the net80211 component's own
# host test script (net80211-host-tests).

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT         := $(abspath $(MAKEFILE_DIR)/../../..)

NET80211 := $(ROOT)/components/net80211
LKPI     := $(ROOT)/components/linuxkpi
SKBUFF   := $(ROOT)/components/skbuff
BUILD_DIR := $(MAKEFILE_DIR)/build

CC ?= cc

# ---- net80211 frozen stack compile set (compile-only) ------------------------
#
# 17 translation units: the stack list of the component's own port/Makefile
# (SRCS_BSD minus its driver/command line) plus the crypto set and their port
# shim. Flags are that Makefile's host subset: CFG_BSD (-D_KERNEL,
# -D_COMPAT_SYS_SYSCTL_H_, forced port_config_bsd.h) and INC_NET (component root,
# the FreeRTOS OSAL compat headers) with "-idirafter compat/netbsd" kept so the
# NetBSD shadow headers fill only the gaps the host libc leaves. Board-only flags
# (-march, -fno-builtin, the newlib stdarg include) are dropped; -DIWM_DEBUG only
# adds an iwm driver trace and no iwm source is in this set, so it is dropped too.
#
# Two host adaptations on top of that flag set:
#   * tests/include/ comes first: the component's own host stand-ins for the OSAL
#     bits (hal/ipl.h, mem/sysmalloc.h) are what its host test script uses, and the
#     FreeRTOS OSAL headers are the target environment, not the host one.
#   * net80211_host_preempt.h claims the glibc guards for the headers the compat
#     tree provides (see the header for why). Both are compile-contract plumbing;
#     the component sources are untouched.
# Two upstream flag relaxations are kept because the imported units trip them:
# -Wno-unused-function (the subset leaves registration helpers unreferenced) and
# -Wno-pointer-sign (the imported code casts char pointers); the target build
# relaxes the same two among others. -include stdarg.h is the host counterpart of
# the target build's newlib stdarg include. The preemption header also drops the
# handful of macros the compat headers redefine with equivalent values, so the set
# compiles warning-free.
NET80211_SRCS := \
    net80211/ieee80211.c \
    net80211/ieee80211_proto.c \
    net80211/ieee80211_node.c \
    net80211/ieee80211_output.c \
    net80211/ieee80211_input.c \
    net80211/ieee80211_amrr.c \
    net80211/ieee80211_netbsd.c \
    net80211/ieee80211_crypto.c \
    net80211/ieee80211_crypto_none.c \
    net80211/ieee80211_crypto_ccmp.c \
    crypto/aes/aes_bear.c \
    crypto/aes/aes_ct.c \
    crypto/aes/aes_ct_enc.c \
    crypto/aes/aes_ct_dec.c \
    crypto/aes/aes_ccm.c \
    crypto/aes/aes_ccm_mbuf.c \
    port/aes_impl_compat.c

NET80211_CFLAGS := -std=gnu11 -Wall -Wno-unused-function -Wno-pointer-sign \
    -D_KERNEL -D_COMPAT_SYS_SYSCTL_H_ -include stdarg.h \
    -include $(MAKEFILE_DIR)/net80211_host_preempt.h \
    -include $(NET80211)/port/port_config_bsd.h \
    -I$(NET80211) -I$(NET80211)/tests/include \
    -I$(NET80211)/port/osal/freertos/compat \
    -idirafter $(NET80211)/compat/netbsd

NET80211_OBJS := $(addprefix $(BUILD_DIR)/,$(addsuffix .o,$(basename $(notdir $(NET80211_SRCS)))))

# ---- linuxkpi contract-header closure ---------------------------------------
#
# The contract translation unit includes <linux/usb.h> and nothing else; the five
# header files it closes over live in components/linuxkpi/. The USB engine group is
# deliberately not compiled: its includes (CherryUSB headers, cmsis_os2_ext.h,
# generic_timer.h, tx_api.h) are not in this repository.
LKPI_CFLAGS := -std=gnu11 -Wall -I$(LKPI)/include -I$(LKPI)

# ---- skbuff ------------------------------------------------------------------
#
# The two self-contained headers of the imported TizenRT unit. freertos/wrapper.h
# is not syntax-checked standalone: it includes wireless.h and osdep_service.h,
# which are outside that unit.
SKBUFF_HEADERS := $(SKBUFF)/skbuff.h $(SKBUFF)/freertos/freertos_skbuff.h

.PHONY: all net80211-compile net80211-manifest net80211-host-tests \
        lkpi-contract lkpi-closure skbuff-headers clean

all: net80211-compile net80211-manifest lkpi-contract lkpi-closure skbuff-headers

define NET80211_RULE
$(BUILD_DIR)/$(notdir $(1:.c=.o)): $(NET80211)/$(1) | $(BUILD_DIR)
	$(CC) $(NET80211_CFLAGS) -c $$< -o $$@
endef

$(foreach src,$(NET80211_SRCS),$(eval $(call NET80211_RULE,$(src))))

net80211-compile: $(NET80211_OBJS)
	@echo "net80211-compile: $(words $(NET80211_OBJS)) translation units compiled (-c only, no link, no run)"

# Byte-identity of the vendored component against its pin: 134 files, sha256.
net80211-manifest:
	@cd $(NET80211) && sha256sum -c --quiet MANIFEST.sha256 && \
	    echo "net80211-manifest: $$(wc -l < MANIFEST.sha256) files match MANIFEST.sha256"

# The component's own host test script, run as-is (zero edits, zero wrapping).
net80211-host-tests:
	@sh $(NET80211)/tests/run-host-tests.sh && echo "net80211-host-tests: component host tests passed"

lkpi-contract: | $(BUILD_DIR)
	$(CC) $(LKPI_CFLAGS) -c $(MAKEFILE_DIR)/lkpi_contract_tu.c -o $(BUILD_DIR)/lkpi_contract_tu.o
	@echo "lkpi-contract: contract translation unit compiled"

# The include closure must be the in-repo five headers plus libc only.
lkpi-closure: | $(BUILD_DIR)
	$(CC) $(LKPI_CFLAGS) -H -fsyntax-only $(MAKEFILE_DIR)/lkpi_contract_tu.c 2> $(BUILD_DIR)/lkpi-closure.raw
	@grep -E '^\.+ ' $(BUILD_DIR)/lkpi-closure.raw | sed -e 's/^\.* //' | sort -u > $(BUILD_DIR)/lkpi-closure.txt
	@grep -v -e '/usr/' -e 'components/linuxkpi' -e 'lkpi_contract_tu.c' \
	    $(BUILD_DIR)/lkpi-closure.txt > $(BUILD_DIR)/lkpi-closure-outside.txt || true
	@if [ -s $(BUILD_DIR)/lkpi-closure-outside.txt ]; then \
	    echo "lkpi-closure: FAILED - headers outside the repository closure:"; \
	    cat $(BUILD_DIR)/lkpi-closure-outside.txt; exit 1; \
	fi
	@echo "lkpi-closure: $$(wc -l < $(BUILD_DIR)/lkpi-closure.txt) headers, all in-repo closure or libc"

skbuff-headers: | $(BUILD_DIR)
	@for h in $(SKBUFF_HEADERS); do \
	    $(CC) -x c -std=gnu11 -Wall -fsyntax-only -I$(SKBUFF) $$h || exit 1; \
	done
	@echo "skbuff-headers: both self-contained headers pass -fsyntax-only"

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

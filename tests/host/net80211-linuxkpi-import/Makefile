# Import build contract: linuxkpi and skbuff.
#
# Self contained; run it from anywhere with:
#   make -C <repo>/tests/host/net80211-linuxkpi-import
#
# Everything under components/ is imported content and is consumed as-is (no local
# edits, no patches). Nothing here links or runs production code: every target is a
# compile-level contract.
#
# The net80211 targets this directory used to carry (a 17-TU compile set on the
# component's own compat tree, a manifest check and the component's host test script)
# are retired: the component is trimmed and its build contract moved to
# tests/host/net80211-port/, which compiles the same units on the port layer. The two
# targets kept here belong to the other two imported units and were not in that scope.

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT         := $(abspath $(MAKEFILE_DIR)/../../..)

LKPI     := $(ROOT)/components/linuxkpi
SKBUFF   := $(ROOT)/components/skbuff
BUILD_DIR := $(MAKEFILE_DIR)/build

CC ?= cc

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

.PHONY: all lkpi-contract lkpi-closure skbuff-headers clean

all: lkpi-contract lkpi-closure skbuff-headers

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

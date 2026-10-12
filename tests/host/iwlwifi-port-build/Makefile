# Static build gates for the vendored iwlwifi driver and its first-party header face.
#
# Self contained; run it from anywhere with:
#   make -C <repo>/tests/host/iwlwifi-port-build
#
# The driver is vendored bytes and the PCI/DMA/interrupt and mac80211 header faces
# it compiles against are this project's own, so the checks split the same way:
#
#   compile    the 63 translation units of the 7260 (mvm) path compile for aarch64
#              against the first-party face, at the warning level the driver's own
#              build takes
#   contract   the face headers compile as one translation unit on their own, at a
#              stricter warning level than the driver's, so the face cannot hide
#              behind the bytes it serves
#   spdx       the license split is mechanical: every extracted file carries one of
#              the three SPDX forms the tree actually uses (with one recorded
#              exception), the first-party face carries the project's, and no
#              license text is copied from elsewhere
#   gates      the kernel-isolation gates over the driver objects (symbol gate) and
#              the first-party face sources, with their canary proofs
#
# Warning level, and why it is what it is: the driver units are compiled with -Wall
# plus -Wextra and the warnings the kernel's own default set switches off again
# (scripts/Makefile.warn) - a driver's bytes are compiled here at exactly the level
# their own build takes, so a warning the kernel tolerates is not turned into a
# gate failure while a warning the kernel would report still fails.

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT  := $(abspath $(MAKEFILE_DIR)/../../..)
DRV   := $(ROOT)/third-party/iwlwifi
FACE  := $(ROOT)/components/linuxkpi/include
BUILD := $(MAKEFILE_DIR)/build

# A space, used to turn the allowlisted warning sites into a grep alternation.
empty :=
space := $(empty) $(empty)

AARCH64_CROSS_PATH ?= $(HOME)/workspace/env/src/xpack-aarch64-none-elf-gcc-13.2.1-1.1
CROSS_COMPILE ?= $(AARCH64_CROSS_PATH)/bin/aarch64-none-elf-
CC  := $(CROSS_COMPILE)gcc

# The 7260 (mvm) translation units: the core half, the firmware half, the PCIe
# transport, the one configuration family the part belongs to, and the mvm units the
# kernel builds for CONFIG_IWLMVM. This is the closure the driver's own Makefiles
# name (iwlwifi/Makefile, mvm/Makefile), minus the units the chip selection does not
# reach: the dvm generation, mei, tests, the other configuration families, and the
# DEBUGFS / LEDS / PM_SLEEP / IWLMEI-conditional units.
SRCS := \
    $(DRV)/iwl-drv.c $(DRV)/iwl-debug.c $(DRV)/iwl-io.c \
    $(DRV)/iwl-nvm-parse.c $(DRV)/iwl-nvm-utils.c $(DRV)/iwl-phy-db.c \
    $(DRV)/iwl-trans.c $(DRV)/iwl-utils.c $(DRV)/iwl-dbg-tlv.c \
    $(DRV)/fw/img.c $(DRV)/fw/notif-wait.c $(DRV)/fw/rs.c $(DRV)/fw/dbg.c \
    $(DRV)/fw/dbg-old.c $(DRV)/fw/pnvm.c $(DRV)/fw/dump.c \
    $(DRV)/fw/regulatory.c $(DRV)/fw/paging.c $(DRV)/fw/smem.c $(DRV)/fw/init.c \
    $(DRV)/pcie/ctxt-info.c $(DRV)/pcie/ctxt-info-v2.c $(DRV)/pcie/drv.c \
    $(DRV)/pcie/utils.c $(DRV)/pcie/gen1_2/rx.c $(DRV)/pcie/gen1_2/tx.c \
    $(DRV)/pcie/gen1_2/trans.c $(DRV)/pcie/gen1_2/trans-gen2.c \
    $(DRV)/pcie/gen1_2/tx-gen2.c \
    $(DRV)/cfg/7000.c \
    $(DRV)/mvm/fw.c $(DRV)/mvm/mac80211.c $(DRV)/mvm/nvm.c $(DRV)/mvm/ops.c \
    $(DRV)/mvm/phy-ctxt.c $(DRV)/mvm/mac-ctxt.c $(DRV)/mvm/utils.c \
    $(DRV)/mvm/rx.c $(DRV)/mvm/rxmq.c $(DRV)/mvm/tx.c $(DRV)/mvm/binding.c \
    $(DRV)/mvm/quota.c $(DRV)/mvm/sta.c $(DRV)/mvm/sf.c $(DRV)/mvm/scan.c \
    $(DRV)/mvm/time-event.c $(DRV)/mvm/rs.c $(DRV)/mvm/rs-fw.c \
    $(DRV)/mvm/power.c $(DRV)/mvm/coex.c $(DRV)/mvm/tt.c $(DRV)/mvm/offloading.c \
    $(DRV)/mvm/tdls.c $(DRV)/mvm/ftm-responder.c $(DRV)/mvm/ftm-initiator.c \
    $(DRV)/mvm/rfi.c $(DRV)/mvm/mld-key.c $(DRV)/mvm/mld-mac.c $(DRV)/mvm/link.c \
    $(DRV)/mvm/mld-sta.c $(DRV)/mvm/mld-mac80211.c $(DRV)/mvm/ptp.c \
    $(DRV)/mvm/time-sync.c

ARCH   := -mcpu=cortex-a55 -mgeneral-regs-only -mstrict-align -mno-outline-atomics
COMMON := -std=gnu11 -ffreestanding -fno-builtin -fno-stack-protector -g3 -O2 \
    -ffunction-sections -fdata-sections
INC    := -I$(FACE) -I$(DRV) -I$(ROOT)/components/ulog/src
# The kernel's Kconfig resolves CONFIG_IWLWIFI and CONFIG_IWLMVM for this part; the
# conditional features (DEBUGFS, LEDS, PM_SLEEP, IWLMEI, ACPI, EFI) are off, which is
# why their units are not in the list above.
DRV_CONFIG := -DCONFIG_IWLWIFI -DCONFIG_IWLMVM
DEPFLAGS := -MMD -MP

DRV_WARN    := -Wall -Wextra -Wno-address-of-packed-member -Wno-maybe-uninitialized \
    -Wno-missing-field-initializers -Wno-shift-negative-value -Wno-sign-compare \
    -Wno-unused-parameter
DRV_CFLAGS  := $(COMMON) $(DRV_CONFIG) $(DRV_WARN)
FACE_CFLAGS := $(COMMON) -Wall -Wextra

.PHONY: all compile contract spdx gates clean

all: compile contract spdx gates

# ---- compile: the vendored bytes against the first-party face ----------------

# Object names keep the source path, because the tree has same-named units in
# different directories (rx.c, tx.c, rs.c each appear twice).
define OBJ
$(BUILD)/$(subst /,_,$(patsubst $(DRV)/%,%,$(1))).o
endef

OBJS := $(foreach s,$(SRCS),$(call OBJ,$(s)))

# Warning sites that are recorded and allowlisted by file and line. One today: the
# pinned imported header's __stringify redefinition, which is recorded in
# components/linuxkpi/PROVENANCE.md ("Known warnings of the imported headers") and
# is the same site the sibling port gates allowlist. Any other warning - in
# particular any warning in the vendored driver's bytes or in a first-party face
# file - still fails the gate.
KNOWN_WARN := sysfs.h:47

compile: $(OBJS)
	@set -e; \
	logs=$$(cat $(BUILD)/*.compile.log 2>/dev/null || true); \
	warn=$$(printf '%s\n' "$$logs" | grep -c 'warning:' || true); \
	known=0; \
	if [ -n "$(KNOWN_WARN)" ]; then known=$$(printf '%s\n' "$$logs" | grep 'warning:' | grep -cE "$(subst $(space),|,$(KNOWN_WARN))" || true); fi; \
	echo "compile: $(words $(SRCS)) translation units, aarch64, warning lines: $$warn ($$known known)"; \
	if [ "$$warn" != "$$known" ]; then \
		echo "FAIL: warning outside the recorded known set:"; \
		printf '%s\n' "$$logs" | grep 'warning:' | grep -vE "$(subst $(space),|,$(KNOWN_WARN))" || true; \
		exit 1; \
	fi

define COMPILE_RULE
$(call OBJ,$(1)): $(1) | $(BUILD)
	@set -e; \
	$$(CC) $$(ARCH) $$(DRV_CFLAGS) $$(DEPFLAGS) $$(INC) -c $$< -o $$@ 2> $$(BUILD)/$$(subst /,_,$$(patsubst $$(DRV)/%,%,$$(1))).compile.log || \
		{ cat $$(BUILD)/$$(subst /,_,$$(patsubst $$(DRV)/%,%,$$(1))).compile.log; exit 1; }
endef

$(foreach src,$(SRCS),$(eval $(call COMPILE_RULE,$(src))))

# ---- contract: the face on its own -------------------------------------------

contract: $(BUILD)/face_contract.o
	@set -e; \
	log=$$(cat $(BUILD)/face_contract.compile.log 2>/dev/null || true); \
	warn=$$(printf '%s\n' "$$log" | grep -c 'warning:' || true); \
	known=$$(printf '%s\n' "$$log" | grep 'warning:' | grep -cE "$(subst $(space),|,$(KNOWN_WARN))" || true); \
	echo "contract: the face compiles standalone at -Wall -Wextra, warning lines: $$warn ($$known known, allowlisted)"; \
	if [ "$$warn" != "$$known" ]; then \
		echo "FAIL: the first-party face must compile without warnings of its own:"; \
		printf '%s\n' "$$log" | grep 'warning:' | grep -vE "$(subst $(space),|,$(KNOWN_WARN))"; \
		exit 1; \
	fi

$(BUILD)/face_contract.o: $(MAKEFILE_DIR)/face_contract.c | $(BUILD)
	@set -e; \
	$(CC) $(ARCH) $(FACE_CFLAGS) $(DEPFLAGS) $(INC) -c $< -o $@ 2> $(BUILD)/face_contract.compile.log || \
		{ cat $(BUILD)/face_contract.compile.log; exit 1; }

-include $(wildcard $(BUILD)/*.d)

# ---- spdx: the license split, mechanically ------------------------------------

# The tree mixes three SPDX forms; two files carry none, recorded here by path so
# any other unmarked file fails. Both are outside the 7260 closure (a test-mode
# header and the test directory's build file).
NO_SPDX := mvm/testmode.h mvm/tests/Makefile

# Two pinned imported headers carry no SPDX line of their own (they are covered by
# the component's provenance record and editing them is not allowed); the face scan
# exempts exactly those, by path.
FACE_PINNED_NO_SPDX := $(FACE)/linux/device.h $(FACE)/linux/sysfs.h

spdx:
	@set -e; \
	if [ ! -f "$(DRV)/iwl-drv.c" ]; then echo "FAIL: the driver tree is not checked out"; exit 1; fi; \
	extract="$$(find $(DRV) -name '*.c' -o -name '*.h' -o -name 'Makefile' -o -name 'Kconfig' | LC_ALL=C sort)"; \
	total=$$(printf '%s\n' $$extract | wc -l); \
	none=$$(grep -L 'SPDX-License-Identifier' $$extract | sed "s|^$(DRV)/||" || true); \
	unexpected=$$(printf '%s\n' "$$none" | grep -vE '^($(subst $(space),|,$(NO_SPDX)))$$' | grep -v '^$$' || true); \
	if [ -n "$$unexpected" ]; then echo "FAIL: extracted file without an SPDX line (not a recorded one):"; printf '%s\n' "$$unexpected"; exit 1; fi; \
	forms=$$(grep -h 'SPDX-License-Identifier' $$extract | sed -e 's/.*SPDX-License-Identifier: //' -e 's/[[:space:]]*\*\/[[:space:]]*$$//' -e 's/[[:space:]]*$$//' | LC_ALL=C sort -u); \
	bad=$$(printf '%s\n' "$$forms" | grep -vE '^(GPL-2.0 OR BSD-3-Clause|GPL-2.0-only|GPL-2.0)$$' || true); \
	if [ -n "$$bad" ]; then echo "FAIL: an SPDX form the tree did not have before:"; printf '%s\n' "$$bad"; exit 1; fi; \
	face="$$(find $(FACE)/linux $(FACE)/net -name '*.h' | grep -vE '$(subst $(space),|,$(FACE_PINNED_NO_SPDX))')"; \
	fbad=$$(grep -L 'SPDX-License-Identifier: BSD-3-Clause' $$face || true); \
	if [ -n "$$fbad" ]; then echo "FAIL: first-party face file without the project SPDX:"; printf '%s\n' "$$fbad"; exit 1; fi; \
	fother=$$(grep -h 'SPDX-License-Identifier' $$face | sort -u | grep -v 'BSD-3-Clause' || true); \
	if [ -n "$$fother" ]; then echo "FAIL: a non-project SPDX form in the face:"; printf '%s\n' "$$fother"; exit 1; fi; \
	copy=$$(grep -l 'GNU General Public' $$face || true); \
	if [ -n "$$copy" ]; then echo "FAIL: license text copied into the first-party face:"; printf '%s\n' "$$copy"; exit 1; fi; \
	echo "spdx: $$total extracted files, $$(printf '%s\n' $$forms | wc -l) SPDX forms + 1 recorded unmarked; $$(printf '%s\n' $$face | wc -l) face files BSD-3-Clause with no copied license text"

# ---- gates: kernel isolation over the driver objects and the face sources -----

gates: $(OBJS)
	PATH=$(dir $(CROSS_COMPILE)):$$PATH \
	sh $(ROOT)/tools/gates/check-kernel-isolation.sh \
	    --root "$(ROOT)" \
	    --stack-objs "$(BUILD)" \
	    --sources "$(FACE) $(MAKEFILE_DIR)" \
	    --canary-src "$(MAKEFILE_DIR)" \
	    --canary

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

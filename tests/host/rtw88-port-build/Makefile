# Static build gates for the vendored rtw88 driver and its first-party header face.
#
# Self contained; run it from anywhere with:
#   make -C <repo>/tests/host/rtw88-port-build
#
# The driver is vendored bytes and the mac80211/cfg80211 header face it compiles
# against is this project's own, so the checks split the same way:
#
#   compile    the 8821CU translation units compile for aarch64 against the
#              first-party face, at the warning level the driver's own build takes
#   contract   the face headers compile as one translation unit on their own, at a
#              stricter warning level than the driver's, so the face cannot hide
#              behind the bytes it serves
#   spdx       the license split is mechanical: the extracted files carry exactly one
#              SPDX form (GPL-2.0 OR BSD-3-Clause, plus the Kconfig metadata file), the
#              first-party face carries the project's (BSD-3-Clause) and no license text
#              copied from elsewhere
#   gates      the kernel-isolation gates over the driver objects (symbol gate) and
#              the first-party face sources, with their canary proofs
#
# Warning level, and why it is what it is: the driver units are compiled at the warning
# level the kernel's own build of this directory takes. That is -Wall -Wextra plus the
# switches the kernel's default warning set turns back off for every directory
# (scripts/Makefile.warn): -Wno-type-limits ("too noisy on range checks and in macros
# handling both signed and unsigned") and -Wno-pointer-sign ("disable pointer signed /
# unsigned warnings in gcc 4.0"), plus the per-directory set the rtw89 gate already
# carries (-Wno-address-of-packed-member for the driver's packed descriptor records, and
# the maybe-uninitialized / missing-field-initializers / shift-negative-value /
# sign-compare / unused-parameter relaxations). The kernel also compiles with
# -fno-strict-aliasing (top-level Makefile), which is a code-generation flag and is kept
# here for the same reason. Everything else stays on: a warning in the driver's own bytes
# or in the first-party face fails this gate, and warnings are counted rather than
# escalated, so a regression shows up as a count in the log.
#
# One warning site is known and allowlisted by file and line: sysfs.h redefining
# __stringify, which sits in the pinned imported header this component consumes
# byte-identically (the record is components/linuxkpi/PROVENANCE.md, "Known warnings
# of the imported headers"). It is the same single site the rtw89 gate allowlists. Any
# other warning - in particular any warning in the vendored driver's bytes or in the
# first-party face - still fails the gate.

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT  := $(abspath $(MAKEFILE_DIR)/../../..)
DRV   := $(ROOT)/third-party/rtw88
FACE  := $(ROOT)/components/linuxkpi/include
BUILD := $(MAKEFILE_DIR)/build

# A space, used to turn the allowlisted warning sites into a grep alternation.
empty :=
space := $(empty) $(empty)

AARCH64_CROSS_PATH ?= $(HOME)/workspace/env/src/xpack-aarch64-none-elf-gcc-13.2.1-1.1
CROSS_COMPILE ?= $(AARCH64_CROSS_PATH)/bin/aarch64-none-elf-
CC  := $(CROSS_COMPILE)gcc

# The 8821CU translation units: the core half, the chip half and the single bus unit.
# This is the set the kernel builds for CONFIG_RTW88_8821CU (minus the units the chip
# selection does not reach: the PCI and SDIO transports, the other chips, and the shared
# rtw8723x unit the RTL8821C selection does not pull in).
SRCS := $(DRV)/main.c $(DRV)/mac80211.c $(DRV)/util.c $(DRV)/debug.c $(DRV)/tx.c \
    $(DRV)/rx.c $(DRV)/mac.c $(DRV)/phy.c $(DRV)/coex.c $(DRV)/efuse.c \
    $(DRV)/fw.c $(DRV)/ps.c $(DRV)/sec.c $(DRV)/bf.c $(DRV)/sar.c \
    $(DRV)/regd.c $(DRV)/rtw8821c.c $(DRV)/rtw8821c_table.c $(DRV)/rtw8821cu.c \
    $(DRV)/usb.c

ARCH   := -mcpu=cortex-a55 -mgeneral-regs-only -mstrict-align -mno-outline-atomics
COMMON := -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -g3 -O2 \
    -ffunction-sections -fdata-sections -fno-strict-aliasing
INC    := -I$(FACE) -I$(DRV)
# The chip and bus selection the kernel's Kconfig resolves for the 8821CU.
DRV_CONFIG := -DCONFIG_RTW88_CORE -DCONFIG_RTW88_8821C -DCONFIG_RTW88_USB \
    -DCONFIG_RTW88_8821CU
# Header dependencies are tracked: the face is edited far more often than the driver
# bytes, and a stale object would report the previous header's warnings.
DEPFLAGS := -MMD -MP

# The kernel's own default set for a driver directory (scripts/Makefile.warn): -Wall
# plus -Wextra, with the warnings its default build switches off again - a driver's
# bytes are compiled here at exactly the level their own build takes.
DRV_WARN    := -Wall -Wextra -Wno-address-of-packed-member -Wno-maybe-uninitialized \
    -Wno-missing-field-initializers -Wno-shift-negative-value -Wno-sign-compare \
    -Wno-unused-parameter -Wno-type-limits -Wno-pointer-sign
DRV_CFLAGS  := $(COMMON) $(DRV_CONFIG) $(DRV_WARN)
FACE_CFLAGS := $(COMMON) -Wall -Wextra

.PHONY: all compile contract closure spdx gates clean

all: compile contract closure spdx gates

# ---- compile: the vendored bytes against the first-party face ----------------

OBJS := $(foreach s,$(SRCS),$(BUILD)/$(notdir $(basename $(s))).o)

# The interface's own record of the pinned imported headers' one known warning site;
# matched by file and line so a different warning in the same file still counts.
KNOWN_WARN := sysfs.h:47

# The two pinned imported headers carry no SPDX line of their own (they are covered by
# the component's provenance record, and editing them is not allowed); the license scan
# exempts exactly those, by path.
FACE_PINNED_NO_SPDX := $(FACE)/linux/device.h $(FACE)/linux/sysfs.h

compile: $(OBJS)
	@set -e; \
	logs=$$(cat $(BUILD)/*.compile.log 2>/dev/null || true); \
	warn=$$(printf '%s\n' "$$logs" | grep -c 'warning:' || true); \
	known=$$(printf '%s\n' "$$logs" | grep 'warning:' | grep -cE "$(subst $(space),|,$(KNOWN_WARN))" || true); \
	echo "compile: $(words $(SRCS)) translation units, aarch64, warning lines: $$warn ($$known known, allowlisted by file and line)"; \
	if [ "$$warn" != "$$known" ]; then \
		echo "FAIL: warning outside the recorded known set:"; \
		printf '%s\n' "$$logs" | grep 'warning:' | grep -vE "$(subst $(space),|,$(KNOWN_WARN))"; \
		exit 1; \
	fi

$(BUILD)/%.o: $(DRV)/%.c | $(BUILD)
	@set -e; \
	$(CC) $(ARCH) $(DRV_CFLAGS) $(DEPFLAGS) $(INC) -c $< -o $@ 2> $(BUILD)/$*.compile.log || \
		{ cat $(BUILD)/$*.compile.log; exit 1; }

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

# ---- closure: what the driver's bytes need from the rest of the system --------
#
# The compile gate is per translation unit, so it cannot see a symbol the driver
# calls that nothing defines - that only shows up when the units are put
# together. This gate links them relocatably (no garbage collection, so nothing
# is dropped before it can be counted) and checks the undefined set against the
# checked-in ledger face-undefined.txt. A new entry is a face gap; a removed one
# is a face change. Either way the ledger has to be updated deliberately, which
# is the point: the rtw88 round found two face gaps this way that the compile
# gate had passed (ieee80211_request_smps and ieee80211_proberesp_get - declared
# in the face, never defined).
LD := $(CROSS_COMPILE)ld
NM := $(CROSS_COMPILE)nm

closure: $(OBJS)
	@set -e; \
	$(LD) -r -o $(BUILD)/rtw88-all.o $(OBJS); \
	$(NM) -u $(BUILD)/rtw88-all.o | awk '{print $$NF}' | LC_ALL=C sort -u > $(BUILD)/undefined.txt; \
	if ! diff -u $(MAKEFILE_DIR)/face-undefined.txt $(BUILD)/undefined.txt > $(BUILD)/closure.diff; then \
		echo "FAIL: the driver's undefined-symbol set changed (a face gap, or a face change):"; \
		cat $(BUILD)/closure.diff; \
		echo "(if the change is intended, update $(MAKEFILE_DIR)/face-undefined.txt)"; \
		exit 1; \
	fi; \
	echo "closure: $$(wc -l < $(BUILD)/undefined.txt) undefined symbols, exactly the recorded face ledger"

# ---- spdx: the license split, mechanically ------------------------------------

spdx:
	@set -e; \
	if [ -z "$(DRV)/main.c" ]; then echo "FAIL: the driver tree is not checked out"; exit 1; fi; \
	extract="$$(printf '%s\n' $(DRV)/*.c $(DRV)/*.h $(DRV)/Makefile)"; \
	total=$$(printf '%s\n' $$extract | wc -l); \
	n=$$(grep -l 'SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause' $$extract | wc -l); \
	if [ "$$n" != "$$total" ]; then echo "FAIL: extracted files with the dual SPDX: $$n of $$total"; exit 1; fi; \
	extra=$$(grep -h 'SPDX-License-Identifier' $$extract | sort -u | grep -v 'GPL-2.0 OR BSD-3-Clause' || true); \
	if [ -n "$$extra" ]; then echo "FAIL: a second SPDX form in the extracted sources:"; printf '%s\n' "$$extra"; exit 1; fi; \
	kconfig=$$(grep -h 'SPDX-License-Identifier' $(DRV)/Kconfig | sort -u); \
	if [ "$$kconfig" != '# SPDX-License-Identifier: GPL-2.0-only' ]; then echo "FAIL: Kconfig is not the expected single GPL-2.0-only metadata file: $$kconfig"; exit 1; fi; \
	face="$$(find $(FACE)/linux $(FACE)/net $(FACE)/asm -name '*.h' | grep -vE '$(subst $(space),|,$(FACE_PINNED_NO_SPDX))')"; \
	bad=$$(grep -L 'SPDX-License-Identifier: BSD-3-Clause' $$face || true); \
	if [ -n "$$bad" ]; then echo "FAIL: first-party face file without the project SPDX:"; printf '%s\n' "$$bad"; exit 1; fi; \
	other=$$(grep -h 'SPDX-License-Identifier' $$face | sort -u | grep -v 'BSD-3-Clause' || true); \
	if [ -n "$$other" ]; then echo "FAIL: a non-project SPDX form in the face:"; printf '%s\n' "$$other"; exit 1; fi; \
	copy=$$(grep -l 'GNU General Public' $$face || true); \
	if [ -n "$$copy" ]; then echo "FAIL: license text copied into the first-party face:"; printf '%s\n' "$$copy"; exit 1; fi; \
	echo "spdx: $$n/$$total extracted files dual-licensed with no second form; Kconfig GPL-2.0-only metadata; $$(printf '%s\n' $$face | wc -l) face files BSD-3-Clause with no copied license text"

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

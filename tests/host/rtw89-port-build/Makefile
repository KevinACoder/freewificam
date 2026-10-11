# Static build gates for the vendored rtw89 driver and its first-party header face.
#
# Self contained; run it from anywhere with:
#   make -C <repo>/tests/host/rtw89-port-build
#
# The driver is vendored bytes and the mac80211/cfg80211 header face it compiles
# against is this project's own, so the checks split the same way:
#
#   compile    the 8851BU translation units compile for aarch64 against the
#              first-party face, at the warning level the driver's own build takes
#   contract   the face headers compile as one translation unit on their own, at a
#              stricter warning level than the driver's, so the face cannot hide
#              behind the bytes it serves
#   spdx       the license split is mechanical: the extracted files carry exactly one
#              SPDX form (GPL-2.0 OR BSD-3-Clause), the first-party face carries the
#              project's (BSD-3-Clause) and no license text copied from elsewhere
#   gates      the kernel-isolation gates over the driver objects (symbol gate) and
#              the first-party face sources, with their canary proofs
#
# Warning level, and why it is what it is: the driver units are compiled with -Wall
# plus the one switch-off the kernel's own build applies to this directory
# (scripts/Makefile.extrawarn: -Wno-address-of-packed-member - the driver takes the
# address of members of its packed descriptor records, which the kernel build
# tolerates rather than asks the driver to change). Everything else stays on: a
# warning in the driver's own bytes or in the first-party face fails this gate, and
# warnings are counted rather than escalated, so a regression shows up as a count in
# the log.
#
# One warning site is known and allowlisted by file and line: sysfs.h redefining
# __stringify, which sits in the pinned imported header this component consumes
# byte-identically (the record is components/linuxkpi/PROVENANCE.md, "Known warnings
# of the imported headers"). Any other warning - in particular any warning in the
# vendored driver's bytes or in the first-party face - still fails the gate.

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT  := $(abspath $(MAKEFILE_DIR)/../../..)
DRV   := $(ROOT)/third-party/rtw89
FACE  := $(ROOT)/components/linuxkpi/include
BUILD := $(MAKEFILE_DIR)/build

# A space, used to turn the allowlisted warning sites into a grep alternation.
empty :=
space := $(empty) $(empty)

AARCH64_CROSS_PATH ?= $(HOME)/workspace/env/src/xpack-aarch64-none-elf-gcc-13.2.1-1.1
CROSS_COMPILE ?= $(AARCH64_CROSS_PATH)/bin/aarch64-none-elf-
CC  := $(CROSS_COMPILE)gcc

# The 8851BU translation units: the core half, the chip half and the two bus units.
# This is the set the kernel builds for CONFIG_RTW89_8851BU (minus the units the chip
# selection does not reach: the PCI transport, the Wi-Fi 7 generation, the other
# chips, and the power-management/debug/LED units).
SRCS := $(DRV)/core.c $(DRV)/mac80211.c $(DRV)/mac.c $(DRV)/phy.c $(DRV)/fw.c \
    $(DRV)/cam.c $(DRV)/efuse.c $(DRV)/regd.c $(DRV)/sar.c $(DRV)/coex.c \
    $(DRV)/ps.c $(DRV)/chan.c $(DRV)/ser.c $(DRV)/acpi.c $(DRV)/util.c \
    $(DRV)/rtw8851b.c $(DRV)/rtw8851b_table.c $(DRV)/rtw8851b_rfk.c \
    $(DRV)/rtw8851b_rfk_table.c $(DRV)/usb.c $(DRV)/rtw8851bu.c

ARCH   := -mcpu=cortex-a55 -mgeneral-regs-only -mstrict-align -mno-outline-atomics
COMMON := -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -g3 -O2 \
    -ffunction-sections -fdata-sections
INC    := -I$(FACE) -I$(DRV)
# The chip and bus selection the kernel's Kconfig resolves for the 8851BU.
DRV_CONFIG := -DCONFIG_RTW89_CORE -DCONFIG_RTW89_8851B -DCONFIG_RTW89_USB \
    -DCONFIG_RTW89_8851BU
# Header dependencies are tracked: the face is edited far more often than the driver
# bytes, and a stale object would report the previous header's warnings.
DEPFLAGS := -MMD -MP

# The kernel's own default set for a driver directory (scripts/Makefile.warn): -Wall
# plus -Wextra, with the warnings its default build switches off again - a driver's
# bytes are compiled here at exactly the level their own build takes.
DRV_WARN    := -Wall -Wextra -Wno-address-of-packed-member -Wno-maybe-uninitialized \
    -Wno-missing-field-initializers -Wno-shift-negative-value -Wno-sign-compare \
    -Wno-unused-parameter
DRV_CFLAGS  := $(COMMON) $(DRV_CONFIG) $(DRV_WARN)
FACE_CFLAGS := $(COMMON) -Wall -Wextra

.PHONY: all compile contract spdx gates clean

all: compile contract spdx gates

# ---- compile: the vendored bytes against the first-party face ----------------

OBJS := $(foreach s,$(SRCS),$(BUILD)/$(notdir $(basename $(s))).o)

# The interface's own record of the pinned imported headers' one known warning site;
# matched by file and line so a different warning in the same file still counts.
# Two recorded sites: the imported header's __stringify redefinition (above), and one
# warning inside the vendored bytes' own style - phy.c passes a u8 string where the
# driver's debug helper takes a char string, which the kernel's default warning set also
# reports. Both are matched by file and line so any other warning still fails.
KNOWN_WARN := sysfs.h:47 phy.c:3842

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

# ---- spdx: the license split, mechanically ------------------------------------

spdx:
	@set -e; \
	if [ -z "$(DRV)/core.c" ]; then echo "FAIL: the driver tree is not checked out"; exit 1; fi; \
	extract="$$(printf '%s\n' $(DRV)/*.c $(DRV)/*.h $(DRV)/Kconfig $(DRV)/Makefile)"; \
	total=$$(printf '%s\n' $$extract | wc -l); \
	n=$$(grep -l 'SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause' $$extract | wc -l); \
	if [ "$$n" != "$$total" ]; then echo "FAIL: extracted files with the dual SPDX: $$n of $$total"; exit 1; fi; \
	extra=$$(grep -h 'SPDX-License-Identifier' $$extract | sort -u | grep -v 'GPL-2.0 OR BSD-3-Clause' || true); \
	if [ -n "$$extra" ]; then echo "FAIL: a second SPDX form in the extracted tree:"; printf '%s\n' "$$extra"; exit 1; fi; \
	face="$$(find $(FACE)/linux $(FACE)/net -name '*.h' | grep -vE '$(subst $(space),|,$(FACE_PINNED_NO_SPDX))')"; \
	bad=$$(grep -L 'SPDX-License-Identifier: BSD-3-Clause' $$face || true); \
	if [ -n "$$bad" ]; then echo "FAIL: first-party face file without the project SPDX:"; printf '%s\n' "$$bad"; exit 1; fi; \
	other=$$(grep -h 'SPDX-License-Identifier' $$face | sort -u | grep -v 'BSD-3-Clause' || true); \
	if [ -n "$$other" ]; then echo "FAIL: a non-project SPDX form in the face:"; printf '%s\n' "$$other"; exit 1; fi; \
	copy=$$(grep -l 'GNU General Public' $$face || true); \
	if [ -n "$$copy" ]; then echo "FAIL: license text copied into the first-party face:"; printf '%s\n' "$$copy"; exit 1; fi; \
	echo "spdx: $$n/$$total extracted files dual-licensed with no second form; $$(printf '%s\n' $$face | wc -l) face files BSD-3-Clause with no copied license text"

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

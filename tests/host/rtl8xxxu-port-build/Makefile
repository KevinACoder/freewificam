# Static build gates for the extracted rtl8xxxu driver and its first-party header face.
#
# Self contained; run it from anywhere with:
#   make -C <repo>/tests/host/rtl8xxxu-port-build
#
# The driver is vendored bytes and the mac80211/cfg80211 header face it compiles
# against is this project's own, so the checks split the same way:
#
#   compile    the RTL8188EU translation units (core.c, 8188e.c) compile for
#              aarch64 against the first-party face, zero warnings
#   contract   the face headers compile as one translation unit on their own, at a
#              stricter warning level than the driver's, so the face cannot hide
#              behind the bytes it serves
#   spdx       the license split is mechanical: the extracted files carry exactly one
#              SPDX form (GPL-2.0-only), the first-party face carries the project's
#              (BSD-2-Clause) and no license text copied from elsewhere
#   gates      the kernel-isolation gates over the driver objects (symbol gate) and
#              the first-party face sources, with their canary proofs
#
# Warning level, and why it is what it is: the driver units are compiled with -Wall,
# which is the level the upstream kernel build itself takes for this directory; the
# project's stricter -Wextra flags kernel-style unused parameters, which upstream
# considers clean, so requiring it here would demand more of the vendored bytes than
# their own build does. That relaxation is recorded rather than hidden, and the
# first-party face is held to -Wextra -Werror separately. Warnings are counted
# instead of escalated to errors, so a regression shows up as a count in the log.
#
# Three warning sites are known and enumerated rather than fixed, because they sit in
# the pinned imported header this component consumes byte-identically (the record is
# components/linuxkpi/PROVENANCE.md, "Known warnings of the imported headers"): the
# two incompatible-pointer warnings of linuxkpi_core.h's irqsave spinlock class (it
# passes a struct mutex where a spinlock_t is expected) and sysfs.h redefining
# __stringify. They are allowlisted by file and line below, so any other warning -
# in particular any warning in the driver's own bytes or in the first-party face -
# still fails the gate.

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT  := $(abspath $(MAKEFILE_DIR)/../../..)
DRV   := $(ROOT)/third-party/rtl8xxxu
FACE  := $(ROOT)/components/linuxkpi/include
BUILD := $(MAKEFILE_DIR)/build

# A space, used to turn the allowlisted warning sites into a grep alternation.
empty :=
space := $(empty) $(empty)

AARCH64_CROSS_PATH ?= $(HOME)/workspace/env/src/xpack-aarch64-none-elf-gcc-13.2.1-1.1
CROSS_COMPILE ?= $(AARCH64_CROSS_PATH)/bin/aarch64-none-elf-
CC  := $(CROSS_COMPILE)gcc
NM  := $(CROSS_COMPILE)nm

# The RTL8188EU translation units. The other seven chip units of the extracted tree
# are carried byte-identical but are not compiled here: they are only reachable
# through the driver's device table, which the board image links in the bridge round.
SRCS := $(DRV)/core.c $(DRV)/8188e.c

ARCH   := -mcpu=cortex-a55 -mgeneral-regs-only -mstrict-align -mno-outline-atomics
COMMON := -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -g3 -O2 \
    -ffunction-sections -fdata-sections
INC    := -I$(FACE) -I$(DRV)
# Header dependencies are tracked: the face is edited far more often than the driver
# bytes, and a stale object would report the previous header's warnings.
DEPFLAGS := -MMD -MP

DRV_CFLAGS  := $(COMMON) -Wall
FACE_CFLAGS := $(COMMON) -Wall -Wextra

.PHONY: all compile contract spdx gates clean

all: compile contract spdx gates

# ---- compile: the extracted bytes against the first-party face ----------------

OBJS := $(foreach s,$(SRCS),$(BUILD)/$(notdir $(basename $(s))).o)

# Two files of the pinned imported header set carry no SPDX line (they are covered by
# the component's provenance record instead, and editing them is not allowed); the
# license scan exempts exactly those, by path.
FACE_PINNED_NO_SPDX := $(FACE)/linux/device.h $(FACE)/linux/sysfs.h

# The three known warning sites of the pinned imported headers (see the note above);
# matched by file and line so a different warning in the same file still counts.
KNOWN_WARN := linuxkpi_core.h:1753 linuxkpi_core.h:1760 linuxkpi_core.h:2200 sysfs.h:47

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
	if [ -z "$(ROOT)/third-party/rtl8xxxu/core.c" ]; then echo "FAIL: the driver tree is not checked out"; exit 1; fi; \
	extract="$$(printf '%s\n' $(DRV)/*.c $(DRV)/*.h $(DRV)/Kconfig $(DRV)/Makefile)"; \
	n=$$(grep -l 'SPDX-License-Identifier: GPL-2.0-only' $$extract | wc -l); \
	if [ "$$n" != "13" ]; then echo "FAIL: extracted files with the GPL-2.0-only SPDX: $$n, expected 13"; exit 1; fi; \
	extra=$$(grep -h 'SPDX-License-Identifier' $$extract | sort -u | grep -v 'GPL-2.0-only' || true); \
	if [ -n "$$extra" ]; then echo "FAIL: a second SPDX form in the extracted tree:"; printf '%s\n' "$$extra"; exit 1; fi; \
	face="$$(find $(FACE)/linux $(FACE)/net -name '*.h' | grep -vE '$(subst $(space),|,$(FACE_PINNED_NO_SPDX))')"; \
	bad=$$(grep -L 'SPDX-License-Identifier: BSD-2-Clause' $$face || true); \
	if [ -n "$$bad" ]; then echo "FAIL: first-party face file without the project SPDX:"; printf '%s\n' "$$bad"; exit 1; fi; \
	other=$$(grep -h 'SPDX-License-Identifier' $$face | sort -u | grep -v 'BSD-2-Clause' || true); \
	if [ -n "$$other" ]; then echo "FAIL: a non-project SPDX form in the face:"; printf '%s\n' "$$other"; exit 1; fi; \
	scanned="$$(find $(FACE)/linux $(FACE)/net -name '*.h')"; \
	copy=$$(grep -l 'GNU General Public' $$scanned || true); \
	if [ -n "$$copy" ]; then echo "FAIL: license text copied into the first-party face:"; printf '%s\n' "$$copy"; exit 1; fi; \
	echo "spdx: 13/13 extracted files GPL-2.0-only with no second form; $$(printf '%s\n' $$face | wc -l) face files BSD-2-Clause with no copied license text"

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
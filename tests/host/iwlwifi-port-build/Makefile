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
BUILD ?= $(MAKEFILE_DIR)/build

# A space, used to turn the allowlisted warning sites into a grep alternation.
empty :=
space := $(empty) $(empty)

AARCH64_CROSS_PATH ?= $(HOME)/workspace/env/src/xpack-aarch64-none-elf-gcc-13.2.1-1.1
CROSS_COMPILE ?= $(AARCH64_CROSS_PATH)/bin/aarch64-none-elf-
CC  := $(CROSS_COMPILE)gcc

# The 7260 (mvm) translation units: the core half, the firmware half, the PCIe
# transport, the combined MAC/RF configuration families the kernel's IWLMVM gate
# selects, and the mvm units. This is the closure the driver's own Makefile names
# (iwlwifi/Makefile, mvm/Makefile), minus the units the chip selection does not
# reach: the dvm generation, mei, tests, the IWLMLD-only configuration families,
# and the DEBUGFS / LEDS / PM_SLEEP / IWLMEI-conditional units.
#
# The configuration families are the whole IWLMVM set, not just the part's own
# family. pcie/drv.c carries the PCI ID table, and that table names every family's
# MAC/RF configuration record - the kernel builds them all for CONFIG_IWLMVM, so
# the link needs them all; they are constant tables with no run-time cost for a
# part that never selects them.
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
    $(DRV)/cfg/7000.c $(DRV)/cfg/8000.c $(DRV)/cfg/9000.c \
    $(DRV)/cfg/22000.c $(DRV)/cfg/ax210.c $(DRV)/cfg/bz.c $(DRV)/cfg/sc.c \
    $(DRV)/cfg/rf-jf.c $(DRV)/cfg/rf-hr.c $(DRV)/cfg/rf-gf.c \
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

# The kernel's own default warning set for a driver directory, transcribed from
# scripts/Makefile.warn (the "Default set of warnings, always enabled" block, the
# W=1/W=2/W=3 "else" blocks that switch the noisy ones back off, and the driver's
# own Makefile line `CFLAGS_pcie/drv.o += -Wno-override-init`). The point of
# transcribing rather than choosing: a warning the kernel tolerates must not be
# turned into a gate failure here, and a warning the kernel would report - or
# reject - must still fail. The -Werror= group is the kernel's own (an implicit
# function declaration is a hard error in the kernel's build, which is exactly the
# check this round leans on to prove every referenced symbol is declared).
DRV_WARN    := -Wall -Wextra -Wundef \
    -Werror=implicit-function-declaration -Werror=implicit-int \
    -Werror=return-type -Werror=strict-prototypes -Werror=date-time \
    -Werror=incompatible-pointer-types -Werror=designated-init \
    -Wno-format-security -Wno-trigraphs -Wno-frame-address \
    -Wno-address-of-packed-member -Wno-type-limits -Wno-dangling-pointer \
    -Wno-pointer-sign -Wno-alloc-size-larger-than -Wno-unused-but-set-variable \
    -Wno-unused-const-variable -Wno-packed-not-aligned -Wno-format-overflow \
    -Wno-format-truncation -Wno-stringop-truncation -Wno-override-init \
    -Wno-missing-field-initializers -Wno-shift-negative-value \
    -Wno-maybe-uninitialized -Wno-sign-compare -Wno-unused-parameter \
    -Wenum-conversion -Wcast-function-type -Wunused
DRV_CFLAGS  := $(COMMON) $(DRV_CONFIG) $(DRV_WARN) $(EXTRA_WARN)
FACE_CFLAGS := $(COMMON) -Wall -Wextra

# `diag` compiles the same set with the kernel's hard errors downgraded to warnings,
# so one run shows every missing declaration instead of stopping at the first. It is
# how the round's work list is read; the gate itself (`compile`) keeps the errors.
EXTRA_WARN ?=

.PHONY: all compile contract spdx gates closure diag clean

all: compile contract spdx gates closure

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
	$$(CC) $$(ARCH) $$(DRV_CFLAGS) $$(DEPFLAGS) $$(INC) -c $$< -o $$@ 2> $$(BUILD)/$(2).compile.log || \
		{ cat $$(BUILD)/$(2).compile.log; exit 1; }
endef

# The per-unit log name is passed in as a literal, not recomputed inside the
# recipe: a `$(1)` reference there would expand to nothing (it is only bound
# during `call`), every unit would write the same hidden `.compile.log`, and the
# warning count below - which globs `*.compile.log` - would silently see none of
# them.
$(foreach src,$(SRCS),$(eval $(call COMPILE_RULE,$(src),$(subst /,_,$(patsubst $(DRV)/%,%,$(src))))))

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
# The component's objects track their headers too: the face is edited far more
# often than the driver's bytes, and a stale object here reports the previous
# header's closure - a false green on the one gate that proves the image links.
-include $(wildcard $(LKPI_BUILD)/*.d)

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

# ---- closure: what the driver and the linuxkpi component still need -----------
#
# A translation unit that compiles proves nothing about what it calls: a
# declaration with no definition behind it compiles and fails at link time. So the
# round's second gate links the driver's objects together with the linuxkpi
# component's own, and looks at what is left. The only symbols that may remain are
# ones the image supplies from outside these two: the image's C library, the
# CMSIS-RTOS2 service calls the OS port implements, the compiler's 64-bit-division
# libcall, the linker script's section brackets, the one weak net-device seam a
# board image supplies, and get_random_bytes - which the wireless image already
# gets from the net80211 port (components/net80211/net80211/ieee80211_netbsd.c
# defines it over its own cprng, so the linuxkpi component must not define a
# second one). That set is recorded in closure-external.txt, one line per symbol,
# and a symbol outside it fails the gate - which is the difference between "the
# driver compiles" and "the driver can be linked into an image".
#
# The linuxkpi component is compiled here with its own include set (its internal
# header, the face, the ulog sources, the CMSIS-RTOS2 interface) and the ThreadX
# flavour of the OS abstraction, because the closure is a property of the sources,
# not of which kernel the image happens to link.

LKPI       := $(ROOT)/components/linuxkpi
LKPI_BUILD := $(BUILD)/lkpi
CMSIS_RTOS2_INC := $(ROOT)/third-party/CMSIS_6/CMSIS/RTOS2/Include

# The component's units this closure needs: the core, the platform target, the
# mac80211 and cfg80211 substitutes, the wiphy-work and TXQ engines, the net-device
# seam, the skbuff pool, the PCI engine and the firmware registry (with the 7260
# image registered in it).
LKPI_SRCS := \
    $(LKPI)/linuxkpi_core.c $(LKPI)/linuxkpi_compat.c \
    $(LKPI)/linuxkpi_platform_target.c $(LKPI)/linuxkpi_80211.c \
    $(LKPI)/linuxkpi_80211_helpers.c $(LKPI)/linuxkpi_wiphy_work.c \
    $(LKPI)/linuxkpi_txq.c $(LKPI)/linuxkpi_netdev.c \
    $(LKPI)/linuxkpi_cfg80211.c $(LKPI)/linuxkpi_osdep.c \
    $(LKPI)/fw_registry.c $(LKPI)/fw/iwl7260fw_blob.c \
    $(LKPI)/skbuff/skbuff_pool.c $(LKPI)/pci/linuxkpi_pci.c

LKPI_CFLAGS := $(COMMON) -Wall -Wextra \
    -I$(ROOT) -I$(FACE) -I$(LKPI) -I$(ROOT)/components/ulog/src \
    -I$(CMSIS_RTOS2_INC) -DCMSIS2_BACKEND_THREADX

define LKPI_OBJ
$(LKPI_BUILD)/$(subst /,_,$(patsubst $(LKPI)/%,%,$(1))).o
endef

LKPI_OBJS := $(foreach s,$(LKPI_SRCS),$(call LKPI_OBJ,$(s)))

EXTERNAL := $(MAKEFILE_DIR)/closure-external.txt

closure: $(OBJS) $(LKPI_OBJS)
	@set -e; \
	if [ ! -f "$(EXTERNAL)" ]; then echo "FAIL: $(notdir $(EXTERNAL)) is missing"; exit 1; fi; \
	$(CROSS_COMPILE)ld -r -o $(BUILD)/closure.o $(OBJS) $(LKPI_OBJS); \
	$(CROSS_COMPILE)nm -u $(BUILD)/closure.o | awk '{print $$2}' | LC_ALL=C sort -u > $(BUILD)/undefined.txt; \
	n=$$(wc -l < $(BUILD)/undefined.txt); \
	outside=$$(comm -23 $(BUILD)/undefined.txt $(EXTERNAL)); \
	if [ -n "$$outside" ]; then \
		echo "FAIL: the closure needs symbols the platform does not supply:"; \
		printf '%s\n' "$$outside"; \
		exit 1; \
	fi; \
	unused=$$(comm -13 $(BUILD)/undefined.txt $(EXTERNAL)); \
	if [ -n "$$unused" ]; then \
		echo "FAIL: the platform ledger names symbols nothing needs (remove them):"; \
		printf '%s\n' "$$unused"; \
		exit 1; \
	fi; \
	echo "closure: driver + linuxkpi reference $$n symbols, all of them the platform's (recorded in $(notdir $(EXTERNAL)))"

$(LKPI_BUILD):
	mkdir -p $@

define LKPI_COMPILE_RULE
$(call LKPI_OBJ,$(1)): $(1) | $(LKPI_BUILD)
	@set -e; \
	$$(CC) $$(ARCH) $$(LKPI_CFLAGS) $$(DEPFLAGS) -c $$< -o $$@ 2> $$(LKPI_BUILD)/$(2).compile.log || \
		{ cat $$(LKPI_BUILD)/$(2).compile.log; exit 1; }
endef

$(foreach src,$(LKPI_SRCS),$(eval $(call LKPI_COMPILE_RULE,$(src),$(subst /,_,$(patsubst $(LKPI)/%,%,$(src))))))

$(BUILD):
	mkdir -p $@

# Every diagnostic of the whole set, in one read. Override BUILD for a private run.
diag: $(OBJS)
	@cat $(BUILD)/*.compile.log 2>/dev/null | grep -E '(warning|error):' || echo "diag: no diagnostics"

clean:
	rm -rf $(BUILD)

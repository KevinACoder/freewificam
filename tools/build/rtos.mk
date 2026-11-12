# tools/build/rtos.mk - the implementation behind ./make.rtos.
#
# It owns the profile selection and the generation step; the platform Makefile
# (tests/board/rtos/build/Makefile) still owns the source sets, the per-world
# flag worlds and the link.  Keeping the two apart is deliberate: this file
# changes only where the CONFIG_* values come from.
#
# This is the previous tree's entry, trimmed to the profiles this line carries
# (RK3568 + FreeRTOS: tick, wired network, PCIe/NVMe, eMMC, USB storage).  The
# SDK/CMake/host targets left with the worlds they served; when the mac80211
# component lands its archives join `libs` here.

ROOT := $(CURDIR)
BUILD_MK := $(ROOT)/tests/board/rtos/build
CONFIGS := $(ROOT)/tests/board/rtos/configs
PYTHON ?= python3
JOBS ?= $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)

# The default profile is the selector in config.mk, exactly as before.
PROFILE ?= $(shell sed -n 's/^[[:space:]]*CONFIG[[:space:]]*:=[[:space:]]*//p' \
	$(ROOT)/tests/board/rtos/config.mk | head -n1)
DEFCONFIG := $(CONFIGS)/$(PROFILE).defconfig
GEN_DIR := $(ROOT)/build/kconfig/$(PROFILE)
GEN_CONFIG := $(GEN_DIR)/freewificam_config
MATRIX_PROFILES ?= rk3568-tick rk3568-net rk3568-pcie-nvme rk3568-emmc rk3568-usb-msc rk3568-kpi-live rk3568-kpi-mock

.PHONY: help list defconfig config menuconfig olddefconfig savedefconfig image images libs \
	check check-no-d check-config-header check-build-contract case-list-check matrix \
	dump-vars clean distclean rk3568_ci s0-compile s1-driver-compile s1-link host-test fw-check

help:
	@printf '%s\n' \
		'usage: ./make.rtos <target> [PROFILE=<defconfig>]' \
		'' \
		'  list [--current]   the defconfigs (or the selected one)' \
		'  defconfig          list the defconfigs (same as list)' \
		'  config             generate build/kconfig/<profile>/freewificam_config{,.h,.config}' \
		'  menuconfig         edit this profile (Kconfiglib menuconfig), then save it' \
		'  olddefconfig       regenerate from build/kconfig/<profile>/.config' \
		'  savedefconfig      write a minimal configs/<profile>.defconfig from .config' \
		'  image              one image for the profile backend' \
		'  images             alias of image (this line carries one backend)' \
		'  libs               archive the image object groups under build/lib/<profile>' \
		'  s0-compile         compile the imported stack TUs against the face (no link; S0_RUN=<n>)' \
		'  s1-driver-compile  compile the vendored iwlwifi TUs against the face (no link; S1_RUN=<n>)' \
		'  s1-link            partial-link stack+driver and classify the undefined list (S1_LINK_RUN=<n>)' \
		'  host-test          build and run the host semantic bench (green, then --break red)' \
		'  fw-check           verify the tracked firmware image and the generated blob' \
		'  check              every defconfig parses and regenerates identically' \
		'  check-no-d         no compile/link command carries a -D (representative set)' \
		'  check-config-header generated header covers every Kconfig symbol' \
		'  check-build-contract the compiler-semantics contract holds' \
		'  case-list-check    image BOARD_TESTS vs CONFIG_CASES vs judge.py (needs image)' \
		'  matrix             clean build a representative profile matrix' \
		'  dump-vars          the effective make variables of the profile (JSON)' \
		'  rk3568_ci          every rk3568-itx defconfig, clean build each' \
		'  clean | distclean  objects, then generated fragments too' \
		'' \
		'profile: $(PROFILE)'

list:
	@for f in $(CONFIGS)/*.defconfig; do \
		[ -e "$$f" ] || continue; \
		p=$$(basename "$$f" .defconfig); \
		if [ "$(CURRENT)" = "1" ] && [ "$$p" != "$(PROFILE)" ]; then continue; fi; \
		printf '%s\n' "$$p"; \
	done

defconfig: list

config:
	@[ -f "$(DEFCONFIG)" ] || { \
		echo "make.rtos: no $(DEFCONFIG); run ./make.rtos list" >&2; exit 1; }
	@$(PYTHON) $(ROOT)/tools/build/kconfig-gen.py --profile $(PROFILE)
	@printf 'config: %s\n' '$(GEN_CONFIG)'

menuconfig: config
	@KCONFIG_CONFIG=$(GEN_DIR)/.config $(PYTHON) $(ROOT)/tools/kconfig/menuconfig.py $(ROOT)/Kconfig
	@$(PYTHON) $(ROOT)/tools/build/kconfig-gen.py --profile $(PROFILE) --from-config $(GEN_DIR)/.config
	@$(PYTHON) $(ROOT)/tools/build/kconfig-gen.py --profile $(PROFILE) --from-config $(GEN_DIR)/.config --savedefconfig $(DEFCONFIG)
	@printf 'menuconfig: %s saved\n' '$(DEFCONFIG)'

olddefconfig:
	@if [ -f "$(GEN_DIR)/.config" ]; then \
		$(PYTHON) $(ROOT)/tools/build/kconfig-gen.py --profile $(PROFILE) --from-config $(GEN_DIR)/.config; \
	else $(PYTHON) $(ROOT)/tools/build/kconfig-gen.py --profile $(PROFILE); fi

savedefconfig:
	@[ -f "$(GEN_DIR)/.config" ] || { echo "savedefconfig: no $(GEN_DIR)/.config" >&2; exit 1; }
	@$(PYTHON) $(ROOT)/tools/build/kconfig-gen.py --profile $(PROFILE) --from-config $(GEN_DIR)/.config --savedefconfig $(DEFCONFIG)
	@printf 'savedefconfig: %s\n' '$(DEFCONFIG)'

check-no-d: config
	@$(ROOT)/tools/build/check-no-d.sh $(MATRIX_PROFILES)

# The compiler-semantics contract (tools/build/check-build-contract.sh): one
# definition, every C flags set derives from it, no set pins its own -O level.
check-build-contract:
	@$(ROOT)/tools/build/check-build-contract.sh

check-config-header: config
	@$(PYTHON) $(ROOT)/tools/build/check-config-header.py $(GEN_DIR)/freewificam_config.h

matrix:
	@set -e; failed=0; mkdir -p $(ROOT)/build/matrix; \
	for p in $(MATRIX_PROFILES); do \
		echo "matrix: $$p"; \
		$(ROOT)/make.rtos clean >/dev/null 2>&1; \
		if $(ROOT)/make.rtos image PROFILE=$$p >$(ROOT)/build/matrix/$$p.log 2>&1; then \
			echo "matrix: $$p ok"; \
		else echo "matrix: $$p FAILED" >&2; failed=1; fi; \
	done; exit $$failed

# The build targets print the dev-loop elapsed time.
image: check-build-contract config
	@start=$$(date +%s); \
	$(MAKE) -C $(BUILD_MK) -j$(JOBS) image PROFILE=$(PROFILE) || { \
		rc=$$?; echo "elapsed: $$(( $$(date +%s) - start ))s (image failed)"; \
		exit $$rc; }; \
	echo "elapsed: $$(( $$(date +%s) - start ))s (image, profile=$(PROFILE), jobs=$(JOBS))"

images: image

libs: check-build-contract config
	@$(MAKE) -C $(BUILD_MK) -j$(JOBS) libs PROFILE=$(PROFILE)

# S0 is a measurement of the imported stack against the component's Linux face:
# one `-c` per translation unit, a per-TU log and a summary under runs/S0.  It
# does not link, does not enter an image, and is expected to report failures -
# the product is the gap list (docs/HANDOFF-S0-HOST.md §2.4).  The profile only
# supplies the face's fixed macros; rk3568-kpi-mock is the component profile
# that is always present.
S0_PROFILE ?= rk3568-kpi-mock
S0_RUN ?= 1

s0-compile:
	@$(PYTHON) $(ROOT)/tools/build/kconfig-gen.py --profile $(S0_PROFILE)
	@$(MAKE) -C $(BUILD_MK) s0-compile PROFILE=$(S0_PROFILE) S0_RUN=$(S0_RUN)

# S1b is the same measurement shape as S0, one layer up: the vendored driver's
# TUs, one `-c` each, against the same face.  It does not link and does not
# enter an image; docs/HANDOFF-S1-LINK.md §3 is the contract.
S1_PROFILE ?= rk3568-kpi-mock
S1_RUN ?= 1

s1-driver-compile:
	@$(PYTHON) $(ROOT)/tools/build/kconfig-gen.py --profile $(S1_PROFILE)
	@$(MAKE) -C $(BUILD_MK) s1-driver-compile PROFILE=$(S1_PROFILE) S1_RUN=$(S1_RUN)

# S1c is a partial link, not an image: it answers "what is still undefined"
# and "does any ieee80211_*/cfg80211_* symbol have two definitions".  Same
# profile as S0/S1b; the classification lives in tools/build/s1-link.sh.
S1_LINK_PROFILE ?= $(S1_PROFILE)
S1_LINK_RUN ?= 1

s1-link:
	@$(PYTHON) $(ROOT)/tools/build/kconfig-gen.py --profile $(S1_LINK_PROFILE)
	@$(MAKE) -C $(BUILD_MK) s1-link PROFILE=$(S1_LINK_PROFILE) S1_LINK_RUN=$(S1_LINK_RUN)

# The host semantic bench: x86 + the upstream FreeRTOS POSIX port + the same
# kernel-API bodies the images carry.  It is a model of the target's execution
# context, not a board test (tests/host/kpi/README.md).  The config header is
# the component profile's; nothing in the bench is compiled into an image.
HOST_PROFILE ?= rk3568-kpi-mock

# The judge's own test: synthetic serial logs for the newest cases' anchors,
# positive and negative, in seconds and without a board (tools/rtos/
# judge-selftest.py).  A new case's anchors land here before it is judged on a
# board.
judge-selftest:
	@$(PYTHON) $(ROOT)/tools/rtos/judge-selftest.py

host-test:
	@$(PYTHON) $(ROOT)/tools/build/kconfig-gen.py --profile $(HOST_PROFILE)
	@$(MAKE) -C $(ROOT)/tests/host/kpi run PROFILE=$(HOST_PROFILE)

# The firmware image is a tracked input now (the user's directive: a clean
# clone must build without the GT tree).  The generated unit is a build
# product; this target checks both against the pin.
fw-check:
	@$(PYTHON) $(ROOT)/tools/rtos/fw-blobs.py --check

check:
	@set -e; for f in $(CONFIGS)/*.defconfig; do \
		[ -e "$$f" ] || continue; \
		p=$$(basename "$$f" .defconfig); \
		$(PYTHON) tools/build/kconfig-gen.py --profile $$p >/dev/null; \
	done
	@$(PYTHON) tools/build/kconfig-gen.py --check-all
	@if [ -n "$$(git ls-files -- build/kconfig 2>/dev/null)" ]; then \
		echo "FAIL: generated files are tracked:" >&2; \
		git ls-files -- build/kconfig >&2; exit 1; \
	fi
	@echo "check: all defconfigs parse and regenerate identically"

CASE_ELF ?= $(BUILD_MK)/build/rtos-$(shell sed -n 's/^CONFIG_BACKEND_NAME="\(.*\)"/\1/p' $(GEN_CONFIG)).elf
OBJCOPY ?= $(if $(AARCH64_CROSS_PATH),$(AARCH64_CROSS_PATH)/bin/aarch64-none-elf-objcopy,aarch64-none-elf-objcopy)
READELF ?= $(if $(AARCH64_CROSS_PATH),$(AARCH64_CROSS_PATH)/bin/aarch64-none-elf-readelf,aarch64-none-elf-readelf)

case-list-check: config
	@test -f "$(CASE_ELF)" || { echo "case-list-check: no $(CASE_ELF); build $(PROFILE) first" >&2; exit 1; }
	@mkdir -p $(ROOT)/build/case-list
	@$(ROOT)/tools/build/check-case-list.sh "$(CASE_ELF)" "$(DEFCONFIG)" "$(ROOT)/tools/rtos/judge.py" "$(OBJCOPY)" "$(READELF)" "$(ROOT)/build/case-list"

dump-vars:
	@$(PYTHON) $(ROOT)/tools/build/kconfig-gen.py --profile $(PROFILE) >/dev/null
	@$(PYTHON) $(ROOT)/tools/build/dump-vars.py --profile $(PROFILE)

clean:
	@rm -rf $(ROOT)/tests/board/rtos/build/build

distclean: clean
	rm -rf $(ROOT)/build/kconfig

# One-shot board coverage: build every defconfig, one clean build per profile.
# The shell script owns the loop so a failure does not abort the remaining
# profiles; the target is nonzero if any profile failed.
rk3568_ci:
	@$(ROOT)/tools/build/ci.sh rk3568_ci

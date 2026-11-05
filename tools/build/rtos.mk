# tools/build/rtos.mk - the implementation behind ./make.rtos.
#
# It owns the profile selection and the generation step; the platform
# Makefile (tests/board/rtos/build/Makefile) still owns the source sets, the
# per-driver flag worlds and the link.  Keeping the two apart is deliberate:
# the M1 migration changes only where the CONFIG_* values come from.

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
MATRIX_PROFILES ?= net-netio-raw-tx256 rtl8xxxu-8723bu-netio-ar-ht20-ba \
	rtw89-8851bu-ehci-bringup firefly-v2-sdio-mci pcie-7260 uvc rtos-shell

.PHONY: help list defconfig config menuconfig olddefconfig savedefconfig image images libs sdk host host-test digest check case-list-check dump-vars clean distclean \
	check-no-d check-config-header check-build-contract matrix trimcheck sdk-check rk3568_ci firefly_v1_ci firefly_v2_ci

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
		'  images             both backends' \
		'  libs               archive the image object groups under build/lib/<profile>' \
		'  sdk                libs + public include + manifest + freewificam.pc under dist/<profile>' \
		'  sdk-check          sdk + L1 consumer link check (tests/host/sdk-consumer)' \
		'  host | host-test   the host conformance tier' \
		'  digest             third-party and Kconfiglib manifests' \
		'  rk3568_ci          every rk3568-itx defconfig, clean build each' \
		'  firefly_v1_ci      every firefly-v1 defconfig, clean build each' \
		'  firefly_v2_ci      every firefly-v2 defconfig, clean build each' \
		'  check              every defconfig parses and regenerates identically' \
		'  check-no-d         no compile/link command carries a -D (representative set)' \
		'  check-config-header generated header covers every Kconfig symbol' \
		'  matrix             clean build a representative profile matrix' \
		'  trimcheck          tick-only vs full image trim probe' \
		'  case-list-check    image BOARD_TESTS vs CONFIG_CASES vs judge.py (needs image)' \
		'  dump-vars          the effective make variables of the profile (JSON)' \
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
# Runs before every image because a violation is a language change, not a
# warning - RTOS-AGG-03 was exactly this, silently, for a whole afternoon.
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

trimcheck: config
	@$(ROOT)/tools/build/check-trim.sh $(PROFILE)

# The build targets print the dev-loop elapsed time (AGENTS 1.3).
image: check-build-contract config
	@start=$$(date +%s); \
	$(MAKE) -C $(BUILD_MK) -j$(JOBS) image PROFILE=$(PROFILE) || { \
		rc=$$?; echo "elapsed: $$(( $$(date +%s) - start ))s (image failed)"; \
		exit $$rc; }; \
	echo "elapsed: $$(( $$(date +%s) - start ))s (image, profile=$(PROFILE), jobs=$(JOBS))"

images: check-build-contract config
	@start=$$(date +%s); \
	$(MAKE) -C $(BUILD_MK) -j$(JOBS) images PROFILE=$(PROFILE) || { \
		rc=$$?; echo "elapsed: $$(( $$(date +%s) - start ))s (images failed)"; \
		exit $$rc; }; \
	echo "elapsed: $$(( $$(date +%s) - start ))s (images, profile=$(PROFILE), jobs=$(JOBS))"

libs: config
	@$(MAKE) -C $(BUILD_MK) -j$(JOBS) libs PROFILE=$(PROFILE)

sdk: config
	@$(MAKE) -C $(BUILD_MK) -j$(JOBS) sdk PROFILE=$(PROFILE)

sdk-check: sdk
	@$(ROOT)/tests/host/sdk-consumer/check.sh $(PROFILE)

host: config
	@start=$$(date +%s); \
	$(MAKE) -C $(BUILD_MK) -j$(JOBS) host PROFILE=$(PROFILE) || { \
		rc=$$?; echo "elapsed: $$(( $$(date +%s) - start ))s (host failed)"; \
		exit $$rc; }; \
	echo "elapsed: $$(( $$(date +%s) - start ))s (host, profile=$(PROFILE), jobs=$(JOBS))"

host-test: config
	@start=$$(date +%s); \
	$(MAKE) -C $(BUILD_MK) -j$(JOBS) host-test PROFILE=$(PROFILE) || { \
		rc=$$?; echo "elapsed: $$(( $$(date +%s) - start ))s (host-test failed)"; \
		exit $$rc; }; \
	echo "elapsed: $$(( $$(date +%s) - start ))s (host-test, profile=$(PROFILE), jobs=$(JOBS))"

digest:
	@$(MAKE) -C $(BUILD_MK) digest

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

# One-shot board coverage: build every defconfig whose board matches.  The
# shell script owns the loop so a failure does not abort the remaining
# profiles; the target is nonzero if any profile failed.
rk3568_ci firefly_v1_ci firefly_v2_ci:
	@$(ROOT)/tools/build/ci.sh $@

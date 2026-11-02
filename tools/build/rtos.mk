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
GEN_CONFIG := $(ROOT)/build/kconfig/$(PROFILE)/generated/config.mk

.PHONY: help list config image images host host-test digest check dump-vars clean distclean \
	rk3568_ci firefly_v1_ci firefly_v2_ci

help:
	@printf '%s\n' \
		'usage: ./make.rtos <target> [PROFILE=<defconfig>]' \
		'' \
		'  list [--current]   the defconfigs (or the selected one)' \
		'  config             generate build/kconfig/<profile>/generated/config.mk' \
		'  image              one image for the profile backend' \
		'  images             both backends' \
		'  host | host-test   the host conformance tier' \
		'  digest             third-party and Kconfiglib manifests' \
		'  rk3568_ci          every rk3568-itx defconfig, clean build each' \
		'  firefly_v1_ci      every firefly-v1 defconfig, clean build each' \
		'  firefly_v2_ci      every firefly-v2 defconfig, clean build each' \
		'  check              every defconfig parses and regenerates identically' \
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

config:
	@[ -f "$(DEFCONFIG)" ] || { \
		echo "make.rtos: no $(DEFCONFIG); run ./make.rtos list" >&2; exit 1; }
	@$(PYTHON) $(ROOT)/tools/build/kconfig-gen.py --profile $(PROFILE)
	@printf 'config: %s\n' '$(GEN_CONFIG)'

# The build targets print the dev-loop elapsed time (AGENTS 1.3).
image: config
	@start=$$(date +%s); \
	$(MAKE) -C $(BUILD_MK) -j$(JOBS) image PROFILE=$(PROFILE) || { \
		rc=$$?; echo "elapsed: $$(( $$(date +%s) - start ))s (image failed)"; \
		exit $$rc; }; \
	echo "elapsed: $$(( $$(date +%s) - start ))s (image, profile=$(PROFILE), jobs=$(JOBS))"

images: config
	@start=$$(date +%s); \
	$(MAKE) -C $(BUILD_MK) -j$(JOBS) images PROFILE=$(PROFILE) || { \
		rc=$$?; echo "elapsed: $$(( $$(date +%s) - start ))s (images failed)"; \
		exit $$rc; }; \
	echo "elapsed: $$(( $$(date +%s) - start ))s (images, profile=$(PROFILE), jobs=$(JOBS))"

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

dump-vars:
	@$(PYTHON) $(ROOT)/tools/build/kconfig-gen.py --profile $(PROFILE) >/dev/null
	@$(PYTHON) $(ROOT)/tools/build/dump-vars.py --profile $(PROFILE)

clean:
	@$(MAKE) -C $(BUILD_MK) clean

distclean: clean
	rm -rf $(ROOT)/build/kconfig

# One-shot board coverage: build every defconfig whose board matches.  The
# shell script owns the loop so a failure does not abort the remaining
# profiles; the target is nonzero if any profile failed.
rk3568_ci firefly_v1_ci firefly_v2_ci:
	@$(ROOT)/tools/build/ci.sh $@

# freewebcam - top level entry points.
#
# The repository bootstrap milestone defines the full build skeleton: per-target
# configuration, the host and board flavours, and the cross toolchain wiring. This file
# deliberately carries only the two entries the gate definitions already name, so that
# nothing here has to be undone when that skeleton lands.

.PHONY: help mock mock-kernels mock-all gates board-images

help:
	@echo "make mock         run the Gate1 mock suites over this repository's code (host only)"
	@echo "make mock-kernels run the vendored-kernel regression suites (slow; only after"
	@echo "                  a change under third-party/)"
	@echo "make mock-all     every suite in the registry"
	@echo "make board-images build every board image in tests/board/suites.mk"
	@echo "make gates        run the static build-time gates"
	@echo "tip: tools/mock/run.sh <suite>... runs just the named suites"

# Gate1: the host-side mock tier. Every suite runs on this machine, and the runner
# speaks one language for all of them (see tools/mock/README.md).
mock:
	tools/mock/run.sh

# The vendored-kernel regressions: not part of a change-driven cycle, run when the
# kernels themselves are touched (or at a milestone close).
mock-kernels:
	tools/mock/run.sh $$(sed -n 's/^[[:space:]]*SUITES_KERNELS[[:space:]]*:=[[:space:]]*//p' tools/mock/suites.mk)

mock-all:
	tools/mock/run.sh $$(sed -n 's/^[[:space:]]*SUITES_KERNELS[[:space:]]*:=[[:space:]]*//p' tools/mock/suites.mk) \
	    $$(sed -n 's/^[[:space:]]*SUITES[[:space:]]*:=[[:space:]]*//p' tools/mock/suites.mk)

# Static gates over the sources: the machine-local knowledge scan, the board
# images' build shape, and the host suites' build shape (the aggregate builds
# stop at the first failure, and a stale object can hide a defect the sources
# already state, so both shapes are also checked from the source side).
gates:
	tools/gates/check-local-knowledge.sh
	tools/gates/check-board-images.sh
	tools/gates/check-host-makefiles.sh

# The board images: every suite in the board registry, built by its own
# declaration. This is the rebuild half of a board regression round - the board
# itself is driven from the workspace (the runner reads the same registry).
# BOARD_SUITES_MK can point at another registry (e.g. the fixtures).
BOARD_SUITES_MK ?= tests/board/suites.mk

board-images:
	@list=$$(sed -n 's/^[[:space:]]*SUITES_BOARD[[:space:]]*:=[[:space:]]*//p' $(BOARD_SUITES_MK)); \
	if [ -z "$$list" ]; then \
		echo "BOARD-NOTE: the suite registry is empty (nothing to build)"; \
		exit 0; \
	fi; \
	root=$$(dirname $(BOARD_SUITES_MK)); \
	for s in $$list; do \
		echo "== board image: $$s"; \
		$(MAKE) -C $$root/$$s || exit 1; \
	done
	@echo "board-images: every registered image built"

# freewebcam - top level entry points.
#
# The repository bootstrap milestone defines the full build skeleton: per-target
# configuration, the host and board flavours, and the cross toolchain wiring. This file
# deliberately carries only the two entries the gate definitions already name, so that
# nothing here has to be undone when that skeleton lands.

.PHONY: help mock mock-kernels mock-all gates

help:
	@echo "make mock         run the Gate1 mock suites over this repository's code (host only)"
	@echo "make mock-kernels run the vendored-kernel regression suites (slow; only after"
	@echo "                  a change under third-party/)"
	@echo "make mock-all     every suite in the registry"
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

# Static gates over the sources: today the machine-local knowledge scan.
gates:
	tools/gates/check-local-knowledge.sh

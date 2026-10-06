# freewebcam - top level entry points.
#
# The repository bootstrap milestone defines the full build skeleton: per-target
# configuration, the host and board flavours, and the cross toolchain wiring. This file
# deliberately carries only the two entries the gate definitions already name, so that
# nothing here has to be undone when that skeleton lands.

.PHONY: help mock gates

help:
	@echo "make mock   run the Gate1 mock suites (host only, no board)"
	@echo "make gates  run the static build-time gates"

# Gate1: the host-side mock tier. Every suite runs on this machine, and the runner
# speaks one language for all of them (see tools/mock/README.md).
mock:
	tools/mock/run.sh

# Static gates over the sources: today the machine-local knowledge scan.
gates:
	tools/gates/check-local-knowledge.sh

# Convenience entry for the one-shot board builds.  The real entry is
# ./make.rtos (and tools/build/rtos.mk); this file only forwards the CI
# targets so `make rk3568_ci firefly_v2_ci` works from the repository root.
.DEFAULT_GOAL := help

.PHONY: help rk3568_ci firefly_v1_ci firefly_v2_ci

help:
	@./make.rtos help

rk3568_ci firefly_v1_ci firefly_v2_ci:
	@./make.rtos $@

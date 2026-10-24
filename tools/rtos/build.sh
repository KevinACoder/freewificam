#!/bin/sh
# @file    build.sh
# @brief   One entry for the RTOS platform's build: images, host tier, digests.
#
# Usage:
#   tools/rtos/build.sh              both board images + gates + digest check
#   tools/rtos/build.sh host         the host conformance binaries
#   tools/rtos/build.sh host-test    build and run them
#   tools/rtos/build.sh digest       third-party manifests only
#   CASES=tick tools/rtos/build.sh   short arm (one case group)
#   OPT=-O2 tools/rtos/build.sh      optimisation lever (default -Og)
#   LEVERS="-DTCP_WND=131072" tools/rtos/build.sh
#                                    a -D that must reach the vendored stack and
#                                    the drivers too (see rtos/build/Makefile)
#
# The fingerprints printed at the end are what a board round's evidence
# records: size, sha256 and the build tag the banner will carry.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
build=$root/rtos/build

target=${1:-all}

make -C "$build" "$target"

case "$target" in
all|image|check|"")
	printf '\n== fingerprints ==\n'
	printf 'opt: %s\n' "${OPT:--O2 (default)}"
	printf 'profile: %s\n' "${PROFILE:-perf (default)}"
	printf 'levers: %s\n' "${LEVERS:-(none)}"
	for image in "$build"/build/rtos-*.bin; do
		[ -e "$image" ] || continue
		printf '%s size=%s sha256=%s\n' \
			"$(basename "$image")" \
			"$(stat -c '%s' "$image")" \
			"$(sha256sum "$image" | cut -d' ' -f1)"
	done
	make -C "$build" digest
	;;
esac
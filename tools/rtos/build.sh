#!/bin/sh
# @file    build.sh
# @brief   One entry for the RTOS platform's build: images, host tier, digests.
#
# Usage:
#   tools/rtos/build.sh              the configured image + gates
#   tools/rtos/build.sh images       both board images
#   tools/rtos/build.sh host         the host conformance binaries
#   tools/rtos/build.sh host-test    build and run them
#   tools/rtos/build.sh digest       component patch records
#
# The image, backend, cases and WLAN switches come from tests/board/rtos/config.mk
# and the selected file in configs/. There are no per-build CASES/OPT/LEVERS
# switches in the normal loop.
#
# The fingerprints printed at the end are what a board round's evidence
# records: size, sha256 and the build tag the banner will carry.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
build=$root/tests/board/rtos/build

target=${1:-all}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}

make -C "$build" -j"$jobs" "$target"

case "$target" in
all|image|check|"")
	printf '\n== fingerprint ==\n'
	printf 'config: tests/board/rtos/config.mk\n'
	backend=$(make -s -C "$build" print-config)
	image="$build/build/rtos-$backend.bin"
	if [ -e "$image" ]; then
		printf '%s size=%s sha256=%s\n' \
			"$(basename "$image")" \
			"$(stat -c '%s' "$image")" \
			"$(sha256sum "$image" | cut -d' ' -f1)"
	fi
	;;
esac
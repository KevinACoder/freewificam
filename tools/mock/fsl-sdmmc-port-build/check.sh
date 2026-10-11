#!/bin/sh
# @file    check.sh
# @brief   Wrapper for the SDIO line's build-gate suite: turn the port test
#          directory's static gates into container case lines.
#
# The heavy lifting lives in tests/host/fsl-sdmmc-port-build's Makefile. This
# wrapper runs its four gates and maps each into one case: the imported units,
# the port files and the board driver compile clean for aarch64, the shadows and
# the seam compile standalone, the license split scans clean, and the
# kernel-isolation gates pass with their canaries.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
log_dir=$root/tools/mock/build/fsl-sdmmc-port-build
mkdir -p "$log_dir"

failures=0

case_line() {
    printf 'CASE fsl-sdmmc-port-build/%s %s\n' "$1" "$2"
    [ "$2" = "PASS" ] || failures=$((failures + 1))
}

for gate in compile contract spdx gates; do
    if make -C "$root/tests/host/fsl-sdmmc-port-build" "$gate" \
            > "$log_dir/$gate.log" 2>&1; then
        case_line "$gate" PASS
    else
        case_line "$gate" FAIL
        printf 'fsl-sdmmc-port-build: %s failed, see %s\n' \
            "$gate" "$log_dir/$gate.log" >&2
        tail -5 "$log_dir/$gate.log" >&2 || true
    fi
done

exit "$failures"

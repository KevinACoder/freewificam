#!/bin/sh
# @file    check.sh
# @brief   Wrapper for the PCIe backend line's build-gate suite: turn the port
#          test directory's static gates into container case lines.
#
# The heavy lifting lives in tests/host/pcie-backend-build's Makefile. This
# wrapper runs its four gates and maps each into one case: the line's units and
# the port TUs they bind to compile clean for aarch64, the headers compile
# standalone, the license split scans clean, and the kernel-isolation gates pass
# with their canaries.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
log_dir=$root/tools/mock/build/pcie-backend-build
mkdir -p "$log_dir"

failures=0

case_line() {
    printf 'CASE pcie-backend-build/%s %s\n' "$1" "$2"
    [ "$2" = "PASS" ] || failures=$((failures + 1))
}

for gate in compile contract spdx gates; do
    if make -C "$root/tests/host/pcie-backend-build" "$gate" \
            > "$log_dir/$gate.log" 2>&1; then
        case_line "$gate" PASS
    else
        case_line "$gate" FAIL
        printf 'pcie-backend-build: %s failed, see %s\n' \
            "$gate" "$log_dir/$gate.log" >&2
        tail -5 "$log_dir/$gate.log" >&2 || true
    fi
done

exit "$failures"

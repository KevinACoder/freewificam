#!/bin/sh
# @file    check.sh
# @brief   Wrapper for the vendored rtl8189fs driver's build-gate suite: turn the port test
#          directory's static gates into container case lines.
# @author  zhugengyu
# @date    12.10.2026
#
# The heavy lifting lives in tests/host/rtl8189fs-port-build's Makefile. This wrapper runs
# its five gates and maps each into one case: the 131 RTL8188F translation units compile
# clean for aarch64 against the first-party header face and the SDIO engine, the face
# compiles standalone, the driver's undefined-symbol set matches the recorded face ledger,
# the license split scans clean, and the kernel-isolation gates pass with their canaries.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
log_dir=$root/tools/mock/build/rtl8189fs-port-build
mkdir -p "$log_dir"

failures=0

case_line() {
    printf 'CASE rtl8189fs-port-build/%s %s\n' "$1" "$2"
    [ "$2" = "PASS" ] || failures=$((failures + 1))
}

for gate in compile contract closure spdx gates; do
    if make -C "$root/tests/host/rtl8189fs-port-build" "$gate" \
            > "$log_dir/$gate.log" 2>&1; then
        case_line "$gate" PASS
    else
        case_line "$gate" FAIL
        printf 'rtl8189fs-port-build: %s failed, see %s\n' \
            "$gate" "$log_dir/$gate.log" >&2
        tail -5 "$log_dir/$gate.log" >&2 || true
    fi
done

exit "$failures"

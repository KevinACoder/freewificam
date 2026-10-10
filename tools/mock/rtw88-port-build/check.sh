#!/bin/sh
# @file    check.sh
# @brief   Wrapper for the vendored rtw88 driver's build-gate suite: turn the port test
#          directory's static gates into container case lines.
# @author  zhugengyu
# @date    11.10.2026
#
# The heavy lifting lives in tests/host/rtw88-port-build's Makefile. This wrapper runs
# its four gates and maps each into one case: the 8821CU translation units compile clean
# for aarch64 against the first-party header face, the face compiles standalone, the
# license split scans clean, and the kernel-isolation gates pass with their canaries.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
log_dir=$root/tools/mock/build/rtw88-port-build
mkdir -p "$log_dir"

failures=0

case_line() {
    printf 'CASE rtw88-port-build/%s %s\n' "$1" "$2"
    [ "$2" = "PASS" ] || failures=$((failures + 1))
}

for gate in compile contract spdx gates; do
    if make -C "$root/tests/host/rtw88-port-build" "$gate" \
            > "$log_dir/$gate.log" 2>&1; then
        case_line "$gate" PASS
    else
        case_line "$gate" FAIL
        printf 'rtw88-port-build: %s failed, see %s\n' \
            "$gate" "$log_dir/$gate.log" >&2
        tail -5 "$log_dir/$gate.log" >&2 || true
    fi
done

exit "$failures"

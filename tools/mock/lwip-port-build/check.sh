#!/bin/sh
# @file    check.sh
# @brief   Wrapper for the lwIP port build-gate suite: turn the port test
#          directory's static gates into container case lines.
# @author  zhugengyu
# @date    08.10.2026
#
# The heavy lifting lives in tests/host/lwip-port-build's Makefile (one build
# language, one place to read it). This wrapper runs its four gates and maps
# each into one case: the stack and port sources compile clean for both
# CMSIS-RTOS2 backends (aarch64), the sys_arch function face is present with
# undefined symbols confined to the CMSIS service prefix, and the
# kernel-isolation gates pass.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
log_dir=$root/tools/mock/build/lwip-port-build
mkdir -p "$log_dir"

failures=0

case_line() {
    printf 'CASE lwip-port-build/%s %s\n' "$1" "$2"
    [ "$2" = "PASS" ] || failures=$((failures + 1))
}

for gate in compile-threadx compile-freertos symbols gates; do
    if make -C "$root/tests/host/lwip-port-build" "$gate" \
            > "$log_dir/$gate.log" 2>&1; then
        case_line "$gate" PASS
    else
        case_line "$gate" FAIL
        printf 'lwip-port-build: %s failed, see %s\n' \
            "$gate" "$log_dir/$gate.log" >&2
        tail -5 "$log_dir/$gate.log" >&2 || true
    fi
done

exit "$failures"

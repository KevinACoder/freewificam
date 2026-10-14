#!/bin/sh
# @file    check.sh
# @brief   Wrapper for the eMMC line's host suite: the battery, and the red
#          control that proves the battery can go red.
#
# The heavy lifting lives in tests/host/fsl-sdmmc-mmc-test's Makefile and main.c.
# This wrapper turns the two runs into the container's case lines.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
log_dir=$root/tools/mock/build/fsl-sdmmc-mmc-test
mkdir -p "$log_dir"

failures=0

case_line() {
    printf 'CASE fsl-sdmmc-mmc-test/%s %s\n' "$1" "$2"
    [ "$2" = "PASS" ] || failures=$((failures + 1))
}

if make -C "$root/tests/host/fsl-sdmmc-mmc-test" all > "$log_dir/build.log" 2>&1; then
    case_line build PASS
else
    case_line build FAIL
    tail -20 "$log_dir/build.log" >&2 || true
    exit "$failures"
fi

if "$root/tests/host/fsl-sdmmc-mmc-test/build/fsl-sdmmc-mmc-test" \
        > "$log_dir/run.log" 2>&1; then
    case_line battery PASS
else
    case_line battery FAIL
    grep -E 'ok=0|failures=' "$log_dir/run.log" >&2 || true
fi

if "$root/tests/host/fsl-sdmmc-mmc-test/build/fsl-sdmmc-mmc-test" \
        --fault expect-tamper > "$log_dir/red.log" 2>&1; then
    # the tampered run must FAIL: a zero exit here means the battery cannot go red
    case_line red-control FAIL
else
    case_line red-control PASS
fi

exit "$failures"

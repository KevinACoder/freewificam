#!/bin/sh
# @file    run-tests.sh
# @brief   Gate1 suite freertos-kernel-unit: run every built R1 binary and judge it.
# @author  zhugengyu
# @date    07.10.2026
# @design adapter - ref threadx:test/tx/regression/CMakeLists.txt:1
#
# A binary passes only when it exits 0, prints the Unity summary "<N> Tests 0 Failures
# 0 Ignored" with N > 0, and ends in OK. Unity's exit code counts failures only, so the
# ignored count is asserted separately: a suite must never mask a case by skipping it,
# and a binary that ran no case at all must not read as green.
#
# Upstream builds libunity with UNITY_OUTPUT_COLOR, so every log carries ANSI colour
# escapes (for example "\033[42mOK\033[00m"). The raw capture is kept as <bin>.raw.log
# and the escapes are stripped from <bin>.log, which is what the judge reads.
#
# One CASE line per binary goes to stdout for the mock container. Exit codes: 0 all
# green; 1 at least one binary failed; 2 built artifacts are missing.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
cd "$root"
. "$here/modules.sh"

bin_dir=$FKU_OUT/bin
if [ ! -d "$bin_dir" ]; then
    printf 'FREERTOS-KERNEL-UNIT blocked: %s is missing - build.sh must run first\n' "$bin_dir" >&2
    exit 2
fi

bins=$(fku_bins)
if [ -z "$bins" ]; then
    printf 'FREERTOS-KERNEL-UNIT blocked: no R1 test binary found under %s\n' "$bin_dir" >&2
    exit 2
fi

mkdir -p "$FKU_LOGS"
table=$FKU_LOGS/summary.tsv
: > "$table"

esc=$(printf '\033')
total=0
failed=0

for bin in $bins; do
    raw=$FKU_LOGS/$bin.raw.log
    log=$FKU_LOGS/$bin.log

    set +e
    ( cd "$FKU_VENDORED" && GCOV_PREFIX="$root/$FKU_OUT/gcov" \
        timeout "$FKU_BIN_TIMEOUT" "$root/$bin_dir/$bin" ) > "$raw" 2>&1
    code=$?
    set -e
    sed "s/${esc}\[[0-9;]*m//g" "$raw" > "$log"

    total=$((total + 1))
    tests=
    if [ "$code" != 0 ]; then
        verdict=FAIL
        reason="exit $code"
    elif ! grep -qE '^[0-9]+ Tests 0 Failures 0 Ignored[[:space:]]*$' "$log"; then
        verdict=FAIL
        reason="no zero-failure Unity summary"
    elif ! grep -qxE 'OK' "$log"; then
        verdict=FAIL
        reason="summary present but the run did not end in OK"
    else
        tests=$(grep -oE '^[0-9]+ Tests' "$log" | head -1 | awk '{print $1}')
        if [ "$tests" -gt 0 ]; then
            verdict=PASS
            reason=
        else
            verdict=FAIL
            reason="no test case ran"
        fi
    fi

    if [ "$verdict" = PASS ]; then
        printf 'CASE freertos-kernel-unit/%s PASS\n' "$bin"
    else
        printf 'CASE freertos-kernel-unit/%s FAIL\n' "$bin"
        printf 'FREERTOS-KERNEL-UNIT reason %s: %s\n' "$bin" "$reason" >&2
        failed=$((failed + 1))
    fi
    printf '%s\t%s\t%s\n' "$bin" "$code" "${tests:--}" >> "$table"
done

printf 'FREERTOS-KERNEL-UNIT %s binaries, %s failed (per-binary logs under %s)\n' \
    "$total" "$failed" "$FKU_LOGS"

[ "$failed" = 0 ] || exit 1

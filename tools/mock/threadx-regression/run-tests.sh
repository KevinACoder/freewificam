#!/bin/sh
# @file    run-tests.sh
# @brief   Run every test the upstream suite built, one process each, and report one
#          CASE line per test.
# @author  zhugengyu
# @date    07.10.2026
# @design adapter - ref threadx:scripts/cmake_bootstrap.sh:40
#
# Judgement per test, from the upstream framework's own contract: the process exits with
# the number of failed tests plus system errors, so exit 0 already means "nothing failed
# and the kernel was left in a clean state". The summary line is checked as well, because
# a run whose exit path is broken would otherwise pass silently.
#
# Each test runs once: no repeat, no retry. A test that fails is a finding, not noise,
# and the wrapper must not wash a red away.
#
# THREADX_REGRESSION_CONFIG selects the configuration built by build.sh; the special case
# below is the upstream test that brings its own main() and therefore prints no summary.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
config=${THREADX_REGRESSION_CONFIG:-default_build_coverage}
build=$here/build/$config
logs=$build/logs
limit=${THREADX_REGRESSION_TEST_TIMEOUT:-120}

if [ ! -d "$build/regression" ]; then
    echo "MOCK-BUILD threadx-regression: nothing built yet under $build" >&2
    exit 2
fi

rm -rf "$logs"
mkdir -p "$logs"

# The authoritative list of tests is the one the upstream build registered; the binary
# for each is looked up in the build tree so that a layout change cannot silently shrink
# the set.
tests=$(ctest --test-dir "$build" -N 2>/dev/null | sed -n 's/^ *Test *#[0-9]*: [^:]*:://p')
if [ -z "$tests" ]; then
    echo "MOCK-BUILD threadx-regression: the build registered no test" >&2
    exit 2
fi

pass=0
fail=0
for name in $tests; do
    binary=$(find "$build" -type f -name "$name" -perm -u+x | head -1)
    if [ -z "$binary" ]; then
        printf 'CASE threadx-regression/%s FAIL (no executable in the build tree)\n' "$name"
        fail=$((fail + 1))
        continue
    fi

    set +e
    ( cd "$(dirname "$binary")" && timeout "$limit" "./$(basename "$binary")" ) \
        > "$logs/$name.log" 2>&1
    code=$?
    set -e

    verdict=PASS
    if [ "$code" = 124 ] || [ "$code" = 143 ]; then
        verdict="FAIL (timeout ${limit}s)"
    elif [ "$code" != 0 ]; then
        verdict="FAIL (exit $code)"
    elif [ "$name" = "threadx_initialize_kernel_setup_test" ]; then
        # This test brings its own main() and reports with a single SUCCESS! line.
        grep -qF "SUCCESS!" "$logs/$name.log" || verdict="FAIL (no SUCCESS! line)"
    else
        grep -qE "Tests Failed:[[:space:]]*0[[:space:]]+System Errors:[[:space:]]*0" \
            "$logs/$name.log" || verdict="FAIL (summary is not a clean two-zero line)"
        if [ "$verdict" = PASS ] && ! grep -qE "Tests Passed:[[:space:]]*[0-9]+" "$logs/$name.log"; then
            verdict="FAIL (no test was reported as executed)"
        fi
    fi

    printf 'CASE threadx-regression/%s %s\n' "$name" "$verdict"
    case "$verdict" in
        PASS) pass=$((pass + 1)) ;;
        *)    fail=$((fail + 1))
              echo "--- $name (tail of $logs/$name.log) ---" >&2
              tail -12 "$logs/$name.log" >&2 ;;
    esac
done

echo "threadx-regression: tests=$(printf '%s\n' "$tests" | wc -l) pass=$pass fail=$fail"

[ "$fail" = 0 ]

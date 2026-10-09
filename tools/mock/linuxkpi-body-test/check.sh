#!/bin/sh
# @file    check.sh
# @brief   Wrapper for the linuxkpi body suite: run the binary, map its anchor lines
#          to container cases, then prove the suite can go red.
# @author  zhugengyu
# @date    08.10.2026
#
# The binary (tests/host/linuxkpi-body-test) links the real bodies and prints one
# `LKPI: <id> PASS|FAIL <detail>` line per case plus a `LKPI: SUITE ...` sentinel.
# This wrapper turns those into cases, runs the firmware blob check
# (check-firmware.sh, which is also this suite's build-time two-source check for
# the compiled-in image), and runs each --fault mode, which must fail: a suite
# that cannot go red proves nothing.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
log_dir=$root/tools/mock/build/linuxkpi-body-test
mkdir -p "$log_dir"

bin=$root/tests/host/linuxkpi-body-test/build/linuxkpi-body-test
if [ ! -x "$bin" ]; then
    echo "MOCK-BUILD linuxkpi-body-test: $bin was not built" >&2
    exit 2
fi

failures=0

case_line() {
    printf 'CASE linuxkpi-body-test/%s %s\n' "$1" "$2"
    [ "$2" = "PASS" ] || failures=$((failures + 1))
}

# ---- the suite itself ---------------------------------------------------------

if timeout 120 "$bin" > "$log_dir/stream.log" 2>&1; then
    pass=$(grep -c '^LKPI: L[0-9]* PASS' "$log_dir/stream.log" || true)
    if [ "$pass" = 9 ] &&
        grep -q '^LKPI: SUITE PASS cases=9$' "$log_dir/stream.log" &&
        ! grep -q '^LKPI: L[0-9]* FAIL' "$log_dir/stream.log"; then
        case_line link-closure PASS
        case_line section-walk PASS
        case_line id-match-pos PASS
        case_line id-match-neg PASS
        case_line firmware-by-name PASS
        case_line urb-traffic PASS
        case_line pool-contract PASS
        case_line two-endpoints PASS
        case_line class-hook-claim PASS
    else
        case_line link-closure FAIL
        echo "linuxkpi-body-test: the run did not report nine passing cases ($pass)" >&2
        tail -8 "$log_dir/stream.log" >&2 || true
    fi
else
    case_line link-closure FAIL
    echo "linuxkpi-body-test: the run failed, see $log_dir/run.log" >&2
    tail -8 "$log_dir/stream.log" >&2 || true
fi

# ---- the production adapter endpoint state ------------------------------------

adapter=$root/tests/host/linuxkpi-body-test/build/cherryusb-adapter-test
if timeout 20 "$adapter" > "$log_dir/adapter.log" 2>&1 &&
    grep -q '^ADAPTER PASS ' "$log_dir/adapter.log"; then
    case_line endpoint-toggle PASS
else
    case_line endpoint-toggle FAIL
    tail -5 "$log_dir/adapter.log" >&2 || true
fi
if timeout 20 "$adapter" --fault-data0 > "$log_dir/adapter-red.log" 2>&1; then
    case_line red-endpoint-toggle FAIL
elif grep -q '^ADAPTER FAIL ep=' "$log_dir/adapter-red.log"; then
    case_line red-endpoint-toggle PASS
else
    case_line red-endpoint-toggle FAIL
fi

# ---- the compiled-in firmware image -------------------------------------------

if sh "$root/tests/host/linuxkpi-body-test/check-firmware.sh" > "$log_dir/firmware.log" 2>&1; then
    case_line firmware-blob PASS
else
    case_line firmware-blob FAIL
    tail -3 "$log_dir/firmware.log" >&2 || true
fi

# ---- the red controls (each must fail) ----------------------------------------

for fault in no-walk fw-name; do
    if timeout 60 "$bin" --fault "$fault" > "$log_dir/fault-$fault.log" 2>&1; then
        case_line "red-$fault" FAIL
        echo "linuxkpi-body-test: the $fault control passed (it must fail)" >&2
    elif grep -q '^LKPI: .* FAIL' "$log_dir/fault-$fault.log"; then
        case_line "red-$fault" PASS
    else
        case_line "red-$fault" FAIL
        echo "linuxkpi-body-test: the $fault control failed without naming a case" >&2
        tail -3 "$log_dir/fault-$fault.log" >&2 || true
    fi
done

# an unknown fault name is a usage error, not a red mode
if timeout 20 "$bin" --fault bogus > "$log_dir/fault-bogus.log" 2>&1; then
    case_line fault-usage FAIL
else
    if grep -q 'unknown fault' "$log_dir/fault-bogus.log"; then
        case_line fault-usage PASS
    else
        case_line fault-usage FAIL
        echo "linuxkpi-body-test: an unknown fault name must be a usage error" >&2
    fi
fi

exit "$failures"

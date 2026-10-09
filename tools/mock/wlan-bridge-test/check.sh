#!/bin/sh
# @file    check.sh
# @brief   Wrapper for the WLAN bridge chain suite: run the binary, map its
#          anchor lines to container cases, then prove the suite can go red.
# @author  zhugengyu
# @date    08.10.2026
#
# The binary (tests/host/wlan-bridge-test) links the real chain and prints one
# `WLANBRIDGE: <id> PASS|FAIL <detail>` line per case plus a
# `WLANBRIDGE: SUITE ...` sentinel. This wrapper turns those into cases and runs
# each --fault mode, which must fail while naming the case it invalidated: a
# suite that cannot go red proves nothing.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
log_dir=$root/tools/mock/build/wlan-bridge-test
mkdir -p "$log_dir"

bin=$root/tests/host/wlan-bridge-test/build/wlan-bridge-test
if [ ! -x "$bin" ]; then
    echo "MOCK-BUILD wlan-bridge-test: $bin was not built" >&2
    exit 2
fi

failures=0

case_line() {
    printf 'CASE wlan-bridge-test/%s %s\n' "$1" "$2"
    [ "$2" = "PASS" ] || failures=$((failures + 1))
}

# ---- the suite itself ---------------------------------------------------------

if timeout 180 "$bin" > "$log_dir/stream.log" 2>&1; then
    pass=$(grep -c '^WLANBRIDGE: B[0-9]* PASS' "$log_dir/stream.log" || true)
    if [ "$pass" = 16 ] &&
        grep -q '^WLANBRIDGE: SUITE PASS cases=16$' "$log_dir/stream.log" &&
        ! grep -q '^WLANBRIDGE: B[0-9]* FAIL' "$log_dir/stream.log"; then
        case_line attach-chain PASS
        case_line scan-order-and-walk PASS
        case_line scan-results PASS
        case_line join-chain PASS
        case_line eapol-routing PASS
        case_line data-path PASS
        case_line ccmp-roundtrip PASS
        case_line driver-link-closure PASS
        case_line balance-and-order PASS
        case_line presentation-layer PASS
        case_line supplicant-backend PASS
        case_line parked-resweep-probes PASS
        case_line midsweep-join-recovery PASS
        case_line assoc-wiring PASS
        case_line uplink-key-selection PASS
        case_line rx-status-flags PASS
    else
        case_line attach-chain FAIL
        echo "wlan-bridge-test: the run did not report sixteen passing cases ($pass)" >&2
        grep '^WLANBRIDGE' "$log_dir/stream.log" | tail -12 >&2 || true
    fi
else
    case_line attach-chain FAIL
    echo "wlan-bridge-test: the run failed, see $log_dir/stream.log" >&2
    grep '^WLANBRIDGE' "$log_dir/stream.log" | tail -8 >&2 || true
fi

# ---- the red controls (each must fail, naming its case) -----------------------

# fault -> the case the fault must invalidate
for pair in no-filter:B2 tamper-auth:B4 double-free-skb:B9 no-manual:B11 no-rearm:B12; do
    fault=${pair%%:*}
    want=${pair##*:}
    if timeout 120 "$bin" --fault "$fault" > "$log_dir/fault-$fault.log" 2>&1; then
        case_line "red-$fault" FAIL
        echo "wlan-bridge-test: the $fault control passed (it must fail)" >&2
    elif grep -q "^WLANBRIDGE: $want FAIL" "$log_dir/fault-$fault.log"; then
        case_line "red-$fault" PASS
    else
        case_line "red-$fault" FAIL
        echo "wlan-bridge-test: the $fault control failed without naming $want" >&2
        grep '^WLANBRIDGE' "$log_dir/fault-$fault.log" | tail -4 >&2 || true
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
        echo "wlan-bridge-test: an unknown fault name must be a usage error" >&2
    fi
fi

exit "$failures"

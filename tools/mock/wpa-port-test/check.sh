#!/bin/sh
# @file    check.sh
# @brief   Wrapper for the wpa_supplicant port suite: run both backend
#          binaries, map their anchor lines to container cases, then prove
#          the suite can go red in two ways.
# @author  zhugengyu
# @date    09.10.2026
#
# The binaries (tests/host/wpa-port-test) link the vendored supplicant, the
# first-party port layer and a scripted peer; each prints one
# `WPAPORT: <id> PASS|FAIL <detail>` line per case plus a SUITE sentinel.
# The ThreadX run is the case evidence; the FreeRTOS run must produce the
# same verdicts from the same inputs (the single adapter, two kernels).

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
log_dir=$root/tools/mock/build/wpa-port-test
mkdir -p "$log_dir"

thx_bin=$root/tests/host/wpa-port-test/build/wpa-port-test-threadx
fr_bin=$root/tests/host/wpa-port-test/build/wpa-port-test-freertos
if [ ! -x "$thx_bin" ] || [ ! -x "$fr_bin" ]; then
    echo "MOCK-BUILD wpa-port-test: binaries were not built" >&2
    exit 2
fi

failures=0

case_line() {
    printf 'CASE wpa-port-test/%s %s\n' "$1" "$2"
    [ "$2" = "PASS" ] || failures=$((failures + 1))
}

# ---- the suite itself (ThreadX backend) ---------------------------------------

if timeout 180 "$thx_bin" --mode main > "$log_dir/stream.log" 2>&1; then
    pass=$(grep -c '^WPAPORT: [a-z0-9.-]* PASS' "$log_dir/stream.log" || true)
    if [ "$pass" = 11 ] &&
        grep -q '^WPAPORT: SUITE PASS cases=11$' "$log_dir/stream.log"; then
        case_line vectors PASS
        case_line smoke-start PASS
        case_line scan-pull-model PASS
        case_line fourway-completion PASS
        case_line bad-frame-discarded PASS
        case_line open-reconnect PASS
        case_line job-timeout PASS
    else
        case_line vectors FAIL
        echo "wpa-port-test: the ThreadX run did not report eleven passing cases" >&2
        grep '^WPAPORT' "$log_dir/stream.log" | tail -12 >&2 || true
    fi
else
    case_line vectors FAIL
    echo "wpa-port-test: the ThreadX run failed, see $log_dir/stream.log" >&2
    grep '^WPAPORT' "$log_dir/stream.log" | tail -8 >&2 || true
fi

# ---- the FreeRTOS backend: same inputs, same verdicts -------------------------

if timeout 180 "$fr_bin" --mode main > "$log_dir/stream-freertos.log" 2>&1; then
    if grep -q '^WPAPORT: SUITE PASS cases=11$' "$log_dir/stream-freertos.log"; then
        case_line freertos-backend PASS
    else
        case_line freertos-backend FAIL
        echo "wpa-port-test: the FreeRTOS run did not pass all cases" >&2
    fi
else
    case_line freertos-backend FAIL
    echo "wpa-port-test: the FreeRTOS run failed, see $log_dir/stream-freertos.log" >&2
    grep '^WPAPORT' "$log_dir/stream-freertos.log" | tail -8 >&2 || true
fi

# ---- the fail-safe shape -------------------------------------------------------

if timeout 60 "$thx_bin" --mode failsafe > "$log_dir/failsafe.log" 2>&1 &&
    grep -q '^WPAPORT: smoke.failsafe PASS' "$log_dir/failsafe.log"; then
    case_line failsafe PASS
else
    case_line failsafe FAIL
    echo "wpa-port-test: the no-backend fail-safe shape did not pass" >&2
fi

# ---- the red controls (each must fail, naming its case) ------------------------

# a wrong PSK on the peer's side: M2's MIC cannot verify, so the peer never
# sends M3 - the fourway case must name the failure
if timeout 120 "$thx_bin" --fault tamper-psk > "$log_dir/fault-tamper-psk.log" 2>&1; then
    case_line red-tamper-psk FAIL
    echo "wpa-port-test: the tamper-psk control passed (it must fail)" >&2
elif grep -q '^WPAPORT: smoke.fourway FAIL' "$log_dir/fault-tamper-psk.log"; then
    case_line red-tamper-psk PASS
else
    case_line red-tamper-psk FAIL
    echo "wpa-port-test: the tamper-psk control failed without naming smoke.fourway" >&2
fi

# a survey that never completes: the pull model starves and the ladder
# fails at the scan-pull case
if timeout 120 "$thx_bin" --fault no-scan-done > "$log_dir/fault-no-scan-done.log" 2>&1; then
    case_line red-no-scan-done FAIL
    echo "wpa-port-test: the no-scan-done control passed (it must fail)" >&2
elif grep -q '^WPAPORT: smoke.scan-pull FAIL' "$log_dir/fault-no-scan-done.log"; then
    case_line red-no-scan-done PASS
else
    case_line red-no-scan-done FAIL
    echo "wpa-port-test: the no-scan-done control failed without naming smoke.scan-pull" >&2
fi

# an unknown fault name is a usage error, not a red mode
if timeout 20 "$thx_bin" --fault bogus > "$log_dir/fault-bogus.log" 2>&1; then
    case_line fault-usage FAIL
else
    if grep -q 'unknown fault' "$log_dir/fault-bogus.log"; then
        case_line fault-usage PASS
    else
        case_line fault-usage FAIL
        echo "wpa-port-test: an unknown fault name must be a usage error" >&2
    fi
fi

exit "$failures"

#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# @file    check.sh
# @brief   Wrapper for the cfg80211 substitute suite: run the binary, map its
#          anchor lines to container cases, then prove the suite can go red.
# @design  the red-control wrapper pattern, mirroring the sibling body suite
#          (tools/mock/linuxkpi-body-test/check.sh:1-40): run the binary, turn its
#          anchor lines into container cases, then run the --fault mode that must
#          fail.
# @author  zhugengyu
# @date    12.10.2026
#
# The binary (tests/host/linuxkpi-cfg80211-test) links the substitute body and
# prints one `CFG: <id> PASS|FAIL <detail>` line per case plus a `CFG: SUITE ...`
# sentinel. This wrapper turns those into cases and runs the --fault mode, which
# must fail: a suite that cannot go red proves nothing.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
log_dir=$root/tools/mock/build/linuxkpi-cfg80211-test
mkdir -p "$log_dir"

bin=$root/tests/host/linuxkpi-cfg80211-test/build/linuxkpi-cfg80211-test
if [ ! -x "$bin" ]; then
    echo "MOCK-BUILD linuxkpi-cfg80211-test: $bin was not built" >&2
    exit 2
fi

failures=0

case_line() {
    printf 'CASE linuxkpi-cfg80211-test/%s %s\n' "$1" "$2"
    [ "$2" = "PASS" ] || failures=$((failures + 1))
}

# ---- the suite itself ---------------------------------------------------------

if timeout 60 "$bin" > "$log_dir/stream.log" 2>&1; then
    pass=$(grep -c '^CFG: C[0-9]* PASS' "$log_dir/stream.log" || true)
    if [ "$pass" = 15 ] &&
        grep -q '^CFG: SUITE PASS cases=15$' "$log_dir/stream.log" &&
        ! grep -q '^CFG: C[0-9]* FAIL' "$log_dir/stream.log"; then
        case_line wiphy-lifecycle PASS
        case_line bss-table-elements PASS
        case_line connect-rsn-params PASS
        case_line connect-wpa1-map PASS
        case_line connect-open PASS
        case_line connect-refusals PASS
        case_line set-key PASS
        case_line disconnect PASS
        case_line scan-request PASS
        case_line truncated-element PASS
        case_line ap-start-order PASS
        case_line ap-stop PASS
        case_line ap-beacon-head PASS
        case_line ap-beacon-tail PASS
        case_line sta-events PASS
    else
        case_line wiphy-lifecycle FAIL
        echo "linuxkpi-cfg80211-test: the run did not report fifteen passing cases ($pass)" >&2
        tail -8 "$log_dir/stream.log" >&2 || true
    fi
else
    case_line wiphy-lifecycle FAIL
    echo "linuxkpi-cfg80211-test: the run failed, see $log_dir/stream.log" >&2
    tail -8 "$log_dir/stream.log" >&2 || true
fi

# ---- the red controls (must fail) ----------------------------------------------

if timeout 30 "$bin" --fault weak-expect > "$log_dir/fault-weak-expect.log" 2>&1; then
    case_line red-weak-expect FAIL
    echo "linuxkpi-cfg80211-test: the weak-expect control passed (it must fail)" >&2
elif grep -q '^CFG: C3 FAIL' "$log_dir/fault-weak-expect.log"; then
    case_line red-weak-expect PASS
else
    case_line red-weak-expect FAIL
    echo "linuxkpi-cfg80211-test: the weak-expect control failed without naming C3" >&2
    tail -3 "$log_dir/fault-weak-expect.log" >&2 || true
fi

# the ordering control: with the wrong expectation for the interface type the op
# sees, C11 must be the case that fails
if timeout 30 "$bin" --fault ap-order > "$log_dir/fault-ap-order.log" 2>&1; then
    case_line red-ap-order FAIL
    echo "linuxkpi-cfg80211-test: the ap-order control passed (it must fail)" >&2
elif grep -q '^CFG: C11 FAIL' "$log_dir/fault-ap-order.log"; then
    case_line red-ap-order PASS
else
    case_line red-ap-order FAIL
    echo "linuxkpi-cfg80211-test: the ap-order control failed without naming C11" >&2
    tail -3 "$log_dir/fault-ap-order.log" >&2 || true
fi

# the payload control: one byte less of beacon tail must fail C11
if timeout 30 "$bin" --fault ap-head > "$log_dir/fault-ap-head.log" 2>&1; then
    case_line red-ap-head FAIL
    echo "linuxkpi-cfg80211-test: the ap-head control passed (it must fail)" >&2
elif grep -q '^CFG: C11 FAIL' "$log_dir/fault-ap-head.log"; then
    case_line red-ap-head PASS
else
    case_line red-ap-head FAIL
    echo "linuxkpi-cfg80211-test: the ap-head control failed without naming C11" >&2
    tail -3 "$log_dir/fault-ap-head.log" >&2 || true
fi

# the station-event control: with one event too many expected, C15 must be the
# case that fails
if timeout 30 "$bin" --fault sta-event > "$log_dir/fault-sta-event.log" 2>&1; then
    case_line red-sta-event FAIL
    echo "linuxkpi-cfg80211-test: the sta-event control passed (it must fail)" >&2
elif grep -q '^CFG: C15 FAIL' "$log_dir/fault-sta-event.log"; then
    case_line red-sta-event PASS
else
    case_line red-sta-event FAIL
    echo "linuxkpi-cfg80211-test: the sta-event control failed without naming C15" >&2
    tail -3 "$log_dir/fault-sta-event.log" >&2 || true
fi

# the element-walk control: with one element's declared length shortened, the
# walk in C14 must be the case that fails
if timeout 30 "$bin" --fault ap-ie-len > "$log_dir/fault-ap-ie-len.log" 2>&1; then
    case_line red-ap-ie-len FAIL
    echo "linuxkpi-cfg80211-test: the ap-ie-len control passed (it must fail)" >&2
elif grep -q '^CFG: C14 FAIL' "$log_dir/fault-ap-ie-len.log"; then
    case_line red-ap-ie-len PASS
else
    case_line red-ap-ie-len FAIL
    echo "linuxkpi-cfg80211-test: the ap-ie-len control failed without naming C14" >&2
    tail -3 "$log_dir/fault-ap-ie-len.log" >&2 || true
fi

# the beacon-oracle control: with one byte of the expectation corrupted, the
# byte-for-byte comparison in C14 must be the case that fails
if timeout 30 "$bin" --fault ap-beacon > "$log_dir/fault-ap-beacon.log" 2>&1; then
    case_line red-ap-beacon FAIL
    echo "linuxkpi-cfg80211-test: the ap-beacon control passed (it must fail)" >&2
elif grep -q '^CFG: C14 FAIL' "$log_dir/fault-ap-beacon.log"; then
    case_line red-ap-beacon PASS
else
    case_line red-ap-beacon FAIL
    echo "linuxkpi-cfg80211-test: the ap-beacon control failed without naming C14" >&2
    tail -3 "$log_dir/fault-ap-beacon.log" >&2 || true
fi

# an unknown fault name is a usage error, not a red mode
if timeout 20 "$bin" --fault bogus > "$log_dir/fault-bogus.log" 2>&1; then
    case_line fault-usage FAIL
elif grep -q 'unknown fault' "$log_dir/fault-bogus.log"; then
    case_line fault-usage PASS
else
    case_line fault-usage FAIL
    echo "linuxkpi-cfg80211-test: an unknown fault name must be a usage error" >&2
fi

exit "$failures"

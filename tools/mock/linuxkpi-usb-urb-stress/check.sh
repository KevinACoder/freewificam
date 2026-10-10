#!/bin/sh
# @file    check.sh
# @brief   Wrapper for the linuxkpi-usb-urb-stress suite: run the matrix, the seeded
#          streams and the red control, and map each into container case lines.
# @author  zhugengyu
# @date    07.10.2026
#
# The harness (tests/host/lkpi-usb-stress-mock) owns the mechanism: the 28-cell
# injection matrix, the seeded operation streams and the double-completion
# self-proof.  This wrapper runs those shapes and turns the observed summaries,
# the determinism check and the failure set into case lines.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
log_dir=$root/tools/mock/build/linuxkpi-usb-urb-stress
mkdir -p "$log_dir"

bin=$root/tests/host/linuxkpi-usb-urb-stress/build/linuxkpi-usb-urb-stress
if [ ! -x "$bin" ]; then
    echo "MOCK-BUILD linuxkpi-linuxkpi-usb-urb-stress: $bin was not built" >&2
    exit 2
fi

failures=0

case_line() {
    printf 'CASE linuxkpi-usb-urb-stress/%s %s\n' "$1" "$2"
    [ "$2" = "PASS" ] || failures=$((failures + 1))
}

# ---- the matrix and the short seeded streams (one invocation) -----------------
#
# The fuzz mode runs the matrix first, so a single invocation yields both: the
# matrix verdicts and the stream verdicts.  The full-scale streams (200000
# operations per seed) are the milestone-scale run; the container uses a shorter
# stream with the same determinism contract to keep a change cycle fast.

if "$bin" --fuzz --iters 20000 > "$log_dir/stream.log" 2>&1; then
    if grep -q '^URBSTRESS: matrix cells=32 pass=32 fail=0$' "$log_dir/stream.log"; then
        case_line matrix PASS
        for fam in B1 B2 B3 B4 B5 B6 B7; do
            case_line "matrix-$fam" PASS
        done
    else
        case_line matrix FAIL
        echo "linuxkpi-usb-urb-stress: matrix summary unexpected" >&2
        tail -5 "$log_dir/stream.log" >&2 || true
    fi

    if grep -q '^URBSTRESS: pairing ' "$log_dir/stream.log" &&
        grep -q '^URBSTRESS: balance ' "$log_dir/stream.log"; then
        case_line pairing-balance PASS
    else
        case_line pairing-balance FAIL
        echo "linuxkpi-usb-urb-stress: no pairing/balance anchor" >&2
    fi

    if grep -q '^URBSTRESS: fuzz seed=[123] iters=20000 .* ok=1 ' "$log_dir/stream.log"; then
        case_line seeds PASS
    else
        case_line seeds FAIL
        echo "linuxkpi-usb-urb-stress: short stream did not pass" >&2
        tail -5 "$log_dir/stream.log" >&2 || true
    fi
else
    case_line matrix FAIL
    case_line seeds FAIL
    echo "linuxkpi-usb-urb-stress: run failed, see $log_dir/run.log" >&2
    tail -5 "$log_dir/stream.log" >&2 || true
fi

# ---- the flow-control family (B7) ---------------------------------------------

# The burst cells run alone (the matrix run above already covers them; these
# lines give the family its own named verdicts and keep its evidence in its own
# log).  B7.1 is the firmware-download shape, B7.2 the live control that proves
# B7.1's detector can fire, B7.3/B7.4 the refused-submit completion contract.
for cell in B7.1 B7.2 B7.3 B7.4; do
    name=$(printf 'flow-%s' "$(printf '%s' "$cell" | tr 'A-Z.' 'a-z-')")
    if "$bin" --cell "$cell" > "$log_dir/$name.log" 2>&1; then
        case_line "$name" PASS
    else
        case_line "$name" FAIL
        tail -3 "$log_dir/$name.log" >&2 || true
    fi
done

# The must-go-red control for the family: the parking that keeps a transient
# shortage away from the driver is one branch in the engine.  The control
# builds the same harness against a copy of the engine whose shortage branch is
# disabled; the burst must then surface the shortage and B7.1 must go red with
# a first witness.
mut=$log_dir/mutated-engine
rm -rf "$mut"
mkdir -p "$mut"
sed 's/if (ret == -LKPI_USB_BE_ERR_NOMEM)/if (0)/' \
    "$root/components/linuxkpi/usb/linuxkpi_usb.c" > "$mut/linuxkpi_usb.c"
if grep -q 'if (0)' "$mut/linuxkpi_usb.c" &&
    make -C "$root/tests/host/linuxkpi-usb-urb-stress" \
        ENGINE_DIR="$mut" BUILD_DIR="$mut/build" > "$log_dir/mutated-build.log" 2>&1; then
    if "$mut/build/linuxkpi-usb-urb-stress" --cell B7.1 > "$log_dir/mutated-b7-1.log" 2>&1; then
        case_line flow-return-red-control FAIL
        echo "linuxkpi-usb-urb-stress: the no-parking engine passed B7.1 (it must fail)" >&2
    else
        if grep -q '^URBSTRESS: first-witness' "$log_dir/mutated-b7-1.log"; then
            case_line flow-return-red-control PASS
        else
            case_line flow-return-red-control FAIL
            echo "linuxkpi-usb-urb-stress: the red control failed without a witness" >&2
            tail -3 "$log_dir/mutated-b7-1.log" >&2 || true
        fi
    fi
else
    case_line flow-return-red-control FAIL
    echo "linuxkpi-usb-urb-stress: the mutated engine did not build" >&2
    tail -5 "$log_dir/mutated-build.log" >&2 || true
fi

# ---- the reverse controls -----------------------------------------------------

# The double-completion injection must be caught by the exactly-once rule with
# the failure set naming that one urb.
if "$bin" --fault double-complete > "$log_dir/double-complete.log" 2>&1; then
    case_line double-complete FAIL
    echo "linuxkpi-usb-urb-stress: the double-completion control passed (it must fail)" >&2
else
    if grep -q '^URBSTRESS: first-witness' "$log_dir/double-complete.log" &&
        grep -q '^URBSTRESS: failset ' "$log_dir/double-complete.log"; then
        case_line double-complete PASS
    else
        case_line double-complete FAIL
        echo "linuxkpi-usb-urb-stress: the control failed without a witness/failset" >&2
        tail -5 "$log_dir/double-complete.log" >&2 || true
    fi
fi

# The control run names its repro command, and the replay path must reproduce the
# seeded stream's digest exactly (a replay that disagrees is a broken repro path).
if grep -q '^URBSTRESS: repro linuxkpi-usb-urb-stress --fault double-complete$' "$log_dir/double-complete.log"; then
    seed1=$(sed -n 's/^URBSTRESS: fuzz seed=1 iters=20000 ops=20000 digest=\([0-9a-f]*\) ok=1 .*$/\1/p' "$log_dir/stream.log" | head -1)
    if [ -n "$seed1" ] && "$bin" --replay 1 20000 > "$log_dir/replay.log" 2>&1 &&
        grep -q "^URBSTRESS: replay seed=1 .*digest=$seed1$" "$log_dir/replay.log"; then
        case_line replay PASS
    else
        case_line replay FAIL
        echo "linuxkpi-usb-urb-stress: the replay digest disagrees with the stream" >&2
        tail -3 "$log_dir/replay.log" >&2 || true
    fi
else
    case_line replay FAIL
    echo "linuxkpi-usb-urb-stress: no repro command in the control run" >&2
fi

exit "$failures"

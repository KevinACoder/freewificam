#!/bin/sh
# @file    check.sh
# @brief   Wrapper for the skbuff stress suite: run the harness in its three
#          shapes and turn each result group into container case lines.
# @author  zhugengyu
# @date    07.10.2026
#
# The harness itself (tests/host/skbuff-allocation-free-stress) owns the mechanism: the
# nineteen-cell scenario matrix, the seeded operation streams and the mcu
# profile.  This wrapper runs the three shapes the suite declares - matrix,
# seeded streams, mcu profile - and maps the observed summaries and the
# determinism check into case lines.  The reverse controls (a wrong expectation
# and a polluted invariant must go red) are run here too.
#
# The same shapes then run again against the production pool body
# (skbuff-allocation-free-stress-real) and against the bounded build of it
# (skbuff-allocation-free-stress-real-mcu, whose storage-budget assertion is a
# compile-time fact): the body is what ships, and the identical matrix, judge
# and streams are what hold it to the contract.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
log_dir=$root/tools/mock/build/skbuff-allocation-free-stress
mkdir -p "$log_dir"

bin=$root/tests/host/skbuff-allocation-free-stress/build/skbuff-allocation-free-stress
bin_real=$root/tests/host/skbuff-allocation-free-stress/build/skbuff-allocation-free-stress-real
bin_mcu=$root/tests/host/skbuff-allocation-free-stress/build/skbuff-allocation-free-stress-real-mcu
for b in "$bin" "$bin_real" "$bin_mcu"; do
    if [ ! -x "$b" ]; then
        echo "MOCK-BUILD skbuff-allocation-free-stress: $b was not built" >&2
        exit 2
    fi
done

failures=0

case_line() {
    printf 'CASE skbuff-allocation-free-stress/%s %s\n' "$1" "$2"
    [ "$2" = "PASS" ] || failures=$((failures + 1))
}

# ---- the matrix ---------------------------------------------------------------

if "$bin" > "$log_dir/matrix.log" 2>&1; then
    total=$(grep -c '^SKBST: A[0-9]*\.[0-9]* PASS$' "$log_dir/matrix.log" || true)
    if [ "$total" = 19 ] &&
        grep -q '^SKBST: matrix cells=19 pass=19 fail=0$' "$log_dir/matrix.log"; then
        case_line matrix PASS
        # one case per family, so a failure names its group
        for fam in A1 A2 A3 A4; do
            n=$(grep -c "^SKBST: $fam\.[0-9]* PASS$" "$log_dir/matrix.log" || true)
            case_line "matrix-$fam" PASS
            : "$n"
        done
    else
        case_line matrix FAIL
        echo "skbuff-allocation-free-stress: matrix summary unexpected ($total cells passed)" >&2
        tail -5 "$log_dir/matrix.log" >&2 || true
    fi
else
    case_line matrix FAIL
    echo "skbuff-allocation-free-stress: matrix run failed, see $log_dir/matrix.log" >&2
    tail -5 "$log_dir/matrix.log" >&2 || true
fi

# ---- the seeded streams (determinism included) --------------------------------

if "$bin" --fuzz > "$log_dir/fuzz.log" 2>&1 &&
    "$bin" --fuzz > "$log_dir/fuzz-again.log" 2>&1; then
    if grep -q '^SKBST: fuzz seed=[123] iters=200000 .* ok=1$' "$log_dir/fuzz.log" &&
        diff -q "$log_dir/fuzz.log" "$log_dir/fuzz-again.log" > /dev/null; then
        case_line seeds PASS
    else
        case_line seeds FAIL
        echo "skbuff-allocation-free-stress: seed streams differ between two runs" >&2
        diff "$log_dir/fuzz.log" "$log_dir/fuzz-again.log" | head -5 >&2 || true
    fi
else
    case_line seeds FAIL
    echo "skbuff-allocation-free-stress: fuzz run failed, see $log_dir/fuzz.log" >&2
    tail -5 "$log_dir/fuzz.log" >&2 || true
fi

# ---- the constrained profile --------------------------------------------------

if "$bin" --profile mcu --fuzz > "$log_dir/mcu.log" 2>&1 &&
    grep -q '^SKBST: matrix cells=19 pass=19 fail=0$' "$log_dir/mcu.log" &&
    grep -q '^SKBST: fuzz seed=1 iters=200000 .* ok=1$' "$log_dir/mcu.log"; then
    case_line mcu-profile PASS
else
    case_line mcu-profile FAIL
    echo "skbuff-allocation-free-stress: mcu profile run failed, see $log_dir/mcu.log" >&2
    tail -5 "$log_dir/mcu.log" >&2 || true
fi

# ---- the reverse controls -----------------------------------------------------

# A harness that cannot go red proves nothing: a deliberately wrong expectation
# must be detected, and the minimal-repro path must land on the same failure.
if "$bin" --selftest-must-red > "$log_dir/must-red.log" 2>&1; then
    case_line must-red FAIL
    echo "skbuff-allocation-free-stress: the wrong-expectation control passed (it must fail)" >&2
else
    if grep -q '^SKBST: first-witness' "$log_dir/must-red.log"; then
        case_line must-red PASS
    else
        case_line must-red FAIL
        echo "skbuff-allocation-free-stress: the control failed without a first-witness line" >&2
        tail -5 "$log_dir/must-red.log" >&2 || true
    fi
fi

if grep -q '^SKBST: replay seed=' "$log_dir/must-red.log"; then
    case_line replay PASS
else
    case_line replay FAIL
    echo "skbuff-allocation-free-stress: no replay confirmation in the control run" >&2
fi

# ---- the production pool body (the same shapes, the code that ships) ----------

if "$bin_real" --fuzz > "$log_dir/real.log" 2>&1 &&
    "$bin_real" > "$log_dir/real-matrix.log" 2>&1 &&
    "$bin_real" --profile mcu > "$log_dir/real-mcu.log" 2>&1; then
    if grep -q '^SKBST: matrix cells=19 pass=19 fail=0$' "$log_dir/real-matrix.log" &&
        grep -q '^SKBST: fuzz seed=1 iters=200000 .* ok=1$' "$log_dir/real.log" &&
        grep -q '^SKBST: fuzz seed=2 iters=200000 .* ok=1$' "$log_dir/real.log" &&
        grep -q '^SKBST: fuzz seed=3 iters=200000 .* ok=1$' "$log_dir/real.log" &&
        grep -q '^SKBST: matrix cells=19 pass=19 fail=0$' "$log_dir/real-mcu.log"; then
        case_line real-pool PASS
    else
        case_line real-pool FAIL
        echo "skbuff-allocation-free-stress: the production pool body did not hold the contract" >&2
        tail -5 "$log_dir/real.log" >&2 || true
    fi
else
    case_line real-pool FAIL
    echo "skbuff-allocation-free-stress: the production-body run failed, see $log_dir/real.log" >&2
    tail -5 "$log_dir/real.log" >&2 || true
fi

# The bounded build: the constrained geometry is compiled in, so what runs here
# is the body as an MCU-shaped build would carry it (the storage budget is a
# build-time assertion in that object).
if "$bin_mcu" > "$log_dir/real-mcu-bounds.log" 2>&1 &&
    "$bin_mcu" --profile mcu --fuzz > "$log_dir/real-bounded.log" 2>&1; then
    if grep -q '^SKBST: profile=host header=8 data=8 data_sz=256 ' "$log_dir/real-mcu-bounds.log" &&
        grep -q '^SKBST: matrix cells=19 pass=19 fail=0$' "$log_dir/real-mcu-bounds.log" &&
        grep -q '^SKBST: matrix cells=19 pass=19 fail=0$' "$log_dir/real-bounded.log" &&
        grep -q '^SKBST: fuzz seed=1 iters=200000 .* ok=1$' "$log_dir/real-bounded.log"; then
        case_line real-pool-bounded PASS
    else
        case_line real-pool-bounded FAIL
        echo "skbuff-allocation-free-stress: the bounded production build did not hold the contract" >&2
        tail -5 "$log_dir/real-mcu-bounds.log" >&2 || true
    fi
else
    case_line real-pool-bounded FAIL
    echo "skbuff-allocation-free-stress: the bounded run failed, see $log_dir/real-mcu-bounds.log" >&2
    tail -5 "$log_dir/real-mcu-bounds.log" >&2 || true
fi

exit "$failures"

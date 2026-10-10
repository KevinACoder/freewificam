#!/bin/sh
# @file    check.sh
# @brief   Wrapper for the linuxkpi-wiphy-work-test suite: run the cases and
#          the red control, and map each into container case lines.
#
# The harness owns the mechanism (tests/host/linuxkpi-wiphy-work-test): the
# polling-budget cases and the completion-layout case.  This wrapper runs it and
# adds the control that proves the cases can fail: the same harness built
# against a mutated copy of linux/iopoll.h whose budget expires on the first
# round must turn the slow-operation case red.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
log_dir=$root/tools/mock/build/linuxkpi-wiphy-work-test
mkdir -p "$log_dir"

bin=$root/tests/host/linuxkpi-wiphy-work-test/build/linuxkpi-wiphy-work-test
if [ ! -x "$bin" ]; then
    echo "MOCK-BUILD linuxkpi-wiphy-work-test: $bin was not built" >&2
    exit 2
fi

failures=0

case_line() {
    printf 'CASE linuxkpi-wiphy-work-test/%s %s\n' "$1" "$2"
    [ "$2" = "PASS" ] || failures=$((failures + 1))
}

# ---- the cases ----------------------------------------------------------------

if "$bin" > "$log_dir/run.log" 2>&1; then
    if grep -q '^WIPHYWORK: summary cases=12 failed=0$' "$log_dir/run.log"; then
        case_line queue-cancel-flush PASS
        case_line delayed-and-guards PASS
        case_line timer-object-reuse PASS
    else
        case_line queue-cancel-flush FAIL
        case_line delayed-and-guards FAIL
        case_line timer-object-reuse FAIL
        echo "linuxkpi-wiphy-work-test: summary unexpected" >&2
        tail -8 "$log_dir/run.log" >&2 || true
    fi
else
    case_line queue-cancel-flush FAIL
    case_line delayed-and-guards FAIL
    case_line timer-object-reuse FAIL
    echo "linuxkpi-wiphy-work-test: run failed" >&2
    tail -8 "$log_dir/run.log" >&2 || true
fi

# ---- the red control ----------------------------------------------------------

# A cancel must unlink the work before it can run.  The control builds the same
# harness against a copy of the engine whose cancel no longer unlinks; the
# cancel-before-run case must then fail (the work runs after all).  Without
# this, "cancelled, so it never ran" would be a claim no implementation could
# contradict.
mut=$log_dir/mutated-engine
rm -rf "$mut"
mkdir -p "$mut"
sed 's/if (!list_empty(&work->entry))/if (0)/' \
    "$root/components/linuxkpi/linuxkpi_wiphy_work.c" > "$mut/linuxkpi_wiphy_work.c"
if grep -q 'if (0)' "$mut/linuxkpi_wiphy_work.c" &&
    make -C "$root/tests/host/linuxkpi-wiphy-work-test" \
        ENGINE_EXTRA_SRC="$mut/linuxkpi_wiphy_work.c" \
        BUILD_DIR="$mut/build" > "$log_dir/mutated-build.log" 2>&1; then
    if timeout -k 5 120 "$mut/build/linuxkpi-wiphy-work-test" > "$log_dir/mutated-run.log" 2>&1; then
        case_line cancel-red-control FAIL
        echo "linuxkpi-wiphy-work-test: the never-unlinking engine passed (it must fail)" >&2
    else
        if grep -q '^WIPHYWORK: cancel-before-run FAIL' "$log_dir/mutated-run.log"; then
            case_line cancel-red-control PASS
        else
            case_line cancel-red-control FAIL
            echo "linuxkpi-wiphy-work-test: the red control failed elsewhere" >&2
            tail -4 "$log_dir/mutated-run.log" >&2 || true
        fi
    fi
else
    case_line cancel-red-control FAIL
    echo "linuxkpi-wiphy-work-test: the mutated engine did not build" >&2
    tail -5 "$log_dir/mutated-build.log" >&2 || true
fi

# A delayed work's timer object is created once and reused.  The control builds
# the same harness against a copy of the core body whose timer setup always makes
# a fresh object: the timer-object case must then fail (the handle changes under
# it).  Without this, "the slot is not leaked" would be a claim no implementation
# could contradict.
mut2=$log_dir/mutated-core
rm -rf "$mut2"
mkdir -p "$mut2"
sed 's/if (shell->inited) {/if (0) {/' \
    "$root/components/linuxkpi/linuxkpi_core.c" > "$mut2/linuxkpi_core.c"
if grep -q 'if (0) {' "$mut2/linuxkpi_core.c" &&
    make -C "$root/tests/host/linuxkpi-wiphy-work-test" \
        CORE_EXTRA_SRC="$mut2/linuxkpi_core.c" \
        BUILD_DIR="$mut2/build" > "$log_dir/mutated-core-build.log" 2>&1; then
    if timeout -k 5 120 "$mut2/build/linuxkpi-wiphy-work-test" > "$log_dir/mutated-core-run.log" 2>&1; then
        case_line timer-reuse-red-control FAIL
        echo "linuxkpi-wiphy-work-test: the recreating core passed (it must fail)" >&2
    else
        if grep -q '^WIPHYWORK: timer-object-reused FAIL' "$log_dir/mutated-core-run.log"; then
            case_line timer-reuse-red-control PASS
        else
            case_line timer-reuse-red-control FAIL
            echo "linuxkpi-wiphy-work-test: the red control failed elsewhere" >&2
            tail -4 "$log_dir/mutated-core-run.log" >&2 || true
        fi
    fi
else
    case_line timer-reuse-red-control FAIL
    echo "linuxkpi-wiphy-work-test: the mutated core did not build" >&2
    tail -5 "$log_dir/mutated-core-build.log" >&2 || true
fi

exit "$failures"

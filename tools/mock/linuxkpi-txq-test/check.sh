#!/bin/sh
# @file    check.sh
# @brief   Wrapper for the linuxkpi-txq-test suite: run the scheduler cases and
#          the red control, and map each into container case lines.
#
# The harness owns the mechanism (tests/host/linuxkpi-txq-test): the txq entry
# points run against the real engine and the real skb pool.  This wrapper runs
# it, groups the cases into three container case lines, and adds the control
# that proves they can fail: the same harness built against an engine whose
# next_txq no longer skips a queue this round already served must turn the
# round-guard case red.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
log_dir=$root/tools/mock/build/linuxkpi-txq-test
mkdir -p "$log_dir"

bin=$root/tests/host/linuxkpi-txq-test/build/linuxkpi-txq-test
if [ ! -x "$bin" ]; then
    echo "MOCK-BUILD linuxkpi-txq-test: $bin was not built" >&2
    exit 2
fi

failures=0

case_line() {
    printf 'CASE linuxkpi-txq-test/%s %s\n' "$1" "$2"
    [ "$2" = "PASS" ] || failures=$((failures + 1))
}

# a group is one case line, backed by the named anchor lines all being PASS
group() {
    name=$1
    shift
    if grep -q '^TXQ: summary cases=17 failed=0$' "$log_dir/run.log"; then
        ok=1
        for c in "$@"; do
            grep -q "^TXQ: $c PASS" "$log_dir/run.log" || ok=0
        done
        if [ "$ok" = "1" ]; then
            case_line "$name" PASS
        else
            case_line "$name" FAIL
            echo "linuxkpi-txq-test: $name: an anchor line is missing or failed" >&2
            grep '^TXQ: ' "$log_dir/run.log" >&2 || true
        fi
    else
        case_line "$name" FAIL
        echo "linuxkpi-txq-test: summary unexpected" >&2
        tail -8 "$log_dir/run.log" >&2 || true
    fi
}

# ---- the cases ----------------------------------------------------------------

if "$bin" > "$log_dir/run.log" 2> "$log_dir/run.err"; then
    group queue-allocation-and-map \
        alloc-shape-and-drv-area sta-queue-ac-map
    group scheduling-rounds \
        schedule-once-per-round no-round-no-handout empty-queue-needs-force \
        return-skip-empty-drops round-robin-across-queues \
        return-force-requeues-next-round
    group dequeue-and-lifecycle \
        dequeue-fifo-and-depth dequeue-tags-the-vif \
        purge-drops-and-unschedules free-sta-releases-its-queues \
        free-vif-sweeps-stations foreign-txq-refused table-full-refused \
        table-slot-reusable table-drained
else
    case_line queue-allocation-and-map FAIL
    case_line scheduling-rounds FAIL
    case_line dequeue-and-lifecycle FAIL
    echo "linuxkpi-txq-test: run failed" >&2
    tail -8 "$log_dir/run.log" >&2 || true
fi

# ---- the red control ----------------------------------------------------------

# A queue is handed to the driver once per scheduling round.  The control builds
# the same harness against a copy of the engine whose next_txq no longer tests
# the round it was last served in; the return-force case must then fail (the
# queue comes back inside the same round).  Without this, "served once per
# round" would be a claim no implementation could contradict.
mut=$log_dir/mutated-engine
rm -rf "$mut"
mkdir -p "$mut"
sed 's/if (t->round == eng.round\[ac\])/if (0)/' \
    "$root/components/linuxkpi/linuxkpi_txq.c" > "$mut/linuxkpi_txq.c"
if grep -q 'if (0)' "$mut/linuxkpi_txq.c" &&
    make -C "$root/tests/host/linuxkpi-txq-test" \
        ENGINE_EXTRA_SRC="$mut/linuxkpi_txq.c" \
        BUILD_DIR="$mut/build" > "$log_dir/mutated-build.log" 2>&1; then
    if timeout -k 5 120 "$mut/build/linuxkpi-txq-test" > "$log_dir/mutated-run.log" 2>&1; then
        case_line round-guard-red-control FAIL
        echo "linuxkpi-txq-test: the round-blind engine passed (it must fail)" >&2
    else
        if grep -q '^TXQ: return-force-requeues-next-round FAIL' "$log_dir/mutated-run.log"; then
            case_line round-guard-red-control PASS
        else
            case_line round-guard-red-control FAIL
            echo "linuxkpi-txq-test: the red control failed elsewhere" >&2
            grep '^TXQ: ' "$log_dir/mutated-run.log" >&2 || true
        fi
    fi
else
    case_line round-guard-red-control FAIL
    echo "linuxkpi-txq-test: the mutated engine did not build" >&2
    tail -5 "$log_dir/mutated-build.log" >&2 || true
fi

exit "$failures"

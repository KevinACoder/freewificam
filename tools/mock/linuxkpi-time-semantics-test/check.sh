#!/bin/sh
# @file    check.sh
# @brief   Wrapper for the linuxkpi-time-semantics-test suite: run the cases and
#          the red control, and map each into container case lines.
#
# The harness owns the mechanism (tests/host/linuxkpi-time-semantics-test): the
# polling-budget cases and the completion-layout case.  This wrapper runs it and
# adds the control that proves the cases can fail: the same harness built
# against a mutated copy of linux/iopoll.h whose budget expires on the first
# round must turn the slow-operation case red.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
log_dir=$root/tools/mock/build/linuxkpi-time-semantics-test
mkdir -p "$log_dir"

bin=$root/tests/host/linuxkpi-time-semantics-test/build/linuxkpi-time-semantics-test
if [ ! -x "$bin" ]; then
    echo "MOCK-BUILD linuxkpi-time-semantics-test: $bin was not built" >&2
    exit 2
fi

failures=0

case_line() {
    printf 'CASE linuxkpi-time-semantics-test/%s %s\n' "$1" "$2"
    [ "$2" = "PASS" ] || failures=$((failures + 1))
}

# ---- the cases ----------------------------------------------------------------

if "$bin" > "$log_dir/run.log" 2>&1; then
    if grep -q '^TIMESEM: summary cases=9 failed=0$' "$log_dir/run.log"; then
        case_line budget PASS
        case_line completion-layout PASS
    else
        case_line budget FAIL
        case_line completion-layout FAIL
        echo "linuxkpi-time-semantics-test: summary unexpected" >&2
        tail -8 "$log_dir/run.log" >&2 || true
    fi
    # the wrapped wait's own container: all three legs present and green
    wait_pass=$(grep -c '^TIMESEM: wait-event-.* PASS' "$log_dir/run.log" || true)
    wait_fail=$(grep -c '^TIMESEM: wait-event-.* FAIL' "$log_dir/run.log" || true)
    if [ "$wait_pass" = 3 ] && [ "$wait_fail" = 0 ]; then
        case_line wait-event-timeout PASS
    else
        case_line wait-event-timeout FAIL
        echo "linuxkpi-time-semantics-test: the wrapped-wait legs are not all green ($wait_pass pass, $wait_fail fail)" >&2
    fi
else
    case_line budget FAIL
    case_line completion-layout FAIL
    case_line wait-event-timeout FAIL
    echo "linuxkpi-time-semantics-test: run failed" >&2
    tail -8 "$log_dir/run.log" >&2 || true
fi

# ---- the red control ----------------------------------------------------------

# The budget accounting is what keeps a slow-but-successful read from being
# reported as a timeout.  The control builds the same harness against a copy of
# the header whose budget expires immediately; the slow-operation case must then
# fail.  Without this, "the slow operation still succeeds" would be a claim no
# implementation could contradict.
mut=$log_dir/mutated-iopoll
rm -rf "$mut"
mkdir -p "$mut/linux"
# The copy lives outside the tree, so its relative include of the contract
# header is repointed at the real one (the face is what is mutated here, not
# the header it sits beside).
sed -e 's/__left_ns--;/__left_ns = -1;/' \
    -e "s|\"../../linuxkpi_core.h\"|\"$root/components/linuxkpi/linuxkpi_core.h\"|" \
    "$root/components/linuxkpi/include/linux/iopoll.h" > "$mut/linux/iopoll.h"
if grep -q '__left_ns = -1;' "$mut/linux/iopoll.h" &&
    make -C "$root/tests/host/linuxkpi-time-semantics-test" \
        IOPOLL_EXTRA_INC="-I$mut" BUILD_DIR="$mut/build" > "$log_dir/mutated-build.log" 2>&1; then
    if "$mut/build/linuxkpi-time-semantics-test" > "$log_dir/mutated-run.log" 2>&1; then
        case_line budget-red-control FAIL
        echo "linuxkpi-time-semantics-test: the expired-budget build passed (it must fail)" >&2
    else
        if grep -q '^TIMESEM: atomic-slow-op-succeeds FAIL' "$log_dir/mutated-run.log"; then
            case_line budget-red-control PASS
        else
            case_line budget-red-control FAIL
            echo "linuxkpi-time-semantics-test: the red control failed elsewhere" >&2
            tail -4 "$log_dir/mutated-run.log" >&2 || true
        fi
    fi
else
    case_line budget-red-control FAIL
    echo "linuxkpi-time-semantics-test: the mutated header did not build" >&2
    tail -5 "$log_dir/mutated-build.log" >&2 || true
fi

# ---- the wrapped-wait red control ---------------------------------------------

# The mapping under test is which result the wrapped wait reports.  The control
# builds the same harness against a wrapper header that restores the pre-fix
# form - a satisfied condition read as a timeout, and a missed wake-up
# re-arming the whole budget - so the woken leg has to go red.  The timeout leg
# that follows it would spin under that form, so the control's run is bounded
# and only the red line is read.
mut2=$log_dir/mutated-core
rm -rf "$mut2"
mkdir -p "$mut2"
cat > "$mut2/linuxkpi_core.h" <<'EOF'
/* the pre-fix wait_event_timeout, restored for the red control */
#include "REAL_CORE_HEADER"
#undef wait_event_timeout
#define	wait_event_timeout(wq, cond, timeout)				\
({									\
	int __ret = 0;							\
	int __ms = (int)jiffies_to_msecs(timeout);			\
	lkpi_waitq_lock(&(wq));						\
	while (!(cond) && __ret == 0)					\
		__ret = lkpi_waitq_timedwait(&(wq), __ms);		\
	lkpi_waitq_unlock(&(wq));					\
	__ret == 0 ? 1 : 0;						\
})
EOF
sed -i "s|REAL_CORE_HEADER|$root/components/linuxkpi/linuxkpi_core.h|" "$mut2/linuxkpi_core.h"
if grep -q '#undef wait_event_timeout' "$mut2/linuxkpi_core.h" &&
    make -C "$root/tests/host/linuxkpi-time-semantics-test" \
        CORE_EXTRA_INC="-I$mut2" BUILD_DIR="$mut2/build" > "$log_dir/mutated-core-build.log" 2>&1; then
    set +e
    timeout 60 "$mut2/build/linuxkpi-time-semantics-test" > "$log_dir/mutated-core-run.log" 2>&1
    set -e
    if grep -q '^TIMESEM: wait-event-woken FAIL' "$log_dir/mutated-core-run.log"; then
        case_line wait-event-red-control PASS
    else
        case_line wait-event-red-control FAIL
        echo "linuxkpi-time-semantics-test: the pre-fix mapping did not turn the woken leg red" >&2
        tail -4 "$log_dir/mutated-core-run.log" >&2 || true
    fi
else
    case_line wait-event-red-control FAIL
    echo "linuxkpi-time-semantics-test: the wrapped-wait control did not build" >&2
    tail -5 "$log_dir/mutated-core-build.log" >&2 || true
fi

exit "$failures"

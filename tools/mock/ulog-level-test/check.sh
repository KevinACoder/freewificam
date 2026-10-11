#!/bin/sh
# @file    check.sh
# @brief   Wrapper for the ulog-level-test suite: run the cases, check the
#          compile-time master gate, and prove the cases can go red.
#
# The harness owns the mechanism (tests/host/ulog-level-test): the engine's
# level semantics, the shell's line discipline, and the never-filtered promotion
# that keeps a judged line alive at any threshold.  This wrapper runs it, adds
# the two-build gate check (ULOG_ENABLED defined and undefined), and adds the two
# controls that prove the cases can fail: a build whose family promotion never
# fires, and a build whose repeat gate never folds.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
log_dir=$root/tools/mock/build/ulog-level-test
mkdir -p "$log_dir"

suite=$root/tests/host/ulog-level-test
bin=$suite/build/ulog-level-test
if [ ! -x "$bin" ]; then
    echo "MOCK-BUILD ulog-level-test: $bin was not built" >&2
    exit 2
fi

failures=0

case_line() {
    printf 'CASE ulog-level-test/%s %s\n' "$1" "$2"
    [ "$2" = "PASS" ] || failures=$((failures + 1))
}

# ---- the cases -----------------------------------------------------------------

if "$bin" > "$log_dir/run.log" 2>&1; then
    if grep -q '^ULOGLEVEL: summary cases=11 failed=0$' "$log_dir/run.log"; then
        for c in engine-subscriber-cap engine-threshold engine-always \
                 engine-level-name wrapper-promotion wrapper-gate \
                 outlet-line-assembly outlet-cr-lf outlet-repeat-fold \
                 outlet-emergency anchor-table; do
            case_line "$c" PASS
        done
    else
        for c in engine-subscriber-cap engine-threshold engine-always \
                 engine-level-name wrapper-promotion wrapper-gate \
                 outlet-line-assembly outlet-cr-lf outlet-repeat-fold \
                 outlet-emergency anchor-table; do
            case_line "$c" FAIL
        done
        echo "ulog-level-test: summary unexpected" >&2
        tail -8 "$log_dir/run.log" >&2 || true
    fi
else
    for c in engine-subscriber-cap engine-threshold engine-always \
             engine-level-name wrapper-promotion wrapper-gate \
             outlet-line-assembly outlet-cr-lf outlet-repeat-fold \
             outlet-emergency anchor-table; do
        case_line "$c" FAIL
    done
    echo "ulog-level-test: the suite run failed" >&2
    tail -8 "$log_dir/run.log" >&2 || true
fi

# ---- the compile-time master gate (two builds of the same source) --------------

# With ULOG_ENABLED undefined the level plane must generate no code at all, and
# the same translation unit must still compile: a build that silently kept the
# engine would make "the console is off" a claim nothing could check.
gate_inc="-I$suite/shim -I$root/os/baremetal -I$root/components/ulog/src -I$root"
if cc -c -std=c11 -Wall -Wextra $gate_inc -o "$log_dir/gate-off.o" \
        "$root/os/baremetal/log.c" > "$log_dir/gate-off.log" 2>&1 &&
   cc -c -std=c11 -Wall -Wextra -DULOG_ENABLED $gate_inc -o "$log_dir/gate-on.o" \
        "$root/os/baremetal/log.c" > "$log_dir/gate-on.log" 2>&1; then
    off=$(nm "$log_dir/gate-off.o" | grep -c 'ulog_message' || true)
    on=$(nm "$log_dir/gate-on.o" | grep -c 'ulog_message' || true)
    if [ "$off" = 0 ] && [ "$on" -gt 0 ]; then
        case_line compile-gate PASS
    else
        case_line compile-gate FAIL
        echo "ulog-level-test: gate off=$off on=$on (want off=0 on>0)" >&2
    fi
else
    case_line compile-gate FAIL
    echo "ulog-level-test: the gate build did not compile" >&2
    tail -5 "$log_dir/gate-off.log" "$log_dir/gate-on.log" >&2 || true
fi

# ---- red control 1: the family promotion never fires ---------------------------

# The promotion is what keeps a judged line alive under a strict threshold.  A
# build whose promotion loop is empty must fail the promotion case; without this,
# "a judged line survives" would be a claim no implementation could contradict.
mut=$log_dir/mutated-nofamily
rm -rf "$mut"
mkdir -p "$mut"
cp "$root/os/baremetal/log.c" "$root/os/baremetal/log.h" \
   "$root/os/baremetal/log_anchors.h" "$root/os/baremetal/minilibc.h" \
   "$root/os/baremetal/tick.h" "$mut/"
sed 's/i < log_anchor_family_count/i < 0u/' "$mut/log.c" > "$mut/log.mut.c"
if grep -q 'i < 0u;' "$mut/log.mut.c" &&
    make -C "$suite" LOG_SRC="$mut/log.mut.c" BUILD_DIR="$mut/build" \
        > "$log_dir/mutated-nofamily-build.log" 2>&1; then
    if "$mut/build/ulog-level-test" > "$log_dir/mutated-nofamily-run.log" 2>&1; then
        case_line promotion-red-control FAIL
        echo "ulog-level-test: the no-promotion build passed (it must fail)" >&2
    else
        if grep -q '^ULOGLEVEL: wrapper-promotion FAIL' \
                "$log_dir/mutated-nofamily-run.log"; then
            case_line promotion-red-control PASS
        else
            case_line promotion-red-control FAIL
            echo "ulog-level-test: the red control failed elsewhere" >&2
            grep '^ULOGLEVEL' "$log_dir/mutated-nofamily-run.log" | tail -6 >&2 || true
        fi
    fi
else
    case_line promotion-red-control FAIL
    echo "ulog-level-test: the mutated shell did not build" >&2
    tail -5 "$log_dir/mutated-nofamily-build.log" >&2 || true
fi

# ---- red control 2: the repeat gate never folds --------------------------------

mut=$log_dir/mutated-nofold
rm -rf "$mut"
mkdir -p "$mut"
cp "$root/os/baremetal/log.c" "$root/os/baremetal/log.h" \
   "$root/os/baremetal/log_anchors.h" "$root/os/baremetal/minilibc.h" \
   "$root/os/baremetal/tick.h" "$mut/"
sed 's/repeat_n != 0u/repeat_n != 0u \&\& 0/' "$mut/log.c" > "$mut/log.mut.c"
if grep -q 'repeat_n != 0u && 0' "$mut/log.mut.c" &&
    make -C "$suite" LOG_SRC="$mut/log.mut.c" BUILD_DIR="$mut/build" \
        > "$log_dir/mutated-nofold-build.log" 2>&1; then
    if "$mut/build/ulog-level-test" > "$log_dir/mutated-nofold-run.log" 2>&1; then
        case_line repeat-red-control FAIL
        echo "ulog-level-test: the no-fold build passed (it must fail)" >&2
    else
        if grep -q '^ULOGLEVEL: outlet-repeat-fold FAIL' \
                "$log_dir/mutated-nofold-run.log"; then
            case_line repeat-red-control PASS
        else
            case_line repeat-red-control FAIL
            echo "ulog-level-test: the red control failed elsewhere" >&2
            grep '^ULOGLEVEL' "$log_dir/mutated-nofold-run.log" | tail -6 >&2 || true
        fi
    fi
else
    case_line repeat-red-control FAIL
    echo "ulog-level-test: the mutated shell did not build" >&2
    tail -5 "$log_dir/mutated-nofold-build.log" >&2 || true
fi

exit "$failures"

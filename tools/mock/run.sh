#!/bin/sh
# @file    run.sh
# @brief   Gate1 mock container: build and run every declared suite, judge by declaration.
# @author  zhugengyu
# @date    07.10.2026
# @design adapter - ref net80211:tests/run-host-tests.sh:1
# @design registry - ref linux:drivers/base/dd.c:1
#
# Usage:
#   tools/mock/run.sh                 run every suite in tools/mock/suites.mk
#   tools/mock/run.sh <suite>...      run the named suites only
#   tools/mock/run.sh --list          list the suites
#
# Exit codes: 0 = every suite behaved as declared; 1 = a case or an expectation failed;
# 2 = configuration, build or environment error (a suite that cannot even be built is
# not a test failure, and it is not a silent pass either).
#
# Each suite is declared in its own suite.mk (fields: see the container README). Its raw
# output is kept under tools/mock/build/<suite>/, and the CASE lines it produced are
# echoed to this script's stdout, so a single aggregated log carries one line per case.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
build_root=$here/build

. "$here/lib.sh"

# The registry and the suite directories can be pointed elsewhere, which is how the
# container's self test drives the very same code over deliberately broken suites.
suites_file=${MOCK_SUITES_MK:-$here/suites.mk}
suite_root=${MOCK_SUITE_ROOT:-$here}
status=0            # 0 ok, 1 case failure, 2 configuration/build failure
suite_count=0
case_total=0
case_ok=0
case_bad=0

config_error() {
    printf 'MOCK-CONFIG %s\n' "$1"
    status=2
}

# --- segment map (C12): schema, then the generated header's determinism -------------
check_segment_map() {
    if [ ! -f "$here/segment-map.tsv" ]; then
        config_error "segment-map.tsv missing"
        return
    fi
    if ! "$here/check-segment-map.sh" > "$build_root/segment-map-check.log" 2>&1; then
        printf 'MOCK-CONFIG segment-map.tsv failed its schema check\n'
        sed 's/^/  /' "$build_root/segment-map-check.log"
        status=2
        return
    fi

    # Regenerating must reproduce the header byte for byte: a generator that drifts
    # between runs would make the two-sided anchor contract unreliable. The check
    # generates twice from the registry that is in place now, so a build artifact left
    # over from an earlier registry state is not mistaken for generator drift.
    prev=$build_root/segment_map.prev
    "$here/gen-segment-map.sh" > /dev/null
    cp "$build_root/segment_map.h" "$prev"
    "$here/gen-segment-map.sh" > /dev/null
    if ! diff -q "$prev" "$build_root/segment_map.h" > /dev/null; then
        config_error "segment_map.h is not reproducible (generator drift)"
    fi
    rm -f "$prev"
}

# --- vector factory (C13): the shared vector/trace files the suites load --------------
# The registry's vector columns, the .dat files and the manifest are one contract with
# three sides; check-vectors.sh is the single implementation that guards it, and it runs
# before any suite so a broken factory stops the run as a configuration error.
check_vectors() {
    if [ ! -f "$here/check-vectors.sh" ]; then
        config_error "check-vectors.sh missing"
        return
    fi
    if ! sh "$here/check-vectors.sh" > "$build_root/vector-check.log" 2>&1; then
        # The findings name the rule, the file and the field; the container states the
        # first one and leaves the log for the detail.
        finding=$(grep -v '^VECTORS-RESULT ' "$build_root/vector-check.log" | head -1 || true)
        if [ -z "$finding" ]; then
            finding="check-vectors.sh exited non-zero without a finding"
        fi
        printf 'MOCK-CONFIG vectors: %s\n' "$finding"
        sed 's/^/  /' "$build_root/vector-check.log"
        status=2
    fi
}

# --- one suite ---------------------------------------------------------------------
# Declared fields are validated first: a suite without a truth source, or a wireless
# suite without its reference route, may not run at all.
run_suite() {
    suite=$1
    dir=$suite_root/$suite
    decl=$dir/suite.mk

    suite_count=$((suite_count + 1))

    if [ ! -f "$decl" ]; then
        config_error "$suite: no suite.mk"
        return
    fi

    name=$(mk_get "$decl" SUITE_NAME)
    truth=$(mk_get "$decl" SUITE_TRUTH_SOURCE)
    timeout_s=$(mk_get "$decl" SUITE_TIMEOUT)
    expect=$(mk_get "$decl" SUITE_EXPECT)
    run_cmd=$(mk_get "$decl" SUITE_RUN)
    build_cmd=$(mk_get "$decl" SUITE_BUILD)
    segments=$(mk_get "$decl" SUITE_SEGMENTS)
    wireless=$(mk_get "$decl" SUITE_WIRELESS)
    route=$(mk_get "$decl" SUITE_ROUTE)

    [ "$name" = "$suite" ]      || { config_error "$suite: SUITE_NAME '$name' does not match the directory"; return; }
    [ -n "$truth" ]             || { config_error "$suite: SUITE_TRUTH_SOURCE missing"; return; }
    [ -n "$segments" ]          || { config_error "$suite: SUITE_SEGMENTS missing (use - when no anchor is covered)"; return; }
    [ -n "$expect" ]            || { config_error "$suite: SUITE_EXPECT missing"; return; }
    [ -n "$run_cmd" ]           || { config_error "$suite: SUITE_RUN missing"; return; }
    case "$timeout_s" in
        ''|*[!0-9]*) config_error "$suite: SUITE_TIMEOUT missing or not a number"; return ;;
    esac
    if [ "$wireless" = "1" ] && [ -z "$route" ]; then
        config_error "$suite: wireless suite without an ieee80211-route annotation"
        return
    fi

    printf '\nMOCK-SUITE %s timeout=%ss truth-source=%s\n' "$name" "$timeout_s" "$truth"

    log_dir=$build_root/$name
    mkdir -p "$log_dir"
    started=$(date +%s)

    if [ -n "$build_cmd" ]; then
        if ! ( cd "$root" && eval "$build_cmd" ) > "$log_dir/build.log" 2>&1; then
            printf 'MOCK-BUILD %s FAIL (log: %s)\n' "$name" "$log_dir/build.log"
            tail -20 "$log_dir/build.log" | sed 's/^/  /'
            status=2
            return
        fi
    fi

    set +e
    ( cd "$root" && timeout "$timeout_s" sh -c "$run_cmd" ) > "$log_dir/run.log" 2>&1
    code=$?
    set -e

    if [ "$code" = 124 ] || [ "$code" = 143 ]; then
        printf 'CASE %s/main TIMEOUT (limit %ss)\n' "$name" "$timeout_s"
        case_bad=$((case_bad + 1))
        if [ "$status" = 0 ]; then status=1; fi
        return
    fi

    report_time() {
        printf 'MOCK-TIME %s build+run=%ss\n' "$name" "$(( $(date +%s) - started ))"
    }

    if [ "$expect" = "self" ]; then
        # The suite reports its own cases; the container checks the report and that the
        # exit code agrees with it - a suite that prints FAIL but exits 0 is broken.
        # -a: an upstream test binary may emit bytes that are not valid text; the
        # case lines are still ASCII and must be read (a binary-looking log once
        # produced a phantom "no CASE line produced" failure on a cold run).
        cases=$(grep -a '^CASE ' "$log_dir/run.log" || true)
        if [ -z "$cases" ]; then
            printf 'CASE %s/suite FAIL (no CASE line produced)\n' "$name"
            case_bad=$((case_bad + 1))
            if [ "$status" = 0 ]; then status=1; fi
            return
        fi
        printf '%s\n' "$cases" | grep -aEv "^CASE $name/" > "$log_dir/foreign-cases.txt" || true
        if [ -s "$log_dir/foreign-cases.txt" ]; then
            printf 'CASE %s/suite FAIL (CASE lines from another suite)\n' "$name"
            case_bad=$((case_bad + 1))
            if [ "$status" = 0 ]; then status=1; fi
        fi
        printf '%s\n' "$cases"
        n=$(printf '%s\n' "$cases" | wc -l)
        ok=$(printf '%s\n' "$cases" | grep -acE ' (PASS|XFAIL)$' || true)
        bad=$(printf '%s\n' "$cases" | grep -acvE ' (PASS|XFAIL)$' || true)
        case_total=$((case_total + n))
        case_ok=$((case_ok + ok))
        case_bad=$((case_bad + bad))
        report_time
        if [ "$bad" != "0" ]; then
            if [ "$status" = 0 ]; then status=1; fi
        elif [ "$code" != "0" ]; then
            printf 'CASE %s/suite FAIL (all cases as declared but the suite exited %s)\n' "$name" "$code"
            case_bad=$((case_bad + 1))
            case_total=$((case_total + 1))
            if [ "$status" = 0 ]; then status=1; fi
        fi
        return
    fi

    verdict=$(mock_classify "$expect" "$code" "$log_dir/run.log" | tr -d '\n')
    printf 'CASE %s/main %s (expect=%s exit=%s)\n' "$name" "$verdict" "$expect" "$code"
    report_time
    case_total=$((case_total + 1))
    if mock_is_ok "$verdict"; then
        case_ok=$((case_ok + 1))
    else
        case_bad=$((case_bad + 1))
        if [ "$status" = 0 ]; then status=1; fi
    fi
}

# --- entry -------------------------------------------------------------------------
mkdir -p "$build_root"

if [ "${1:-}" = "--list" ] || [ "${1:-}" = "-l" ]; then
    sed -n 's/^[[:space:]]*SUITES[[:space:]]*:=[[:space:]]*//p' "$suites_file" | tr ' ' '\n' | grep -v '^$'
    exit 0
fi

if [ ! -f "$suites_file" ]; then
    config_error "suites.mk missing"
    exit 2
fi

check_segment_map
check_vectors

if [ $# -gt 0 ]; then
    wanted=$*
else
    wanted=$(sed -n 's/^[[:space:]]*SUITES[[:space:]]*:=[[:space:]]*//p' "$suites_file" | tr ' ' '\n' | grep -v '^$')
fi

wireless_declared=0

for s in $wanted; do
    run_suite "$s"
    decl=$suite_root/$s/suite.mk
    if [ -f "$decl" ] && [ "$(mk_get "$decl" SUITE_WIRELESS)" = "1" ]; then
        wireless_declared=1
    fi
done

# An empty set is stated, never assumed: with no wireless suite registered yet, the
# container says so instead of leaving the question open.
if [ "$wireless_declared" = 0 ]; then
    printf 'MOCK-NOTE no wireless suites registered yet\n'
fi

printf '\nMOCK-SUMMARY suites=%s cases=%s ok=%s failed=%s\n' \
       "$suite_count" "$case_total" "$case_ok" "$case_bad"

if [ "$status" = 0 ]; then
    printf 'MOCK-RESULT pass\n'
else
    printf 'MOCK-RESULT fail\n'
fi

exit "$status"

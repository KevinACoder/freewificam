#!/bin/sh
# @file    run.sh
# @brief   Prove the container can go red: expected-red forms are classified as declared,
#          and deliberately broken suites make the container itself fail.
# @author  zhugengyu
# @date    07.10.2026
# @design template method - ref net80211:tests/run-host-tests.sh:1
#
# Half one exercises the classification rules directly (the rules live in the container's
# own library, so this is the same code path the runner uses). Half two drives the whole
# container over synthetic broken suites and asserts that it reports each break the way it
# is supposed to - a container that silently passes its own negative controls is the one
# failure mode a self test must not have.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
mock=$(cd "$here/.." && pwd)
fixtures=$mock/build/container-selftest/fixtures
suite=container-selftest
failures=0

. "$mock/lib.sh"

result() {
    if [ "$2" = "1" ]; then
        printf 'CASE %s/%s PASS\n' "$suite" "$1"
    else
        printf 'CASE %s/%s FAIL\n' "$suite" "$1"
        failures=$((failures + 1))
    fi
}

log=$(mktemp)
trap 'rm -f "$log"' EXIT

# classify NAME EXPECT EXPECTED_VERDICT - run the fixture for EXPECT and compare.
classify() {
    name=$1
    expect=$2
    want=$3
    shift 3

    set +e
    "$@" > "$log" 2>&1
    code=$?
    set -e

    got=$(mock_classify "$expect" "$code" "$log" | tr -d '\n')
    if [ "$got" = "$want" ]; then
        result "$name" 1
    else
        result "$name" 0
        echo "  $name: expect=$expect exit=$code classified $got, wanted $want" >&2
    fi
}

# --- half one: the four expectation forms, plus a signature that must not match -------
classify plain_pass           'exit:0'                  PASS  "$fixtures/pass"
classify declared_red         'exit:1 match:reason=4'   XFAIL "$fixtures/fail"
classify designed_crash       'signal:11'               XFAIL "$fixtures/crash"
classify compilefail_source   'compilefail'             XFAIL \
         cc -c "$here/fixtures/compilefail.c" -o /dev/null
classify signature_mismatch   'exit:1 match:no-such-signature' FAIL "$fixtures/fail"

# --- half two: the container must fail on broken suites ------------------------------
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"; rm -f "$log"' EXIT

write_suite() {
    name=$1
    expect=$2
    run=$3
    truth=$4
    mkdir -p "$tmp/$name"
    {
        echo "SUITE_NAME := $name"
        echo "SUITE_TIMEOUT := 5"
        echo "SUITE_EXPECT := $expect"
        echo "SUITE_RUN := $run"
        echo "SUITE_SEGMENTS := -"
        echo "SUITE_WIRELESS := 0"
        if [ -n "$truth" ]; then echo "SUITE_TRUTH_SOURCE := $truth"; fi
    } > "$tmp/$name/suite.mk"
}

write_suite selftest-fail         'exit:0'  "$fixtures/fail"          "negative control"
write_suite selftest-xpass        'exit:1'  "$fixtures/pass"          "negative control"
write_suite selftest-timeout      'exit:0'  "sleep 30"                "negative control"
write_suite selftest-compilepass  'compilefail' "cc -c $here/fixtures/pass.c -o /dev/null" "negative control"

{
    echo "SUITES := selftest-fail selftest-xpass selftest-timeout selftest-compilepass"
} > "$tmp/suites.mk"

set +e
MOCK_SUITES_MK=$tmp/suites.mk MOCK_SUITE_ROOT=$tmp sh "$mock/run.sh" > "$tmp/out.log" 2>&1
rc=$?
set -e

assert_break() {
    name=$1
    log=$2
    pattern=$3
    if grep -qE "$pattern" "$log"; then
        result "$name" 1
    else
        result "$name" 0
        echo "  $name: expected /$pattern/ in the container report" >&2
        sed 's/^/    /' "$log" >&2
    fi
}

assert_break negative_control_fail        "$tmp/out.log" '^CASE selftest-fail/main FAIL'
assert_break negative_control_xpass       "$tmp/out.log" '^CASE selftest-xpass/main XPASS'
assert_break negative_control_timeout     "$tmp/out.log" '^CASE selftest-timeout/main TIMEOUT'
assert_break negative_control_compilepass "$tmp/out.log" '^CASE selftest-compilepass/main XPASS'
assert_break negative_control_exit_code   "$tmp/out.log" '^MOCK-RESULT fail'
result negative_control_exit_status "$([ "$rc" = 1 ] && echo 1 || echo 0)"

# A suite that does not declare where its expectations come from may not run at all: the
# container must refuse it as a configuration error, not test it.
write_suite selftest-nodecl 'exit:0' "$fixtures/pass" ""
{
    echo "SUITES := selftest-nodecl"
} > "$tmp/suites-nodecl.mk"

set +e
MOCK_SUITES_MK=$tmp/suites-nodecl.mk MOCK_SUITE_ROOT=$tmp sh "$mock/run.sh" > "$tmp/out-nodecl.log" 2>&1
rc=$?
set -e

assert_break negative_control_missing_truth "$tmp/out-nodecl.log" 'MOCK-CONFIG selftest-nodecl: SUITE_TRUTH_SOURCE missing'
result negative_control_missing_truth_status "$([ "$rc" = 2 ] && echo 1 || echo 0)"

echo "$suite: failures=$failures"
[ "$failures" = 0 ]

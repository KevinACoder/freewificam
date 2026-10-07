#!/bin/sh
# @file    check.sh
# @brief   Run the 64-bit handle cases and report one CASE line each, plus the
#          precondition anchor; --expect-red runs the red control instead.
# @author  zhugengyu
# @date    07.10.2026
# @design adapter - ref net80211:tests/run-host-tests.sh:1
#
# Normal mode judges the C9 expectation: every case must exit 0 with its "ok" anchor, and
# no case may report an unmet precondition.
#
# --expect-red is the suite's red control, run against the pinned adapter bytes
# (make -C tests/host/freertos-cmsis2-smoke PATCHES=off).  It asserts the exact signature
# the truncating build shows, so "this suite can go red" is reproducible and not a story:
#   mutex   exit 2   the handle is already truncated at creation, so the precondition
#                    check refuses to pretend the case ran
#   timer   exit 139 the handle is intact, but the callback dispatch dereferences the
#                    truncated callb pointer: signal 11
#   mempool exit 0   its corrupted value only reached low bits of a mask, so the pool
#                    works even when truncated - the case documents that boundary

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
suite=freertos-host-smoke
logs=$root/tools/mock/build/$suite

# The normal run judges the patched build; the red control judges its own build, which the
# Makefile places in build-pinned/ so the two modes can never share objects or binaries.
if [ "${1:-}" = "--expect-red" ]; then
    bin=$root/tests/host/freertos-cmsis2-smoke/build/pinned/c9-handle-trunc
else
    bin=$root/tests/host/freertos-cmsis2-smoke/build/c9-handle-trunc
fi

mkdir -p "$logs"
failures=0

if [ ! -x "$bin" ]; then
    printf 'CASE %s/suite FAIL (no binary; build %s first)\n' "$suite" "$bin"
    exit 1
fi

# run_case NAME -> sets rc
run_case() {
    set +e
    timeout 60 "$bin" "$1" > "$logs/$1.log" 2>&1
    rc=$?
    set -e
}

case_report() {
    name=$1
    code=$2
    if [ "$code" = 0 ] && grep -q "^c9: $name ok$" "$logs/$name.log"; then
        printf 'CASE %s/c9-%s PASS\n' "$suite" "$name"
    else
        printf 'CASE %s/c9-%s FAIL (exit %s)\n' "$suite" "$name" "$code"
        tail -8 "$logs/$name.log" | sed 's/^/  /'
        failures=$((failures + 1))
    fi
}

if [ "${1:-}" = "--expect-red" ]; then
    run_case mutex;   mrc=$rc
    run_case timer;   trc=$rc
    run_case mempool; prc=$rc

    verdict=PASS
    note=
    [ "$mrc" = 2 ]   || { verdict=FAIL; note="$note mutex(want exit 2)"; }
    [ "$trc" = 139 ] || { verdict=FAIL; note="$note timer(want signal 11)"; }
    [ "$prc" = 0 ]   || { verdict=FAIL; note="$note mempool(want exit 0)"; }

    printf 'c9-red: mutex=%s timer=%s mempool=%s\n' "$mrc" "$trc" "$prc"
    printf 'CASE %s/c9-red-signature %s%s\n' "$suite" "$verdict" "$note"

    [ "$verdict" = PASS ]
    exit $?
fi

run_case mutex;   mrc=$rc
run_case timer;   trc=$rc
run_case mempool; prc=$rc

# The precondition anchor is the anti-false-green gate: a run in which a handle was not
# above 4 GiB proved nothing about 64-bit handles, whatever the cases reported.
if [ "$mrc" = 2 ] || [ "$trc" = 2 ] || [ "$prc" = 2 ]; then
    printf 'CASE %s/c9-precondition-64bit FAIL (a handle was not above 4 GiB)\n' "$suite"
    failures=$((failures + 1))
else
    printf 'CASE %s/c9-precondition-64bit PASS\n' "$suite"
fi

case_report mutex   "$mrc"
case_report timer   "$trc"
case_report mempool "$prc"

echo "$suite: failures=$failures" >&2
[ "$failures" = 0 ]

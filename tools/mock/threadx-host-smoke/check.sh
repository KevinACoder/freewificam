#!/bin/sh
# @file    check.sh
# @brief   Wrap the ThreadX host smoke test into container cases.
# @author  zhugengyu
# @date    07.10.2026
# @design adapter - ref net80211:tests/run-host-tests.sh:1
#
# The smoke test itself is not duplicated here: this wrapper builds it through its own
# Makefile and turns the anchors it prints into one CASE line each, so the container and
# its aggregated log speak the same language as every other suite.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
smoke=$root/tests/host/threadx-cmsis2-smoke
binary=$smoke/build/threadx-cmsis2-smoke
suite=threadx-host-smoke

if [ ! -x "$binary" ]; then
    echo "MOCK-BUILD $suite: $binary was not built" >&2
    exit 2
fi

log=$(mktemp)
trap 'rm -f "$log"' EXIT

set +e
timeout 60 "$binary" > "$log" 2>&1
code=$?
set -e

case_line() {
    if [ "$2" = "1" ]; then
        printf 'CASE %s/%s PASS\n' "$suite" "$1"
    else
        printf 'CASE %s/%s FAIL\n' "$suite" "$1"
        failures=$((failures + 1))
    fi
}

failures=0

case_line exit_code "$([ "$code" = 0 ] && echo 1 || echo 0)"

check_anchor() {
    if grep -qF "os2-smoke: $2" "$log"; then
        case_line "$1" 1
    else
        case_line "$1" 0
    fi
}

check_anchor kernel_running "kernel running"
check_anchor got_1 "got 1"
check_anchor got_2 "got 2"
check_anchor got_3 "got 3"
check_anchor pass_anchor "PASS"

# The three messages must appear in order: a consumer that reports them out of order is
# not exercising the queue the way the test claims.
order_ok=0
order=$(grep -n -F -e "os2-smoke: got 1" -e "os2-smoke: got 2" -e "os2-smoke: got 3" "$log" \
        | cut -d: -f1)
if [ "$(printf '%s\n' "$order" | grep -c . || true)" = 3 ] \
   && printf '%s\n' "$order" | awk 'NR > 1 && $1 <= prev { bad = 1 } { prev = $1 } END { exit bad }'; then
    order_ok=1
fi
case_line got_order "$order_ok"

ticks=$(sed -n 's/.*tick advanced n=\([0-9][0-9]*\).*/\1/p' "$log" | tail -1)
case_line tick_advanced "$([ -n "$ticks" ] && [ "$ticks" -ge 5 ] && echo 1 || echo 0)"

echo "$suite: exit=$code ticks=${ticks:-none} failures=$failures"
[ "$failures" = 0 ]

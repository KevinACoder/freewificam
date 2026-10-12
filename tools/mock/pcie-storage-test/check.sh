#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# @file    check.sh
# @brief   Wrap the storage stack's battery into container cases.
#
# The suite binary is not duplicated here: this wrapper runs the binary its own
# Makefile built and turns its anchor lines into one CASE line each, so the
# container and its aggregated log speak the same language as every other suite.
#
# Judged: the green run (one case per anchor line, plus the run's exit and its
# final verdict), and four red controls - the expectation of each fault case
# deliberately inverted (F1, F2, F3 must each go red), and a scripted device that
# acknowledges commands but returns inverted bytes (the round trip must go red).
# Without them a green battery would only prove it can print ok=1.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
suite=pcie-storage-test
binary=$root/tests/host/pcie-storage-test/build/pcie-storage-test
log_dir=$root/tools/mock/build/$suite

if [ ! -x "$binary" ]; then
    echo "MOCK-BUILD $suite: $binary was not built" >&2
    exit 2
fi

mkdir -p "$log_dir"
failures=0

case_line() {
    printf 'CASE %s/%s %s\n' "$suite" "$1" "$2"
    [ "$2" = "PASS" ] || failures=$((failures + 1))
}

# The anchor grammar is "storage-test: <id> ok=<0|1> <detail>"; the id is the
# case name and the flag is its verdict. An anchor with an unreadable flag is a
# malformed report and a failure of its own.
scan_anchors() {
    log=$1
    n=0
    bad=0
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        rest=${line#storage-test: }
        id=${rest%% *}
        flags=${rest#"$id" }
        verdict=${flags%% *}
        case "$verdict" in
        ok=1) ok=PASS ;;
        ok=0) ok=FAIL ;;
        *) ok=FAIL; bad=$((bad + 1)) ;;
        esac
        n=$((n + 1))
        case_line "$id" "$ok"
    done <<EOF
$(grep -E '^storage-test: [a-z][a-z0-9-]* ok=' "$log" || true)
EOF
    printf '%s %s\n' "$n" "$bad" > "$log_dir/anchors.count"
}

# ---------------------------------------------------------------------------
# the green run: one case per anchor line
#
# The battery's raw output goes to its own file (battery.log), not run.log: the
# container captures this wrapper's stdout into run.log, so reusing that name
# would have the two writers race for the same file.
# ---------------------------------------------------------------------------
log=$log_dir/battery.log

set +e
timeout 120 "$binary" > "$log" 2>&1
code=$?
set -e

case_line run_exit "$([ "$code" = 0 ] && echo PASS || echo FAIL)"

scan_anchors "$log"
read -r n bad < "$log_dir/anchors.count"

case_line anchors_seen "$([ "$n" -ge 10 ] && echo PASS || echo FAIL)"
case_line anchors_wellformed "$([ "$bad" = 0 ] && echo PASS || echo FAIL)"

# The battery's own verdict must be the run's last word, and nothing may be
# skipped.
case_line pass_anchor \
    "$([ "$(grep '^storage-test: ' "$log" | tail -1)" = "storage-test: PASS" ] && echo PASS || echo FAIL)"
case_line no_skip "$([ "$(grep -ci skip "$log" || true)" = 0 ] && echo PASS || echo FAIL)"

# ---------------------------------------------------------------------------
# red controls: each fault case's expectation inverted must fail the run
# ---------------------------------------------------------------------------
red_case() {
    id=$1
    rlog=$log_dir/red-$id.log
    set +e
    timeout 120 "$binary" --tamper "$id" > "$rlog" 2>&1
    rcode=$?
    set -e
    if [ "$rcode" != 0 ] && grep -q "^storage-test: $id ok=0" "$rlog"; then
        case_line "red-$id" PASS
    else
        case_line "red-$id" FAIL
        echo "  $suite: tampered $id did not go red (exit=$rcode)" >&2
        grep -n "$id" "$rlog" | sed 's/^/    /' >&2 || true
    fi
}

red_case f1-completion-timeout
red_case f2-late-irq
red_case f3-doorbell-drop

# ---------------------------------------------------------------------------
# red control: a scripted device that lies about its reads
# ---------------------------------------------------------------------------
lie_log=$log_dir/red-lying.log
set +e
timeout 120 "$binary" --fault lying-device > "$lie_log" 2>&1
lie_code=$?
set -e

if [ "$lie_code" != 0 ] && grep -q '^storage-test: exfat-roundtrip ok=0' "$lie_log"; then
    case_line red-lying-device PASS
else
    case_line red-lying-device FAIL
    echo "  $suite: the lying device did not turn the round trip red (exit=$lie_code)" >&2
    grep -n 'exfat-roundtrip\|storage-test: FAIL' "$lie_log" | sed 's/^/    /' >&2 || true
fi

echo "$suite: exit=$code cases=$n failures=$failures"
[ "$failures" = 0 ]

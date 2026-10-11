#!/bin/sh
# @file    check.sh
# @brief   Wrap the SDIO engine's battery into container cases.
#
# The suite binary is not duplicated here: this wrapper runs the binary its own
# Makefile built and turns its anchor lines into one CASE line each, so the
# container and its aggregated log speak the same language as every other suite.
#
# Three runs are judged: the green run (one case per anchor line, plus the run's
# exit and its final verdict) and two red controls - a deliberately wrong
# expectation, and a card that answers inverted bytes. Without them a green
# battery would only prove it can print ok=1.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
suite=linuxkpi-sdio-test
binary=$root/tests/host/linuxkpi-sdio-test/build/linuxkpi-sdio-test
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

# The anchor grammar is "sdio-test: <id> ok=<0|1> <detail>"; the id is the case
# name and the flag is its verdict. An informational line carries no " ok=" and
# is not an anchor; an anchor with an unreadable flag is a malformed report and
# is a failure of its own. The CASE lines go to stdout; the counts go to a file,
# because the caller must not have to separate them from the case output.
scan_anchors() {
    log=$1
    n=0
    bad=0
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        rest=${line#sdio-test: }
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
$(grep -E '^sdio-test: [a-z][a-z0-9-]* ok=' "$log" || true)
EOF
    printf '%s %s\n' "$n" "$bad" > "$log_dir/anchors.count"
}

# ---------------------------------------------------------------------------
# the green run: one case per anchor line
# ---------------------------------------------------------------------------
log=$log_dir/run.log

set +e
timeout 60 "$binary" > "$log" 2>&1
code=$?
set -e

case_line run_exit "$([ "$code" = 0 ] && echo PASS || echo FAIL)"

scan_anchors "$log"
read -r n bad < "$log_dir/anchors.count"

case_line anchors_seen "$([ "$n" -ge 15 ] && echo PASS || echo FAIL)"
case_line anchors_wellformed "$([ "$bad" = 0 ] && echo PASS || echo FAIL)"

# The battery's own verdict must be the run's last word, and nothing may be
# skipped.
case_line pass_anchor \
    "$([ "$(grep '^sdio-test: ' "$log" | tail -1)" = "sdio-test: PASS" ] && echo PASS || echo FAIL)"
case_line no_skip "$([ "$(grep -ci skip "$log" || true)" = 0 ] && echo PASS || echo FAIL)"

# ---------------------------------------------------------------------------
# red control 1: the same battery with one expectation deliberately wrong
# ---------------------------------------------------------------------------
red_log=$log_dir/red-expect.log
set +e
timeout 60 "$binary" --fault expect-tamper > "$red_log" 2>&1
red_code=$?
set -e

if [ "$red_code" != 0 ] && grep -q '^sdio-test: word-access ok=0' "$red_log"; then
    case_line red-control-expect PASS
else
    case_line red-control-expect FAIL
    echo "  $suite: the tampered expectation did not go red (exit=$red_code)" >&2
    grep -n 'word-access' "$red_log" | sed 's/^/    /' >&2 || true
fi

# ---------------------------------------------------------------------------
# red control 2: a card that answers inverted bytes
# ---------------------------------------------------------------------------
lie_log=$log_dir/red-lying.log
set +e
timeout 60 "$binary" --fault lying-card > "$lie_log" 2>&1
lie_code=$?
set -e

if [ "$lie_code" != 0 ] && grep -q '^sdio-test: readb-writeb ok=0' "$lie_log"; then
    case_line red-control-lying PASS
else
    case_line red-control-lying FAIL
    echo "  $suite: the lying card did not turn the readback red (exit=$lie_code)" >&2
    grep -n 'readb-writeb' "$lie_log" | sed 's/^/    /' >&2 || true
fi

echo "$suite: exit=$code cases=$n failures=$failures"
[ "$failures" = 0 ]

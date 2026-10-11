#!/bin/sh
# @file    check.sh
# @brief   Wrap the SDIO adapter's battery into container cases.
#
# The suite binary is not duplicated here: this wrapper runs the binary its own
# Makefile built and turns its anchor lines into one CASE line each, so the
# container and its aggregated log speak the same language as every other suite.
#
# Two groups are judged: the green run (one case per anchor line, plus the run's
# exit and its final verdict) and the red control - the same battery with one
# expectation deliberately wrong, which must fail. Without that second run a
# green battery would only prove it can print ok=1.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
suite=fsl-sdmmc-adapter-test
binary=$root/tests/host/fsl-sdmmc-adapter-test/build/fsl-sdmmc-adapter-test
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

log=$log_dir/run.log

# ---------------------------------------------------------------------------
# the green run: one case per anchor line
# ---------------------------------------------------------------------------
set +e
timeout 60 "$binary" > "$log" 2>&1
code=$?
set -e

case_line run_exit "$([ "$code" = 0 ] && echo PASS || echo FAIL)"

# The anchor grammar is "adapter-test: <id> ok=<0|1> <detail>"; the id is the
# case name and the flag is its verdict. A line without a flag is a malformed
# report and is a failure of its own.
n=0
bad=0
while IFS= read -r line; do
    rest=${line#adapter-test: }
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
$(grep '^adapter-test: [a-z]' "$log" | grep -v '^adapter-test: PASS$' | grep -v '^adapter-test: FAIL ')
EOF

case_line anchors_seen "$([ "$n" -ge 17 ] && echo PASS || echo FAIL)"
case_line anchors_wellformed "$([ "$bad" = 0 ] && echo PASS || echo FAIL)"

# The battery's own verdict must be the run's last word, and nothing may be
# skipped.
case_line pass_anchor \
    "$([ "$(grep '^adapter-test: ' "$log" | tail -1)" = "adapter-test: PASS" ] && echo PASS || echo FAIL)"
case_line no_skip "$([ "$(grep -ci skip "$log" || true)" = 0 ] && echo PASS || echo FAIL)"

# ---------------------------------------------------------------------------
# the red control: the same battery, one expectation deliberately wrong
# ---------------------------------------------------------------------------
red_log=$log_dir/red-control.log
set +e
timeout 60 "$binary" --fault expect-tamper > "$red_log" 2>&1
red_code=$?
set -e

if [ "$red_code" != 0 ] && grep -q '^adapter-test: extended-write-two-bytes ok=0' "$red_log"; then
    case_line red-control PASS
else
    case_line red-control FAIL
    echo "  $suite: the tampered expectation did not go red (exit=$red_code)" >&2
    grep -n 'extended-write-two-bytes\|adapter-test: FAIL' "$red_log" | sed 's/^/    /' >&2 || true
fi

echo "$suite: exit=$code cases=$n failures=$failures"
[ "$failures" = 0 ]

#!/bin/sh
# @file    run.sh
# @brief   Gate1 wrapper for every board suite's judge self test.
# @author  zhugengyu
# @date    15.10.2026
# @design registry - ref tests/board/README.md:97-99 (a judge's --selftest is
#          the contract) and tools/mock/run.sh:150-190 (the CASE grammar)
#
# Each judge already owns the cases: a legal transcript it must pass and one
# mutation per piece of evidence it must fail. This wrapper runs them, one CASE
# line per judge, and prints a failing judge's own report so the container's log
# says which mutation stopped landing on its declared exit code.

set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
cd "$root"

suite=board-judge-selftest
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

bad=0
judges=0
for judge in tests/board/*/judge.py; do
    name=$(basename "$(dirname "$judge")")
    judges=$((judges + 1))
    if python3 "$judge" --selftest > "$tmp/$name.log" 2>&1; then
        ok=$(grep -c ' ok$' "$tmp/$name.log" || true)
        echo "CASE $suite/$name mutations=$ok PASS"
    else
        echo "CASE $suite/$name FAIL"
        sed 's/^/  /' "$tmp/$name.log"
        bad=$((bad + 1))
    fi
done

if [ "$judges" = 0 ]; then
    echo "CASE $suite/suite FAIL (no judge found under tests/board)"
    exit 1
fi

if [ "$bad" = 0 ]; then
    echo "CASE $suite/all judges=$judges PASS"
    exit 0
fi
echo "CASE $suite/all judges=$judges failing=$bad FAIL"
exit 1

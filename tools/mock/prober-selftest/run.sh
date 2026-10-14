#!/bin/sh
# @file    run.sh
# @brief   Gate1 wrapper for the measurement peer's own self test.
# @author  zhugengyu
# @date    15.10.2026
# @design registry - ref tools/mock/run.sh:150-190 (the CASE grammar and the
#          exit-code-agrees-with-the-report rule this translates into)
#
# The peer has no build product and needs no radio: `prober.py selftest` is the
# case set, and it already carries the negatives (a phase line with a missing
# field, a missing value that must not read as zero, an empty capture, the
# per-phase action grammar). This wrapper only translates its per-check lines
# into the container's CASE grammar, prefixes them with the suite name, and
# proves the suite can go red: a deliberately broken copy of one check must
# make the peer report failure, or the suite proves nothing.

set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
cd "$root"

suite=prober-selftest
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

python3 tools/prober/prober.py selftest > "$tmp/selftest.log" 2>&1
rc=$?

cases=$(sed -n \
    -e 's/^prober: selftest \(.*[^ ]\)  *ok$/CASE '"$suite"'\/\1 PASS/p' \
    -e 's/^prober: selftest \(.*[^ ]\)  *FAIL$/CASE '"$suite"'\/\1 FAIL/p' \
    "$tmp/selftest.log")

if [ -z "$cases" ]; then
    echo "CASE $suite/suite FAIL (the peer produced no check line)"
    sed 's/^/  /' "$tmp/selftest.log"
    exit 1
fi
printf '%s\n' "$cases"

# the summary line agrees with the peer's own exit code
if [ "$rc" = 0 ]; then
    echo "CASE $suite/selftest PASS"
else
    echo "CASE $suite/selftest FAIL"
fi

# red control: one check broken on purpose must turn the self test red
cp tools/prober/prober.py "$tmp/prober-broken.py"
sed -i 's/num("0x1d") == 29/num("0x1d") == 30/' "$tmp/prober-broken.py"
if python3 "$tmp/prober-broken.py" selftest >/dev/null 2>&1; then
    echo "CASE $suite/red-control FAIL (a broken check still passed)"
    exit 1
fi
echo "CASE $suite/red-control PASS"

if printf '%s\n' "$cases" | grep -q ' FAIL$'; then
    exit 1
fi
exit 0

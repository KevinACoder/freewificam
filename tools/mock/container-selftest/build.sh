#!/bin/sh
# @file    build.sh
# @brief   Build the self test fixtures.
# @author  zhugengyu
# @date    07.10.2026
# @design test double - ref net80211:tests/run-host-tests.sh:1
#
# Three fixtures are compiled here (a pass, a designed red, a designed crash); the fourth
# is the negative source that must never compile, so building it is the test itself.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
out=$(cd "$here/.." && pwd)/build/container-selftest/fixtures
mkdir -p "$out"

for f in pass fail crash; do
    cc -O0 -Wall -o "$out/$f" "$here/fixtures/$f.c"
done

echo "container-selftest: fixtures built in $out"

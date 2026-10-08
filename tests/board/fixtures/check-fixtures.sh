#!/bin/sh
# @file    check-fixtures.sh
# @brief   Run the board-suite protocol fixtures: declaration matrix + judge matrix.
# @author  zhugengyu
# @date    08.10.2026
# @design  self-verifying fixture harness - ref tools/mock/container-selftest/run.sh
#          (a fixture set that proves the container can go red, not just green)
#          plus tests/board/threadx-rk3568-regression/judge.py (a judge judged
#          by its own selftest); here both directions are one command with one
#          verdict per fixture.
#
# Usage (works from any directory, including the repository root):
#   sh tests/board/fixtures/check-fixtures.sh "<board runner command>"
#
# <board runner command> runs the host-side board runner, which lives outside
# this repository (the repository side holds the declarations, the images and
# the judges); it is passed in so no local path is written down here. The two
# matrices:
#
#   declarations  <runner> --validate <suite.mk>
#       fixture-accept must be accepted (exit 0); every neg-* fixture must be
#       rejected (exit 2) - each is the complete declaration with one field
#       broken, so a rejection is evidence about that field.
#   judgments     python3 ref-judge/judge.py <transcript>
#       every static transcript under ref-judge/transcripts/ must land on the
#       exit code its manifest row declares: 0 pass, 1 fail (an incomplete run
#       included), 2 configuration error.
#
# Exit status: 0 every check matched; 1 at least one mismatch; 2 usage.

set -u

here=$(cd "$(dirname "$0")" && pwd)
judge=$here/ref-judge/judge.py
manifest=$here/ref-judge/transcripts/manifest.tsv

if [ $# -ne 1 ] || [ -z "$1" ]; then
	printf 'usage: sh %s "<board runner command>"\n' "$0" >&2
	exit 2
fi
runner=$1

checks=0
failed=0

pass() {
	checks=$((checks + 1))
	printf 'PASS %s\n' "$1"
}

# $1 = the failed check, $2 = the command output to show (optional)
fail() {
	checks=$((checks + 1))
	failed=$((failed + 1))
	printf 'FAIL %s\n' "$1"
	if [ $# -gt 1 ] && [ -n "$2" ]; then
		printf '%s\n' "$2" | sed 's/^/    | /'
	fi
}

# The runner command is a command line by design ($runner is deliberately
# unquoted: it may carry its interpreter), so the fixture path is the only
# argument appended to it.
check_declaration() {
	dir=$1
	want=$2
	why=$3

	out=$($runner --validate "$here/$dir/suite.mk" 2>&1)
	got=$?
	if [ "$got" -eq "$want" ]; then
		pass "declaration $dir: exit $got ($why)"
	else
		fail "declaration $dir: exit $got, want $want ($why)" "$out"
	fi
}

printf 'check-fixtures: declarations\n'
check_declaration fixture-accept 0 "the complete declaration is accepted"
check_declaration neg-no-truth-source 2 "missing SUITE_TRUTH_SOURCE"
check_declaration neg-no-end-pattern 2 "missing SUITE_END_PATTERN"
check_declaration neg-name-mismatch 2 "SUITE_NAME differs from the directory"
check_declaration neg-timeout 2 "SUITE_TIMEOUT is not a number"
check_declaration neg-empty-image 2 "SUITE_IMAGE is empty"

printf 'check-fixtures: judgments\n'
for transcript in "$here"/ref-judge/transcripts/*.txt; do
	name=$(basename "$transcript")
	want=$(awk -F'\t' -v name="$name" \
		'!/^#/ && $1 == name { print $2 }' "$manifest")
	if [ -z "$want" ]; then
		fail "judge $name: no expectation row in the manifest"
		continue
	fi

	out=$(python3 "$judge" "$transcript" 2>&1)
	got=$?
	if [ "$got" -eq "$want" ]; then
		pass "judge $name: exit $got"
	else
		fail "judge $name: exit $got, want $want" "$out"
	fi
done

if [ "$failed" -eq 0 ]; then
	printf 'check-fixtures: PASS (%d/%d matched)\n' "$checks" "$checks"
	exit 0
fi
printf 'check-fixtures: FAIL (%d of %d mismatched)\n' "$failed" "$checks"
exit 1

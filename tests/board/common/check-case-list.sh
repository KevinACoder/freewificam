#!/bin/sh
# Three-source case-name reconciliation for a regression suite.
#
# The case names live in three places that must agree, in order:
#   1. the image's BOARD_TESTS section (the table the runner walks),
#   2. the suite declaration (SUITE_CASE_NAMES in suite.mk, read by the host
#      runner and by the judges' reconciliation),
#   3. the judge's case table (judge.py CASES).
# A drift here is exactly the failure mode the judgment channel exists to
# catch, so it is caught at build time instead: a case that disappears from one
# source fails the build, not the board run.
#
# Usage: check-case-list.sh <elf> <suite.mk> <judge.py> <objcopy> <readelf> <scratch>
set -eu

elf=$1
suite_mk=$2
judge=$3
objcopy=$4
readelf=$5
scratch=$6

here=$(dirname "$0")

image_list=$(python3 "$here/extract-case-list.py" "$elf" "$objcopy" "$readelf" "$scratch" | tr '\n' ' ' | sed 's/ *$//')
declared_list=$(sed -n 's/^[[:space:]]*SUITE_CASE_NAMES[[:space:]]*:=[[:space:]]*//p' "$suite_mk" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
judge_list=$(python3 -c 'import sys; sys.path.insert(0, sys.argv[1]); import judge; print(" ".join(judge.CASES))' "$(dirname "$judge")")

echo "image : $image_list"
echo "declare: $declared_list"
echo "judge : $judge_list"

[ -n "$image_list" ] || { echo "FAIL: the image's test table is empty"; exit 1; }
[ "$image_list" = "$declared_list" ] || { echo "FAIL: image table and SUITE_CASE_NAMES differ"; exit 1; }
[ "$image_list" = "$judge_list" ] || { echo "FAIL: image table and judge case list differ"; exit 1; }
echo "PASS: image, declaration and judge list the same cases in the same order"

#!/bin/sh
# Three-source case-name reconciliation for one board image.
#
# The case names live in three places that must agree:
#   1. the image's BOARD_TESTS section (the table the runner walks),
#   2. the image's profile declaration (CONFIG_CASES in its defconfig),
#   3. the judge's case table (judge.py CASES) - the image may use a subset,
#      but every case it carries must be judgeable.
# A drift here is exactly the failure mode the judgment channel exists to
# catch, so it is caught at build time instead: a case that disappears from one
# source fails the build, not the board run.
#
# Usage: check-case-list.sh <elf> <defconfig> <judge.py> <objcopy> <readelf> <scratch>
set -eu

elf=$1
defconfig=$2
judge=$3
objcopy=$4
readelf=$5
scratch=$6

here=$(dirname "$0")

image_list=$(python3 "$here/extract-case-list.py" "$elf" "$objcopy" "$readelf" "$scratch" | tr '\n' ' ' | sed 's/ *$//')
declared_list=$(sed -n 's/^[[:space:]]*CONFIG_CASES[[:space:]]*=[[:space:]]*"\(.*\)"[[:space:]]*$/\1/p' "$defconfig" | tail -1)
judge_list=$(python3 -c 'import sys; sys.path.insert(0, sys.argv[1]); import judge; print(" ".join(judge.CASES))' "$(dirname "$judge")")

echo "image   : $image_list"
echo "declare : $declared_list"
echo "judge   : $judge_list"

[ -n "$image_list" ] || { echo "FAIL: the image's test table is empty"; exit 1; }
[ "$image_list" = "$declared_list" ] || { echo "FAIL: image table and CONFIG_CASES differ"; exit 1; }

for name in $image_list; do
	case " $judge_list " in
		*" $name "*) ;;
		*) echo "FAIL: image case '$name' is not in the judge table"; exit 1 ;;
	esac
done

echo "PASS: image, defconfig and judge agree"

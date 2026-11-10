#!/usr/bin/env bash
#
# check-commit-msg.sh - enforce docs/COMMIT-CONVENTION.md for dev_v2.
#
# Usage:
#   tools/check-commit-msg.sh <message-file>
#   tools/check-commit-msg.sh -        # read the message from stdin
#
# Exit: 0 pass, 1 violation, 2 usage.
#
set -u

file="${1:--}"
if [ "$file" = "-" ]; then
	msg="$(cat)" || exit 2
else
	[ -f "$file" ] || { echo "check-commit-msg: no such file: $file" >&2; exit 2; }
	msg="$(cat "$file")" || exit 2
fi

subject="${msg%%$'\n'*}"
fail=0
warn=0

say() { printf 'check-commit-msg: %s\n' "$*" >&2; }
bad() { say "FAIL: $*"; fail=1; }
soft() { say "WARN: $*"; warn=1; }

# Merge/revert commits are not subject to the format.
case "$subject" in
Merge*|Revert*) exit 0 ;;
esac

if [ -z "$subject" ]; then
	bad "empty subject"
	exit 1
fi

# <area>: (<label>) <lowercase imperative ...>
if ! [[ "$subject" =~ ^([a-z][a-z0-9-]*):\ \(([a-z][a-z0-9.-]*)\)\ [a-z] ]]; then
	bad "subject must be '<area>: (<label>) <lowercase imperative ...>'"
fi

if [ "${#subject}" -gt 72 ]; then
	bad "subject is ${#subject} chars; limit is 72"
fi

case "$subject" in
	*.) bad "subject must not end with a period" ;;
esac

if LC_ALL=C grep -qP '[^\x00-\x7F]' <<<"$subject"; then
	bad "subject must be ASCII"
fi

# Banned terms: local paths, lab identifiers, knowledge-base references.
banned='(K arm|L arm|R arm|K/L|L/K|\bWIP\b|\bwip\b|\bbackup\b|\bfixup\b|\btmp\b|~/|/home/|/mnt/|KI-|oslab|itx-wiki|project_rtos_webcam)'
if grep -Eq "$banned" <<<"$msg"; then
	hit="$(grep -Eo "$banned" <<<"$msg" | sort -u | tr '\n' ' ')"
	bad "banned term(s): $hit"
fi

# Documented vocabularies; unknown ones warn, do not fail.
areas='repo|docs|tools|wlan|netdev|bridge|board|lab|kernelmod|linuxnative|rtos|trm|proxy|contract|catalog|model|conformance'
labels='bootstrap|convention|plan|migration|gates|hooks|import|provenance|kernelmod|linuxnative|rtos|trm|proxy|contract|catalog|model|conformance|fix|docs|linux-ko|release|issues'

if [[ "$subject" =~ ^([a-z][a-z0-9-]*):\ \(([a-z][a-z0-9.-]*)\) ]]; then
	area="${BASH_REMATCH[1]}"
	label="${BASH_REMATCH[2]}"
	[[ "$area" =~ ^($areas)$ ]] || soft "area '$area' is not in the documented list"
	[[ "$label" =~ ^($labels)$ ]] || soft "label '$label' is not in the documented list"
fi

# Provenance is required when the message says it imports something.
if grep -Eqi 'import|imported|from trunk-base' <<<"$msg"; then
	if ! grep -Eq '(Provenance:|trunk-base [0-9a-f]{7,}|@[0-9a-f]{7,}|[0-9a-f]{40})' <<<"$msg"; then
		soft "import mentioned without a Provenance: line or a commit/tag id"
	fi
fi

if [ "$fail" -ne 0 ]; then
	exit 1
fi
exit 0

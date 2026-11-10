#!/bin/sh
# s0-compile.sh - compile one stack translation unit at a time and classify the
# first wall each one hits.
#
# This is a measurement, not a gate: the exit status is 0 when the harness ran,
# whatever the translation units did.  The product is the per-TU log plus the
# summary TSV; docs/HANDOFF-S0-HOST.md §2.4 says the round's output is the gap
# list, never "green".
#
# Usage:
#   s0-compile.sh <out-dir> <cc> <flags> <tu-list-file>
#
# <tu-list-file> has one source path per line, relative to the repository root.
# The caller (tests/board/rtos/build/Makefile) owns the flags; this script never
# invents one and refuses a flag set that reaches into linux-upstream/include
# (the stack must compile against this component's face, not the kernel tree).
set -eu

out=$1
cc=$2
flags=$3
list=$4
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cross=$(dirname "$cc")
home=${HOME:-/nonexistent}

[ -f "$list" ] || { echo "s0-compile: no TU list $list" >&2; exit 2; }
case " $flags " in
*linux-upstream*)
	echo "s0-compile: refusing a flag set that includes linux-upstream" >&2
	exit 2
	;;
esac

mkdir -p "$out/compile" "$out/obj"
summary=$out/summary.tsv
printf 'tu\tresult\tclass\tfirst_error\tblocked_by\twarnings\tlog\n' > "$summary"

total=0
passed=0
while IFS= read -r tu; do
	[ -n "$tu" ] || continue
	total=$((total + 1))
	rel=${tu#"$root"/}
	obj="$out/obj/$(printf '%s' "$rel" | tr '/' '_').o"
	log="$out/compile/$(printf '%s' "$rel" | tr '/' '_').log"
	if (
		cd "$root"
		"$cc" $flags -c "$rel" -o "$obj"
	) >"$log" 2>&1; then
		rc=0
	else
		rc=$?
	fi
	# Keep local paths out of the record (commit convention, banned terms).
	sed -e "s|$root|<FREEWIFICAM>|g" -e "s|$cross|<CROSS>|g" \
	    -e "s|$home|<HOME>|g" -i "$log"
	warnings=$(grep -c 'warning:' "$log" || true)
	if [ "$rc" -eq 0 ]; then
		printf '%s\tpass\t-\t-\t-\t%s\t%s\n' "$rel" "$warnings" "${log#"$out"/}" >>"$summary"
		passed=$((passed + 1))
		continue
	fi
	first=$(grep -m1 -E '(fatal )?error:' "$log" || true)
	class=kpi-missing
	blocked=-
	case "$first" in
	*"No such file or directory"*)
		class=lic-headers
		blocked=$(printf '%s' "$first" | sed -n "s/.*error: \(.*\): No such file.*/\1/p")
		;;
	*"implicit declaration of function"*)
		blocked=$(printf '%s' "$first" | sed -n "s/.*function '\([a-zA-Z0-9_]*\)'.*/\1/p")
		case "$blocked" in
		cfg80211_*|__cfg80211_*) class=cfg-missing ;;
		esac
		;;
	*"no member named"*)
		blocked=$(printf '%s' "$first" | sed -n "s/.*no member named '\([a-zA-Z0-9_]*\)'.*/\1/p")
		;;
	*"unknown type name"*)
		blocked=$(printf '%s' "$first" | sed -n "s/.*unknown type name '\([a-zA-Z0-9_]*\)'.*/\1/p")
		;;
	*)
		blocked=$(printf '%s' "$first" | sed -e 's/.*error: //' | cut -c1-100)
		;;
	esac
	[ -n "$blocked" ] || blocked=-
	first=${first#"<FREEWIFICAM>/"}
	printf '%s\tfail\t%s\t%s\t%s\t%s\t%s\n' \
	    "$rel" "$class" "$(printf '%s' "$first" | cut -c1-160)" \
	    "$blocked" "$warnings" "${log#"$out"/}" >>"$summary"
done <"$list"

printf 's0-compile: %s TUs, %s pass, %s fail -> %s\n' \
    "$total" "$passed" "$((total - passed))" "$summary"
exit 0

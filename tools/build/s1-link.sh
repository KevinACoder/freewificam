#!/bin/sh
# s1-link.sh - the S1c link inventory: what the stack + driver still need.
#
# One partial link (`ld -r`) of every stack and driver object this round built,
# then the undefined symbols that remain after subtracting the names the
# component's own kernel-API bodies define.  Nothing here produces an image or
# a binary: the product is the classified list.
#
# Usage:
#   s1-link.sh <out-dir> <ld> <nm>
#
# The caller (tests/board/rtos/build/Makefile, target s1-link) lays out:
#   <out-dir>/stack/obj/*.o   the 62 stack TUs
#   <out-dir>/driver/obj/*.o  the 72 iwlwifi TUs
#   <out-dir>/kpi/obj/*.o     the component's kernel-API bodies
#
# Outputs under <out-dir>:
#   combined.o        the partial link (absent when it failed; see ld.err)
#   undefined.tsv     symbol, class, provider
#   duplicates.tsv    symbol, definition count, objects
#   winners.txt       every ieee80211_*/cfg80211_* symbol and its definition count
#   summary.txt       the counts and the exit verdict
#
# Classes: provided (a kpi/component object defines it), cfg-missing
# (cfg80211_*/__cfg80211_*), libc (the image's C library / FreeRTOS provides
# it), kpi-missing (the rest - the real gap list).
set -eu

# Absolute: the partial link runs from inside <out>, and a caller may have
# named it relatively.
out=$(cd "$1" && pwd)
ld=$2
nm=$3

[ -d "$out/stack/obj" ] || { echo "s1-link: no stack objects under $out" >&2; exit 2; }
[ -d "$out/driver/obj" ] || { echo "s1-link: no driver objects under $out" >&2; exit 2; }

objs=$(ls "$out"/stack/obj/*.o "$out"/driver/obj/*.o 2>/dev/null || true)
[ -n "$objs" ] || { echo "s1-link: no objects" >&2; exit 2; }
printf '%s\n' $objs > "$out/objects.rsp"

# The partial link.  A duplicate strong definition makes ld fail before a
# symbol can be classified; that is a finding, not a crash, so the failure is
# recorded and the inventory continues from per-object nm output.
if (cd "$out" && $ld -r -o combined.o @objects.rsp) >"$out/ld.log" 2>"$out/ld.err"; then
	ld_status=ok
else
	ld_status=failed
fi

# One (symbol, object) row per definition.  Weak definitions are kept out of
# the duplicate count (the linker accepts them); they still count as a provider.
: >"$out/defs.tsv"
for f in $objs "$out"/kpi/obj/*.o; do
	[ -f "$f" ] || continue
	$nm --defined-only "$f" 2>/dev/null | awk -v f="$f" '
		NF >= 3 {
			t = $(NF - 1)
			if (t == "U") next
			# Low-case types are local (static) symbols: the same
			# header inline appears in many TUs under the same
			# name and that is normal.  Only global definitions
			# matter for the duplicate/winner question.
			if (t ~ /^[a-z]$/) next
			if (t == "W" || t == "V") next
			print $NF "\t" f
		}' >>"$out/defs.tsv"
done

cut -f1 "$out/defs.tsv" | sort -u >"$out/defined.txt"
cut -f1 "$out/defs.tsv" | sort | uniq -d >"$out/dup.names"

# The undefined set comes from the partial link when it succeeded, so symbols
# that other units in the same link define are already resolved; otherwise the
# per-object references are merged (still unique).
if [ "$ld_status" = ok ] && [ -f "$out/combined.o" ]; then
	$nm -u "$out/combined.o" 2>/dev/null | awk '{print $NF}' | sort -u >"$out/undefined.txt"
else
	for f in $objs; do
		$nm -u "$f" 2>/dev/null | awk '{print $NF}'
	done | sort -u >"$out/undefined.txt"
fi

is_libc() {
	case "$1" in
	__memcpy|__memset|__memmove|memchr|memcmp|memcpy|memmove|memset|\
	memscan|strcat|strchr|strcmp|strcpy|strlen|strncmp|strncpy|strnlen|\
	strncasecmp|strpbrk|strrchr|strstr|strtok|strtok_r|strscpy|strsep|\
	stpcpy|stpncpy|\
	sprintf|snprintf|vsnprintf|sscanf|vsscanf|printf|puts|abort|\
	_ctype|gcd|crc32_le|crc32_be|crc32c|__crc32c_le|\
	div_s64|div_u64|div64_u64|__divdi3|__udivdi3|__aeabi_uldivmod)
		return 0 ;;
	esac
	return 1
}

printf 'symbol\tclass\tprovider\n' >"$out/undefined.tsv"
p=0; c=0; l=0; k=0; total=0
while IFS= read -r sym; do
	[ -n "$sym" ] || continue
	total=$((total + 1))
	provider=$(awk -F'\t' -v s="$sym" '$1 == s {print $2; exit}' "$out/defs.tsv" || true)
	if [ -n "$provider" ]; then
		cls=provided; p=$((p + 1))
	elif printf '%s' "$sym" | grep -Eq '^(__)?cfg80211_'; then
		cls=cfg-missing; c=$((c + 1))
	elif is_libc "$sym"; then
		cls=libc; l=$((l + 1))
	else
		cls=kpi-missing; k=$((k + 1))
	fi
	printf '%s\t%s\t%s\n' "$sym" "$cls" "${provider:--}" >>"$out/undefined.tsv"
done <"$out/undefined.txt"

printf 'symbol\tdefinitions\tobjects\n' >"$out/duplicates.tsv"
dups=0
while IFS= read -r sym; do
	[ -n "$sym" ] || continue
	files=$(awk -F'\t' -v s="$sym" '$1 == s {print $2}' "$out/defs.tsv" | sort -u | tr '\n' ' ')
	printf '%s\t%s\t%s\n' "$sym" "$(printf '%s\n' $files | wc -l | tr -d ' ')" "$files" >>"$out/duplicates.tsv"
	dups=$((dups + 1))
done <"$out/dup.names"

# The one-winner rule (AGENTS §2.3): every stack/driver API symbol must have
# exactly one definition in this link.
: >"$out/winners.txt"
while IFS= read -r sym; do
	case "$sym" in
	ieee80211_*|__ieee80211_*|cfg80211_*|__cfg80211_*) ;;
	*) continue ;;
	esac
	n=$(awk -F'\t' -v s="$sym" '$1 == s {print $2}' "$out/defs.tsv" | sort -u | wc -l | tr -d ' ')
	printf '%s\t%s\n' "$sym" "$n" >>"$out/winners.txt"
done <"$out/defined.txt"
multi=$(awk -F'\t' '$2 > 1' "$out/winners.txt" | wc -l | tr -d ' ')

{
	printf 'objects=%s\n' "$(printf '%s\n' $objs | wc -l | tr -d ' ')"
	printf 'undefined_total=%s\n' "$total"
	printf 'provided_by_component=%s\n' "$p"
	printf 'class_cfg_missing=%s\n' "$c"
	printf 'class_libc_or_os=%s\n' "$l"
	printf 'class_kpi_missing=%s\n' "$k"
	printf 'duplicate_symbols=%s\n' "$dups"
	printf 'multi_winner_ieee80211_or_cfg80211=%s\n' "$multi"
	printf 'ld_status=%s\n' "$ld_status"
} >"$out/summary.txt"

printf 's1-link: %s objects, %s undefined (%s kpi-missing, %s cfg-missing, %s libc, %s provided), %s duplicates, ld=%s -> %s\n' \
	"$(printf '%s\n' $objs | wc -l | tr -d ' ')" "$total" "$k" "$c" "$l" "$p" "$dups" "$ld_status" "$out/summary.txt"
exit 0

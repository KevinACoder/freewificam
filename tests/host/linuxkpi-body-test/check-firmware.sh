#!/bin/sh
# @file    check-firmware.sh
# @brief   Verify every compiled-in firmware image: each blob translation unit's
#          byte array must have exactly the digest the manifest records, and
#          optionally the same bytes as a reference copy of the image.
# @author  zhugengyu
# @date    10.10.2026
#
# Usage: check-firmware.sh [reference-directory ...]
#
# Each array is read back out of the C source rather than from a built object, so
# the check works before the host binary links and cannot be fooled by a stale
# object.  With reference directories the comparison is two-source: each
# compiled-in image against the file the board's Linux lane ships for the same
# chip, searched for by the driver name in each directory in turn (the round
# ledger records which directories were given and which comparisons ran).

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
fw_dir=$root/components/linuxkpi/fw
manifest=$fw_dir/MANIFEST.sha256
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

entries=$(awk '$1 ~ /^[0-9a-f]{64}$/ {print $1, $2, $3}' "$manifest")
if [ -z "$entries" ]; then
	echo "check-firmware: FAIL (no digest in $manifest)" >&2
	exit 2
fi

# the reference copy of that image, or nothing when no directory holds one
find_reference() {
	refname=$1
	shift
	for dir in "$@"; do
		if [ -f "$dir/$refname" ]; then
			echo "$dir/$refname"
			return 0
		fi
	done
	return 1
}

printf '%s\n' "$entries" | while read -r want name blob; do
	# the array name is the blob file's stem with _blob -> _data
	array=$(basename "$blob" .c | sed 's/_blob$//')_data

	# Extract the initialiser body and turn it back into bytes.
	sed -n "/^static const unsigned char $array\[\] = {/,/^};/p" "$fw_dir/$blob" \
		| sed -e '1d' -e '$d' \
		| tr -s ' \t\n' ' ' \
		| tr ',' '\n' \
		| grep -o '0x[0-9a-f][0-9a-f]' \
		| sed 's/0x//' > "$work/hex"
	if [ ! -s "$work/hex" ]; then
		echo "check-firmware: FAIL (no bytes parsed out of $blob for $name)" >&2
		exit 2
	fi
	xxd -r -p "$work/hex" > "$work/image"
	size=$(wc -c < "$work/image")
	got=$(sha256sum "$work/image" | cut -d' ' -f1)

	if [ "$got" != "$want" ]; then
		echo "check-firmware: FAIL ($blob: digest $got != manifest $want)" >&2
		exit 1
	fi

	if [ "$#" -eq 0 ]; then
		echo "firmware: $name, $size bytes, digest $got matches the manifest"
		continue
	fi
	if ref_file=$(find_reference "$name" "$@"); then
		ref=$(sha256sum "$ref_file" | cut -d' ' -f1)
		if [ "$ref" != "$got" ]; then
			echo "check-firmware: FAIL ($blob: reference $ref_file digest $ref != compiled-in $got)" >&2
			exit 1
		fi
		echo "firmware: $name, $size bytes, digest $got matches the manifest and $ref_file"
	else
		echo "firmware: $name, $size bytes, digest $got matches the manifest (no reference copy found)"
	fi
done

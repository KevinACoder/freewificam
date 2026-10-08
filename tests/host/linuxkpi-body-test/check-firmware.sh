#!/bin/sh
# @file    check-firmware.sh
# @brief   Verify the compiled-in firmware image: the byte array in the blob
#          translation unit must have exactly the digest the manifest records, and
#          optionally the same bytes as a reference copy of the image.
# @author  zhugengyu
# @date    08.10.2026
#
# Usage: check-firmware.sh [reference-image]
#
# The array is read back out of the C source rather than from a built object, so the
# check works before the host binary links and cannot be fooled by a stale object.
# With a reference image the comparison is two-source: the compiled-in bytes against
# the file the board's Linux lane ships for the same chip (the round ledger records
# that invocation).

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
blob=$root/components/linuxkpi/fw/rtl8188eufw_blob.c
manifest=$root/components/linuxkpi/fw/MANIFEST.sha256
reference=${1:-}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

want=$(awk '$1 ~ /^[0-9a-f]{64}$/ {print $1}' "$manifest")
if [ -z "$want" ]; then
	echo "check-firmware: FAIL (no digest in $manifest)" >&2
	exit 2
fi

# Extract the initialiser body and turn it back into bytes.
sed -n '/^static const unsigned char rtl8188eufw_data\[\] = {/,/^};/p' "$blob" \
	| sed -e '1d' -e '$d' \
	| tr -s ' \t\n' ' ' \
	| tr ',' '\n' \
	| grep -o '0x[0-9a-f][0-9a-f]' \
	| sed 's/0x//' > "$work/hex"
if [ ! -s "$work/hex" ]; then
	echo "check-firmware: FAIL (no bytes parsed out of $blob)" >&2
	exit 2
fi
xxd -r -p "$work/hex" > "$work/image"
size=$(wc -c < "$work/image")
got=$(sha256sum "$work/image" | cut -d' ' -f1)

if [ "$got" != "$want" ]; then
	echo "check-firmware: FAIL (compiled-in image digest $got != manifest $want)" >&2
	exit 1
fi

if [ -n "$reference" ]; then
	if [ ! -f "$reference" ]; then
		echo "check-firmware: FAIL (reference image not found)" >&2
		exit 2
	fi
	ref=$(sha256sum "$reference" | cut -d' ' -f1)
	if [ "$ref" != "$got" ]; then
		echo "check-firmware: FAIL (reference digest $ref != compiled-in $got)" >&2
		exit 1
	fi
	echo "firmware: ${size} bytes, digest ${got} matches the manifest and the reference image"
else
	echo "firmware: ${size} bytes, digest ${got} matches the manifest"
fi

#!/bin/sh
# The AP6181 image gate: this driver is a first-party port of Cypress WWD and
# must not carry the Linux wireless stack.  The linker script keeps a set of
# empty __lkpi_* section boundaries (start == end) for the Linux profiles; they
# are not code and are allowed.  Anything else that looks like linuxkpi,
# mac80211, cfg80211 or brcmfmac is a failure.
#
# usage: wiced-image-gate.sh <image.elf>
set -eu

elf=${1:?usage: wiced-image-gate.sh <image.elf>}
NM=${NM:-arm-none-eabi-nm}

bad=$("$NM" -S "$elf" | awk '
	# name type size address
	$4 ~ /^(lkpi_|ieee80211_|cfg80211_|linuxkpi_|brcmfmac)/ { print $4 }
' | sort -u)

if [ -n "$bad" ]; then
	echo "wiced-image-gate: Linux wireless symbols in $elf:" >&2
	echo "$bad" >&2
	exit 1
fi

echo "wiced-image-gate: pass (no linuxkpi/mac80211/cfg80211/brcmfmac symbols)"

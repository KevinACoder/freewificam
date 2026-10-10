#!/bin/sh
# Cross-source check: the image's USB transfer stage must cover the widest single
# transfer the image's driver submits.
#
# The stage pool refuses any transfer larger than one slot, and the refusal is a
# permanent property of the request length, not a transient shortage: the engine
# parks the transfer and retries it forever, the driver never sees a completion,
# and the receive path - beacons, and the firmware's own C2H reports, which ride
# the same bulk-in endpoint - stays empty.  The two numbers live in different
# trees (the image's Makefile and the vendored driver's header), so the build
# compares them instead of trusting either side's comment.
#
# Usage: check-usb-stage-size.sh <stage-bytes> <driver-usb-header> [symbol]
set -eu

stage=$1
hdr=$2
sym=${3:-RTW89_USB_RECVBUF_SZ}

[ -f "$hdr" ] || { echo "FAIL: driver USB header not found: $hdr"; exit 1; }

recv=$(sed -n "s/^[[:space:]]*#[[:space:]]*define[[:space:]]*${sym}[[:space:]]\{1,\}\([0-9]\{1,\}\).*/\1/p" "$hdr" | head -1)
[ -n "$recv" ] || { echo "FAIL: $sym not found in $hdr"; exit 1; }

echo "image stage slot : $stage bytes"
echo "driver widest rx : $recv bytes ($sym)"

[ "$stage" -ge "$recv" ] || {
	echo "FAIL: the image's stage slot ($stage bytes) is smaller than the driver's"
	echo "      widest receive transfer ($recv bytes); every such transfer is"
	echo "      refused for good.  Raise CONFIG_LKPI_USB_CHERRYUSB_STAGE_SIZE in"
	echo "      this image's Makefile."
	exit 1
}
echo "PASS: the image's stage slot covers the driver's widest receive transfer"

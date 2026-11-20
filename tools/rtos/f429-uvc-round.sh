#!/bin/sh
# f429-uvc-round.sh - one F429 UVC camera boot over the Pi serial/flash path.
#
# Usage:
#   tools/rtos/f429-uvc-round.sh runs/<date>-<topic>/<boot>
#
# Profile: PROFILE (default stm32f429-uvc-480).  The judge's case list and
# target mode default to the 640x480 arm; override them for the detect-only or
# 800x600 arm, e.g.
#
#   PROFILE=stm32f429-uvc-detect \
#   UVC_CASES=uvc_detect UVC_VIDEO=0 \
#   tools/rtos/f429-uvc-round.sh runs/.../detect
#
# The script builds, records the image digest/size, starts the Pi's CH340
# capture, flashes the BIN and relay-cold-boots, then judges the captured log.
# It never pushes or commits.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT=${1:?usage: $0 <output-dir>}
PROFILE=${PROFILE:-stm32f429-uvc-480}
PI=${PI_HOST:-pi@192.168.0.17}
SERIAL_TTY=${SERIAL_TTY:-/dev/ttyUSB0}
CAPTURE_SECS=${CAPTURE_SECS:-60}
ELF="$ROOT/tests/board/rtos/build/build/rtos-freertos.elf"
BIN="$ROOT/tests/board/rtos/build/build/rtos-freertos.bin"

UVC_VIDEO=${UVC_VIDEO:-1}
UVC_CASES=${UVC_CASES:-}
if [ -z "$UVC_CASES" ]; then
	if [ "$UVC_VIDEO" = "1" ]; then
		UVC_CASES="uvc_detect uvc_open uvc_iso_smoke uvc_stream"
	else
		UVC_CASES="uvc_detect"
	fi
fi
UVC_VID=${UVC_VID:-046d}
UVC_PID=${UVC_PID:-0990}
UVC_WIDTH=${UVC_WIDTH:-640}
UVC_HEIGHT=${UVC_HEIGHT:-480}
UVC_IVAL=${UVC_IVAL:-1}
UVC_MPS_MAX=${UVC_MPS_MAX:-1023}
UVC_MIN_FRAMES=${UVC_MIN_FRAMES:-50}
UVC_MAX_FRAME=${UVC_MAX_FRAME:-131072}

mkdir -p "$OUT"

echo "== build $PROFILE =="
( cd "$ROOT" && ./make.rtos image PROFILE="$PROFILE" ) > "$OUT/build.log" 2>&1
cp "$ROOT/tests/board/rtos/build/build/build-info-freertos.txt" "$OUT/build-info.txt" 2>/dev/null || true
sha256sum "$ELF" "$BIN" > "$OUT/image.sha256"
( cd "$ROOT" && arm-none-eabi-size "$ELF" 2>/dev/null || true ) > "$OUT/size.txt"
cp "$BIN" "$OUT/rtos-uvc.bin"

echo "== Pi serial capture -> $OUT/serial.log =="
ssh "$PI" "stty -F $SERIAL_TTY 115200 raw -echo; timeout $CAPTURE_SECS cat $SERIAL_TTY" \
	> "$OUT/serial.log" 2>&1 &
CAP_PID=$!
sleep 1

echo "== relay cold boot + flash =="
rc=0
bash "$ROOT/../skills/stm32f429-board/scripts/pi-flash.sh" "$BIN" --cycle \
	> "$OUT/flash.log" 2>&1 || rc=$?

wait "$CAP_PID" || true
strings "$OUT/serial.log" > "$OUT/serial.txt" 2>/dev/null || true

echo "== judge =="
jrc=0
if [ "$UVC_VIDEO" = "1" ]; then
	python3 "$ROOT/tools/rtos/judge.py" "$OUT/serial.txt" \
		--json "$OUT/judge.json" \
		--expect "cases=$(echo "$UVC_CASES" | tr ' ' ',')" \
		--expect "uvc_vid=$UVC_VID" --expect "uvc_pid=$UVC_PID" \
		--expect "uvc_width=$UVC_WIDTH" --expect "uvc_height=$UVC_HEIGHT" \
		--expect "uvc_ival=$UVC_IVAL" --expect "uvc_mps_max=$UVC_MPS_MAX" \
		--expect "uvc_min_frames=$UVC_MIN_FRAMES" \
		--expect "uvc_max_frame_bytes=$UVC_MAX_FRAME" \
		> "$OUT/judge.txt" 2>&1 || jrc=$?
else
	python3 "$ROOT/tools/rtos/judge.py" "$OUT/serial.txt" \
		--json "$OUT/judge.json" --expect "cases=$UVC_CASES" \
		> "$OUT/judge.txt" 2>&1 || jrc=$?
fi
cat "$OUT/judge.txt" 2>/dev/null || true

if [ "$rc" -ne 0 ] || [ "$jrc" -ne 0 ]; then
	echo "FAIL: flash=$rc judge=$jrc (evidence in $OUT)" >&2
	exit 1
fi
echo "PASS: $PROFILE ($OUT)"

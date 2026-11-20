#!/bin/sh
# f429-uac-round.sh - one F429 UAC microphone boot over the Pi serial/flash
# path.
#
# Usage:
#   tools/rtos/f429-uac-round.sh runs/<date>-<topic>/<boot>
#
# Profile: PROFILE (default stm32f429-uac-mic).  The judge's case list follows
# UAC_CASES; override it for the detect-only or video+microphone arm:
#
#   PROFILE=stm32f429-uac-detect UAC_CASES=uac_detect \
#   tools/rtos/f429-uac-round.sh runs/.../a0
#
# The script builds, records the image digest/size, starts the Pi's CH340
# capture, flashes the BIN and relay-cold-boots, then judges the captured log.
# It never pushes or commits.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT=${1:?usage: $0 <output-dir>}
PROFILE=${PROFILE:-stm32f429-uac-mic}
PI=${PI_HOST:-pi@192.168.0.17}
SERIAL_TTY=${SERIAL_TTY:-/dev/ttyUSB0}
CAPTURE_SECS=${CAPTURE_SECS:-45}
ELF="$ROOT/tests/board/rtos/build/build/rtos-freertos.elf"
BIN="$ROOT/tests/board/rtos/build/build/rtos-freertos.bin"

UAC_CASES=${UAC_CASES:-uac_detect uac_mic}
UAC_VID=${UAC_VID:-046d}
UAC_PID=${UAC_PID:-0990}
UAC_AC=${UAC_AC:-2}
UAC_AS=${UAC_AS:-3}
UAC_RATE=${UAC_RATE:-16000}
UAC_VIDEO_MIN_FRAMES=${UAC_VIDEO_MIN_FRAMES:-200}
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
cp "$BIN" "$OUT/rtos-uac.bin"

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
python3 "$ROOT/tools/rtos/judge.py" "$OUT/serial.txt" \
	--json "$OUT/judge.json" \
	--expect "cases=$(echo "$UAC_CASES" | tr ' ' ',')" \
	--expect "uac_vid=$UAC_VID" --expect "uac_pid=$UAC_PID" \
	--expect "uac_ac=$UAC_AC" --expect "uac_as=$UAC_AS" \
	--expect "uac_rate=$UAC_RATE" \
	--expect "uac_video_min_frames=$UAC_VIDEO_MIN_FRAMES" \
	--expect "uvc_vid=$UVC_VID" --expect "uvc_pid=$UVC_PID" \
	--expect "uvc_width=$UVC_WIDTH" --expect "uvc_height=$UVC_HEIGHT" \
	--expect "uvc_ival=$UVC_IVAL" --expect "uvc_mps_max=$UVC_MPS_MAX" \
	--expect "uvc_min_frames=$UVC_MIN_FRAMES" \
	--expect "uvc_max_frame_bytes=$UVC_MAX_FRAME" \
	> "$OUT/judge.txt" 2>&1 || jrc=$?
cat "$OUT/judge.txt" 2>/dev/null || true

if [ "$rc" -ne 0 ] || [ "$jrc" -ne 0 ]; then
	echo "FAIL: flash=$rc judge=$jrc (evidence in $OUT)" >&2
	exit 1
fi
echo "PASS: $PROFILE ($OUT)"
#!/bin/sh
# f429-usb-msc-round.sh - one F429 U-disk acceptance boot over mculab.
#
# Usage: MCU=/path/to/mculab/scripts/mcu \
#          tools/rtos/f429-usb-msc-round.sh runs/<date>-<topic>/bootN
#
# The script builds the profile, records the image digest/size, asks mculab to
# flash/verify/reset, waits for usbh: msc PASS, and saves the serial tail.  A
# timeout is data: the tail and the flash JSON are kept and the script exits
# non-zero.  It never pushes or commits.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
PROFILE=${PROFILE:-stm32f429-usb-msc}
OUT=${1:?usage: MCU=... $0 <output-dir>}
MCU=${MCU:-mcu}
EXPECT=${EXPECT:-"usbh: msc PASS"}
EXPECT_TIMEOUT=${EXPECT_TIMEOUT:-45}
ELF="$ROOT/tests/board/rtos/build/build/rtos-freertos.elf"
BIN="$ROOT/tests/board/rtos/build/build/rtos-freertos.bin"

mkdir -p "$OUT"

echo "== build $PROFILE =="
( cd "$ROOT" && ./make.rtos image PROFILE="$PROFILE" ) > "$OUT/build.log" 2>&1
cp "$ROOT/tests/board/rtos/build/build/build-info-freertos.txt" "$OUT/build-info.txt"
sha256sum "$ELF" "$BIN" > "$OUT/image.sha256"
( cd "$ROOT" && arm-none-eabi-size "$ELF" 2>/dev/null || true ) > "$OUT/size.txt"

echo "== mculab doctor =="
"$MCU" doctor --json > "$OUT/doctor.json"

echo "== flash + expect '$EXPECT' =="
rc=0
"$MCU" flash "$ELF" --verify --reset --expect "$EXPECT" \
	--expect-timeout "$EXPECT_TIMEOUT" --json > "$OUT/flash.json" || rc=$?

# The serial buffer also carries earlier boots; the tail is the readable
# record, the flash JSON is the judged one.
"$MCU" serial tail -n 1000 --json > "$OUT/serial.json" || true
python3 - "$OUT/serial.json" "$OUT/serial.log" <<'PY' || true
import base64, json, sys
src, dst = sys.argv[1], sys.argv[2]
try:
    data = json.load(open(src))
    text = data.get("text", "")
    if not text:
        raw = base64.b64decode(data.get("data", ""))
        text = raw.decode("utf-8", errors="replace")
    open(dst, "w").write(text)
except Exception as exc:
    open(dst, "w").write("serial tail unavailable: %s\n" % exc)
PY

if [ "$rc" -eq 0 ]; then
    echo "PASS: $EXPECT ($OUT)"
else
    echo "FAIL: mcu flash rc=$rc (evidence in $OUT)" >&2
fi
exit "$rc"

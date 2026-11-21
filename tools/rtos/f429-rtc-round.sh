#!/bin/sh
# f429-rtc-round.sh - one F429 RTC/system-time boot over the Pi serial path.
#
# Usage:
#   tools/rtos/f429-rtc-round.sh runs/<date>-<topic>/<boot> [cold|nrst]
#
#   cold   boot 1: power the board, flash the image, relay-cold-boot it.
#          boot 3 (FLASH=0): no flash, just relay-cold-boot the same image.
#   nrst   boot 2: no flash, pyOCD reset (SYSRESETREQ).  The backup domain
#          and its RTC calendar survive; the boot counter must go up.
#
# Environment:
#   BUILD=1|0        build the profile first (default 1; boot 2/3 use 0)
#   FLASH=1|0        flash the BIN (default 1 only for a cold boot)
#   BDRST=1          after the load, reset the backup domain through pyOCD
#                    (PWREN, DBP, BDRST pulse) so the next cold boot is a
#                    genuine first boot.  Destructive: the stored calendar
#                    and the magic are discarded.
#   PROFILE=...      default stm32f429-rtc
#   CAPTURE_SECS=... default 40 (cold) / 25 (nrst)
#   EXPECT=...       judge expectation override
#
# The script records the image digest/size, the serial capture, the judged
# tail and the flash/reset command.  It never commits or pushes.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT=${1:?usage: $0 <output-dir> [cold|nrst]}
MODE=${2:-cold}
PROFILE=${PROFILE:-stm32f429-rtc}
PI=${PI_HOST:-pi@192.168.0.17}
SERIAL_TTY=${SERIAL_TTY:-/dev/ttyUSB0}
ELF="$ROOT/tests/board/rtos/build/build/rtos-freertos.elf"
BIN="$ROOT/tests/board/rtos/build/build/rtos-freertos.bin"
SIZE=${SIZE:-$HOME/workspace/env/src/xpack-arm-none-eabi-gcc-13.2.1-1.1/bin/arm-none-eabi-size}

if [ "$MODE" = "cold" ]; then
	BUILD=${BUILD:-1}
	FLASH=${FLASH:-1}
	CAPTURE_SECS=${CAPTURE_SECS:-40}
	EXPECT=${EXPECT:-"cases=rtc"}
else
	BUILD=${BUILD:-0}
	FLASH=${FLASH:-0}
	CAPTURE_SECS=${CAPTURE_SECS:-25}
	EXPECT=${EXPECT:-"cases=rtc rtc_boot_min=2 rtc_persist=required"}
fi

mkdir -p "$OUT"

if [ "$BUILD" = "1" ]; then
	echo "== build $PROFILE =="
	( cd "$ROOT" && ./make.rtos image PROFILE="$PROFILE" ) > "$OUT/build.log" 2>&1
	cp "$ROOT/tests/board/rtos/build/build/build-info-freertos.txt" \
		"$OUT/build-info.txt" 2>/dev/null || true
fi
[ -f "$BIN" ] || { echo "f429-rtc-round: no image at $BIN" >&2; exit 2; }

sha256sum "$ELF" "$BIN" > "$OUT/image.sha256"
"$SIZE" "$ELF" > "$OUT/size.txt" 2>/dev/null || true

echo "== Pi serial capture -> $OUT/serial.log =="
ssh "$PI" "stty -F $SERIAL_TTY 115200 raw -echo; timeout $CAPTURE_SECS cat $SERIAL_TTY" \
	> "$OUT/serial.log" 2>&1 &
CAP_PID=$!
sleep 1

if [ "$MODE" = "cold" ]; then
	{
		date -Is
		if [ "$FLASH" = "1" ]; then
			echo "== power on"
			bash "$ROOT/../skills/rk3568-board/scripts/power.sh" on --confirm || true
			sleep 3
			if [ "${BDRST:-0}" = "1" ]; then
				echo "== load (no reset) + backup-domain reset"
				scp -q "$BIN" "$PI:/tmp/f429-rtc.bin"
				ssh "$PI" "sudo pyocd load -t stm32f429xg -e sector --format bin --no-reset -a 0x08000000 /tmp/f429-rtc.bin"
				ssh "$PI" 'sudo pyocd cmd -t stm32f429xg -M halt -c "write32 0x40023840 0x10000000"'
				ssh "$PI" 'sudo pyocd cmd -t stm32f429xg -M halt -c "write32 0x40007000 0x00004100"'
				ssh "$PI" 'sudo pyocd cmd -t stm32f429xg -M halt -c "write32 0x40023870 0x00010000"'
				ssh "$PI" 'sudo pyocd cmd -t stm32f429xg -M halt -c "write32 0x40023870 0x00000000"'
				echo "== relay cycle"
				bash "$ROOT/../skills/rk3568-board/scripts/power.sh" cycle --confirm
			else
				echo "== load + relay cycle"
				bash "$ROOT/../skills/stm32f429-board/scripts/pi-flash.sh" "$BIN" --cycle
			fi
		else
			echo "== relay cycle (same image, no load)"
			bash "$ROOT/../skills/rk3568-board/scripts/power.sh" cycle --confirm
		fi
		date -Is
	} > "$OUT/flash.log" 2>&1
else
	{
		date -Is
		echo "== pyocd reset (SYSRESETREQ; backup domain stays)"
		ssh "$PI" "sudo pyocd reset -t stm32f429xg" || \
			ssh "$PI" "sudo pyocd cmd -t stm32f429xg -M attach -c reset"
		date -Is
	} > "$OUT/reset.log" 2>&1
fi

wait "$CAP_PID" || true

# The capture starts before the reset and may carry an earlier boot; the
# judged text starts at the last banner.
strings "$OUT/serial.log" | awk '
	/^rtos: banner backend=freertos/ { start = NR }
	{ line[NR] = $0 }
	END { for (i = (start ? start : 1); i <= NR; i++) print line[i] }
' > "$OUT/serial.txt" 2>/dev/null || true

echo "== judge ($EXPECT) =="
set --
for kv in $EXPECT; do
	set -- "$@" --expect "$kv"
done
jrc=0
python3 "$ROOT/tools/rtos/judge.py" "$OUT/serial.txt" \
	--json "$OUT/judge.json" "$@" > "$OUT/judge.txt" 2>&1 || jrc=$?
tail -20 "$OUT/judge.txt" || true

echo "== image =="
cat "$OUT/image.sha256"
exit "$jrc"

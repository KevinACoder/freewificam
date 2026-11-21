#!/bin/sh
# f429-stream-eth-round.sh - one wired-link stream round of the F429 -> Pi.
#
# Usage:
#   tools/rtos/f429-stream-eth-round.sh runs/<date>-<topic>/<boot>
#
# The profile is stm32f429-stream-eth (pi_stream stage 4 over the board's
# LAN8720A, CONFIG_STREAM_WINDOW_MS=600000): ten minutes of MJPEG plus the
# camera microphone's PCM to the Pi, which receives, checks the header/CRC and
# saves the raw streams.  The receiver runs on the Pi for the whole window and
# prints one stats line a minute, so a mid-run stall is visible in the log and
# not only in the exit totals.
#
# Outputs: build.log, build-info.txt, image.sha256, size.txt, flash.log,
# serial.log/serial.txt, judge.json, judge.txt, receiver.log, receiver-stats.json,
# stability.txt (the Pi-side interval summary).
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT=${1:?usage: $0 <output-dir>}
PROFILE=${PROFILE:-stm32f429-stream-eth}
PI=${PI_HOST:-pi@192.168.0.17}
SERIAL_TTY=${SERIAL_TTY:-/dev/ttyUSB0}
WINDOW_MS=${WINDOW_MS:-600000}
CAPTURE_SECS=${CAPTURE_SECS:-700}
ELF="$ROOT/tests/board/rtos/build/build/rtos-freertos.elf"
BIN="$ROOT/tests/board/rtos/build/build/rtos-freertos.bin"
TAG="f429-stream-eth-$(basename "$OUT")"
PI_HOME=/home/pi
PI_RUN="$PI_HOME/f429cam/run/$TAG"
PI_STATS="$PI_HOME/f429cam/run/$TAG/stats.json"

pi() { ssh "$PI" "$@"; }

mkdir -p "$OUT"

echo "== build $PROFILE =="
( cd "$ROOT" && ./make.rtos image PROFILE="$PROFILE" ) > "$OUT/build.log" 2>&1
cp "$ROOT/tests/board/rtos/build/build/build-info-freertos.txt" \
	"$OUT/build-info.txt" 2>/dev/null || true
sha256sum "$ELF" "$BIN" > "$OUT/image.sha256"
( cd "$ROOT" && arm-none-eabi-size "$ELF" 2>/dev/null || true ) \
	> "$OUT/size.txt"
cp "$BIN" "$OUT/rtos-stream.bin"

echo "== Pi receiver (deployed from this tree) =="
pi "pkill -f 'receiver[.]py --out' 2>/dev/null || true"
pi "mkdir -p $PI_RUN"
scp -q "$ROOT/tools/pi-stream/receiver.py" "$PI:$PI_HOME/f429cam/receiver.py"
pi "cd $PI_HOME/f429cam && timeout $(( CAPTURE_SECS + 30 )) python3 receiver.py \
	--out $PI_RUN --mode av --stats-json $PI_STATS \
	--stats-interval 60" > "$OUT/receiver.log" 2>&1 &
RECV_PID=$!
sleep 2

echo "== Pi serial capture -> $OUT/serial.log =="
pi "stty -F $SERIAL_TTY 115200 raw -echo; timeout $CAPTURE_SECS cat $SERIAL_TTY" \
	> "$OUT/serial.log" 2>&1 &
CAP_PID=$!
sleep 1

echo "== power on, load, relay cold boot =="
{ date -Is; echo "== power on"; } > "$OUT/flash.log"
bash "$ROOT/../skills/rk3568-board/scripts/power.sh" on --confirm \
	>> "$OUT/flash.log" 2>&1 || true
sleep 3
{ date -Is; echo "== load + cycle"; } >> "$OUT/flash.log"
rc=0
bash "$ROOT/../skills/stm32f429-board/scripts/pi-flash.sh" "$BIN" --cycle \
	>> "$OUT/flash.log" 2>&1 || rc=$?
date -Is >> "$OUT/flash.log"

echo "== wait for the board's result (window ${WINDOW_MS} ms) =="
deadline=$(( $(date +%s) + CAPTURE_SECS ))
seen_result=0
while [ "$(date +%s)" -lt "$deadline" ]; do
	if tr -d '\r' < "$OUT/serial.log" 2>/dev/null | \
		grep -aq "rtos: result"; then
		seen_result=1
		break
	fi
	sleep 1
done
echo "result seen: $seen_result"

# Stop the receiver cleanly: SIGTERM makes it write the stats JSON and the
# final totals.  Two seconds is the writer's own wake-up cadence.
pi "pkill -TERM -f 'receiver[.]py --out' 2>/dev/null || true"
sleep 2
scp -q "$PI:$PI_STATS" "$OUT/receiver-stats.json" 2>/dev/null || true
kill "$CAP_PID" 2>/dev/null || true
pi "pkill -f 'cat /dev/ttyUSB[0-9]' 2>/dev/null || true"
wait "$CAP_PID" 2>/dev/null || true
wait "$RECV_PID" 2>/dev/null || true

strings "$OUT/serial.log" | awk '
	/^rtos: banner backend=freertos/ { start = NR }
	{ line[NR] = $0 }
	END { for (i = (start ? start : 1); i <= NR; i++) print line[i] }
' > "$OUT/serial.txt" 2>/dev/null || true

echo "== judge =="
jrc=0
python3 "$ROOT/tools/rtos/judge.py" "$OUT/serial.txt" \
	--json "$OUT/judge.json" \
	--expect "cases=pi_stream" > "$OUT/judge.txt" 2>&1 || jrc=$?
tail -30 "$OUT/judge.txt" || true

echo "== stability summary (Pi side) =="
python3 - "$OUT" <<'PY' > "$OUT/stability.txt" 2>&1 || true
import json, re, statistics, sys
from pathlib import Path

out = Path(sys.argv[1])
rows = []
for line in (out / "receiver.log").read_text(errors="replace").splitlines():
    m = re.match(r"rx: t=(\d+)s video_frames=(\d+) video_bytes=(\d+) "
                 r"audio_blocks=(\d+) audio_bytes=(\d+) crc_err=(\d+) "
                 r"short=(\d+) jpeg_bad=(\d+) connects=(\{.*\})", line)
    if m:
        rows.append({"t": int(m.group(1)), "frames": int(m.group(2)),
                     "crc": int(m.group(6)), "short": int(m.group(7)),
                     "jpeg": int(m.group(8)), "conn": m.group(9)})
stats = {}
p = out / "receiver-stats.json"
if p.exists():
    try:
        stats = json.loads(p.read_text())
    except ValueError:
        stats = {}
print(f"receiver stats: {json.dumps(stats, sort_keys=True)}")
if not rows:
    print("stability: no periodic receiver lines")
    sys.exit(0)
frames = [r["frames"] for r in rows]
deltas = [b - a for a, b in zip(frames, frames[1:])]
print(f"stability: intervals={len(rows)} last_t={rows[-1]['t']}s "
      f"frames_total={frames[-1]} "
      f"per60s_min={min(deltas) if deltas else 'n/a'} "
      f"median={int(statistics.median(deltas)) if deltas else 'n/a'} "
      f"max={max(deltas) if deltas else 'n/a'} "
      f"crc={rows[-1]['crc']} short={rows[-1]['short']} "
      f"jpeg_bad={rows[-1]['jpeg']}")
stall = [i for i, d in enumerate(deltas) if d <= 0]
if stall:
    print(f"stability: EMPTY INTERVALS at {stall}")
errs = rows[-1]
if errs["crc"] or errs["short"] or errs["jpeg"]:
    print("stability: receiver errors present")
if stats and stats.get("video_frames", 0) == 0:
    print("stability: the receiver saved no video")
PY
cat "$OUT/stability.txt" || true

echo "== image =="
cat "$OUT/image.sha256"
[ "$rc" -eq 0 ] || echo "flash returned $rc (see $OUT/flash.log)" >&2
bad=$(grep -c 'stability: EMPTY INTERVALS\|stability: receiver errors\|stability: the receiver saved no video' \
	"$OUT/stability.txt" 2>/dev/null || true)
if [ "${bad:-0}" != 0 ]; then
	exit 1
fi
exit "$jrc"

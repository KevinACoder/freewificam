#!/bin/sh
# f429-eth-round.sh - one F429 wired-Ethernet boot over the Pi serial/flash path.
#
# Usage:
#   tools/rtos/f429-eth-round.sh runs/<date>-<topic>/<boot>
#
# Profile: PROFILE (default stm32f429-eth).  The script builds the image,
# records its digest/size, starts the Pi's CH340 capture, powers the board,
# flashes the BIN and relay-cold-boots it, judges the captured log, and then
# runs the two host-side witnesses the judge cannot see:
#   - the Pi pings the board's DHCP lease (the board's receive/answer path)
#   - the Pi pings a public address (the LAN's internet truth)
# It never pushes or commits.
#
# The relay is switched on before the SWD load: pyOCD cannot connect to an
# unpowered target, and the round's standard reset is the --cycle after the
# load (off >= 6 s, on, settle 12 s), not the load itself.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT=${1:?usage: $0 <output-dir>}
PROFILE=${PROFILE:-stm32f429-eth}
PI=${PI_HOST:-pi@192.168.0.17}
SERIAL_TTY=${SERIAL_TTY:-/dev/ttyUSB0}
CAPTURE_SECS=${CAPTURE_SECS:-90}
ELF="$ROOT/tests/board/rtos/build/build/rtos-freertos.elf"
BIN="$ROOT/tests/board/rtos/build/build/rtos-freertos.bin"
# The board's Ethernet is plugged into the lab router; the Pi is on the same
# L2 and is the peer witness.  The public target only has to answer from the
# Pi for the environment line; the board's own gate is in the case.
PEER_PUBLIC=${PEER_PUBLIC:-223.5.5.5}

mkdir -p "$OUT"

echo "== build $PROFILE =="
( cd "$ROOT" && ./make.rtos image PROFILE="$PROFILE" ) > "$OUT/build.log" 2>&1
cp "$ROOT/tests/board/rtos/build/build/build-info-freertos.txt" "$OUT/build-info.txt" 2>/dev/null || true
sha256sum "$ELF" "$BIN" > "$OUT/image.sha256"
( cd "$ROOT" && arm-none-eabi-size "$ELF" 2>/dev/null || true ) > "$OUT/size.txt"
cp "$BIN" "$OUT/rtos-eth.bin"

echo "== Pi serial capture -> $OUT/serial.log =="
ssh "$PI" "stty -F $SERIAL_TTY 115200 raw -echo; timeout $CAPTURE_SECS cat $SERIAL_TTY" \
	> "$OUT/serial.log" 2>&1 &
CAP_PID=$!
sleep 1

echo "== power on, relay cold boot after the load =="
# Wall-clock anchors: the flash log's own lines carry pyOCD-relative times, and
# the relay cycle ends with a 12 s settle, so a host timestamp after it dates
# the boot (and the DHCP exchange in a simultaneous tcpdump) to the second.
{ date -Is; echo "== power on"; } > "$OUT/flash.log"
bash "$ROOT/../skills/rk3568-board/scripts/power.sh" on --confirm \
	>> "$OUT/flash.log" 2>&1 || true
sleep 3
rc=0
{ date -Is; echo "== load + cycle"; } >> "$OUT/flash.log"
bash "$ROOT/../skills/stm32f429-board/scripts/pi-flash.sh" "$BIN" --cycle \
	>> "$OUT/flash.log" 2>&1 || rc=$?
date -Is >> "$OUT/flash.log"

wait "$CAP_PID" || true
# The capture starts before the SWD load (the probe needs the board powered),
# so the image that was in flash may boot once with the old build.  The judged
# text starts at the LAST banner: the relay cold boot after the load.  The raw
# capture is kept whole beside it.
strings "$OUT/serial.log" | awk '
	/^rtos: banner backend=freertos/ { start = NR }
	{ line[NR] = $0 }
	END { for (i = (start ? start : 1); i <= NR; i++) print line[i] }
' > "$OUT/serial.txt" 2>/dev/null || true

echo "== judge =="
jrc=0
python3 "$ROOT/tools/rtos/judge.py" "$OUT/serial.txt" \
	--json "$OUT/judge.json" \
	--expect "cases=f429_eth" > "$OUT/judge.txt" 2>&1 || jrc=$?
tail -20 "$OUT/judge.txt" || true

echo "== peer witnesses (host side) =="
LEASE=$(sed -n 's/.*f429_eth: dhcp ip=\([0-9.][0-9.]*\).*/\1/p' "$OUT/serial.txt" | head -1)
if [ -n "$LEASE" ]; then
	ssh "$PI" "ping -c 10 -W 1 $LEASE" > "$OUT/peer-ping.log" 2>&1 || true
	echo "peer-ping: $LEASE -> $(grep -E 'received|packet loss' "$OUT/peer-ping.log" | tail -1)"
else
	echo "peer-ping: no DHCP lease in the log" | tee "$OUT/peer-ping.log"
fi
ssh "$PI" "ping -c 5 -W 1 $PEER_PUBLIC" > "$OUT/pi-public-ping.log" 2>&1 || true
echo "pi-public-ping: $PEER_PUBLIC -> $(grep -E 'received|packet loss' "$OUT/pi-public-ping.log" | tail -1)"

echo "== image =="
cat "$OUT/image.sha256"
[ "$rc" -eq 0 ] || echo "flash returned $rc (see $OUT/flash.log)" >&2
exit "$jrc"

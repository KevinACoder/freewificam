#!/bin/bash
# mon-round.sh - one RTOS monitor round (docs/HANDOFF-AC7260-MONITOR.md).
#
#   tools/rtos/mon-round.sh --evidence runs/<date>-iwl7260-mon/mon1
#   tools/rtos/mon-round.sh --evidence .../mon2 --keep-pi   # second cold boot
#
# The image is `rk3568-iwl7260-mon` (tick pcie iwl7260_mon).  The bench is the
# standard one with one change: the Pi's MT7921AU (wlan1) is a managed client of
# lab-ap instead of a monitor, and it reconnects/pings for the whole window so
# the capture has a station's auth/assoc/probe/data frames next to the AP's
# beacons.  The AP itself (wlan2 / lab-ap / ch5 / 2432 MHz) is never touched.
#
# Outputs under --evidence: serial.log, transcript.txt, timings.json (the
# board tool's four-piece evidence), rtos.bin + sha256, bench-gate-*.txt,
# pi-station*.txt (the AP's own view), judge.json / judge.txt.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BOARD="$ROOT/../skills/rk3568-board/scripts"
PI=${PI:-pi@192.168.0.17}
SSID=${SSID:-labwifi-ar-ht20s}
CH=${CH:-5}
FREQ=${FREQ:-2432}
AP_IF=${AP_IF:-wlan2}
STA_IF=${STA_IF:-wlan1}
MON_EXPECT_MAC=${MON_EXPECT_MAC:-6c:29:95:12:f0:d9}
BIN="$ROOT/tests/board/rtos/build/build/rtos-freertos.bin"
TIMEOUT=${TIMEOUT:-420}
EVIDENCE=
KEEP_PI=0

while [ $# -gt 0 ]; do
	case "$1" in
		--evidence) EVIDENCE=${2:?}; shift 2 ;;
		--bin)      BIN=${2:?}; shift 2 ;;
		--timeout)  TIMEOUT=${2:?}; shift 2 ;;
		--keep-pi)  KEEP_PI=1; shift ;;
		-h|--help)  sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "mon-round: unknown argument $1" >&2; exit 2 ;;
	esac
done
[ -n "$EVIDENCE" ] || { echo "mon-round: --evidence DIR is required" >&2; exit 2; }
mkdir -p "$EVIDENCE"
EVIDENCE=$(cd "$EVIDENCE" && pwd)
TAG=$(basename "$EVIDENCE")
PI_LOG=/tmp/pi-mon-loop-$TAG.log

pi() { ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" "$@"; }
pisu() { pi "sudo -n $*"; }

RESTORED=0
restore_pi() {
	[ "$RESTORED" = 1 ] && return 0
	RESTORED=1
	pisu "pkill -f 'pi-mon-sta.sh loop' 2>/dev/null || true" >/dev/null 2>&1 || true
	[ "$KEEP_PI" = 1 ] && return 0
	pisu "bash /tmp/pi-mon-sta.sh down" > "$EVIDENCE/pi-restore.txt" 2>&1 || true
}
trap restore_pi EXIT

scp -q -o BatchMode=yes "$HERE/pi-mon-sta.sh" "$PI:/tmp/pi-mon-sta.sh"
pisu "chmod 755 /tmp/pi-mon-sta.sh"

{
	echo "tag=$TAG"
	echo "time=$(date -Is)"
	echo "channel=$CH freq=$FREQ ssid=$SSID expect_mac=$MON_EXPECT_MAC"
	pi 'systemctl is-active lab-ap 2>/dev/null || true'
	pisu "iw dev $AP_IF info" || true
	pisu "bash /tmp/pi-mon-sta.sh status" || true
} > "$EVIDENCE/bench-gate-before.txt" 2>&1 || true

# The AP gate: the standard bench's AP must be the one the GT arm captured
# beacons from, or the two arms are not the same measurement.
pisu "iw dev $AP_IF info" 2>/dev/null | grep -q "channel $CH " \
	|| { echo "mon-round: $AP_IF is not the AP on channel $CH" >&2; exit 3; }

pisu "bash /tmp/pi-mon-sta.sh up" > "$EVIDENCE/bench-gate-up.txt" 2>&1
grep -q 'STA_UP=1' "$EVIDENCE/bench-gate-up.txt" \
	|| { echo "mon-round: the Pi's STA did not join $SSID" >&2; exit 4; }

# The traffic source runs for the boot's whole life: every window of the case
# then has probe/auth/assoc and data frames next to the beacons.
pisu "bash -c 'nohup bash /tmp/pi-mon-sta.sh loop 600 >$PI_LOG 2>&1 </dev/null &'" \
	|| true

cp -f "$BIN" "$EVIDENCE/rtos.bin"
sha256sum "$EVIDENCE/rtos.bin" > "$EVIDENCE/rtos-bin.sha256"
"$BOARD/tftp.sh" put "$EVIDENCE/rtos.bin" --name rtos.bin \
	> "$EVIDENCE/tftp.txt" 2>&1

"$BOARD/boot-rtos.sh" --bin rtos.bin --success "rtos: result" --quiet 0 \
	--timeout "$TIMEOUT" --power-cycle --retry 1 --evidence "$EVIDENCE" \
	>"$EVIDENCE/boot.out" 2>&1 || true
sleep 2

SERIAL="$EVIDENCE/serial.log"
if ! [ -f "$SERIAL" ] || ! tr -d '\r' < "$SERIAL" | grep -aq 'iwl7260_mon: core ready'; then
	echo "mon-round: INVALID BOOT: no iwl7260_mon anchors (wrong image?)" >&2
	echo "invalid: wrong image (no iwl7260_mon anchor)" > "$EVIDENCE/invalid.txt"
	exit 2
fi

# The AP-side witness for M4: the monitor must never appear as a station.
pisu "iw dev $AP_IF station dump" > "$EVIDENCE/pi-station-after.txt" 2>&1 || true
grep -q "$MON_EXPECT_MAC" "$EVIDENCE/pi-station-after.txt" \
	&& echo "tx-visibility: the monitor MAC appears in the AP's station table" \
		> "$EVIDENCE/tx-witness.txt" \
	|| echo "tx-visibility: no monitor MAC in the AP's station table" \
		> "$EVIDENCE/tx-witness.txt"
pi "sudo -n tail -20 $PI_LOG" > "$EVIDENCE/pi-loop-tail.txt" 2>&1 || true

python3 "$HERE/judge.py" "$SERIAL" --json "$EVIDENCE/judge.json" \
	--expect cases=tick,pcie,iwl7260_mon \
	>"$EVIDENCE/judge.txt" 2>&1 || true

restore_pi
echo "mon-round: evidence -> $EVIDENCE"

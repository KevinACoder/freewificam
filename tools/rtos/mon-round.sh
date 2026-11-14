#!/bin/bash
# mon-round.sh - one RTOS monitor round (docs/HANDOFF-AC7260-MONITOR.md).
#
#   tools/rtos/mon-round.sh --evidence runs/<date>-iwl7260-mon/mon1
#   tools/rtos/mon-round.sh --evidence .../mon2 --keep-pi   # second cold boot
#   tools/rtos/mon-round.sh --card rtw89 \
#       --evidence runs/<date>-rtw89-8851bu-mon/mon1 \
#       --bin freewificam/tests/board/rtos/build/build/rtos-freertos.bin
#
# The card is selected by --card (default iwl7260, so the old command is
# unchanged):
#   iwl7260  image `rk3568-iwl7260-mon` (tick pcie iwl7260_mon)
#   rtw89    image `rk3568-rtw89-8851bu-mon` (tick rtw89_mon), the RTL8851BU
#            over the DWC3 xHCI, firmware 0.29.41.5
#
# The bench is the standard one with one change: the Pi's MT7921AU (wlan1) is a
# managed client of lab-ap instead of a monitor, and it reconnects/pings for the
# whole window so the capture has a station's auth/assoc/probe/data frames next
# to the AP's beacons.  The AP itself (wlan2 / lab-ap / ch5 / 2432 MHz) is never
# touched.
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
CARD=${CARD:-iwl7260}
MON_EXPECT_MAC=${MON_EXPECT_MAC:-}
BIN="$ROOT/tests/board/rtos/build/build/rtos-freertos.bin"
TIMEOUT=${TIMEOUT:-420}
EVIDENCE=
KEEP_PI=0

while [ $# -gt 0 ]; do
	case "$1" in
		--card)     CARD=${2:?}; shift 2 ;;
		--evidence) EVIDENCE=${2:?}; shift 2 ;;
		--bin)      BIN=${2:?}; shift 2 ;;
		--timeout)  TIMEOUT=${2:?}; shift 2 ;;
		--keep-pi)  KEEP_PI=1; shift ;;
		-h|--help)  sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "mon-round: unknown argument $1" >&2; exit 2 ;;
	esac
done

case "$CARD" in
	iwl7260)
		CASE=iwl7260_mon
		EXPECT_CASES=tick,pcie,iwl7260_mon
		ANCHOR="iwl7260_mon: core ready"
		MON_EXPECT_MAC=${MON_EXPECT_MAC:-6c:29:95:12:f0:d9}
		;;
	rtw89)
		CASE=rtw89_mon
		EXPECT_CASES=tick,rtw89_mon
		ANCHOR="rtw89_mon: core ready"
		MON_EXPECT_MAC=${MON_EXPECT_MAC:-90:de:80:1b:25:ed}
		;;
	*)
		echo "mon-round: --card must be iwl7260 or rtw89" >&2
		exit 2
		;;
esac
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
	echo "tag=$TAG card=$CARD case=$CASE"
	echo "time=$(date -Is)"
	echo "channel=$CH freq=$FREQ ssid=$SSID expect_mac=$MON_EXPECT_MAC"
	pi 'systemctl is-active lab-ap 2>/dev/null || true'
	pisu "iw dev $AP_IF info" || true
	pisu "bash /tmp/pi-mon-sta.sh status" || true
} > "$EVIDENCE/bench-gate-before.txt" 2>&1 || true

# The AP gate: the standard bench's AP must be the one the round is tuned to,
# or the monitor is counting a different BSS.  Both cards use the same bench
# AP on the same channel; only the DUT and its case name change.
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

# boot-rtos.sh --retry 1 writes a retried boot under attempt-2/; the top
# level then has only the failed attempt's empty serial.log.  The boot that
# counts is the retried one, so promote its four-piece files and say so.
BOOTDIR=$EVIDENCE
if ! [ -s "$EVIDENCE/serial.log" ]; then
	LATEST=$(ls -dt "$EVIDENCE"/attempt-*/serial.log 2>/dev/null | head -1 || true)
	if [ -n "$LATEST" ] && [ -s "$LATEST" ]; then
		BOOTDIR=$(dirname "$LATEST")
		for f in serial.log transcript.txt timings.json meta.txt facts.txt \
			 milestones.tsv transfers.tsv; do
			[ -f "$BOOTDIR/$f" ] && cp -f "$BOOTDIR/$f" "$EVIDENCE/$f"
		done
		echo "boot-rtos retried; the boot that counts is $(basename "$BOOTDIR")" \
			> "$EVIDENCE/boot-retry.txt"
	fi
fi

SERIAL="$EVIDENCE/serial.log"
if ! [ -f "$SERIAL" ] || ! tr -d '\r' < "$SERIAL" | grep -aqF "$ANCHOR"; then
	echo "mon-round: INVALID BOOT: no $CASE anchors (wrong image?)" >&2
	echo "invalid: wrong image (no $CASE anchor)" > "$EVIDENCE/invalid.txt"
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
	--expect "cases=$EXPECT_CASES" \
	>"$EVIDENCE/judge.txt" 2>&1 || true

restore_pi
echo "mon-round: evidence -> $EVIDENCE"

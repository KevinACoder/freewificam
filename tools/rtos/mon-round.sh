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
#   rtl8xxxu image `rk3568-rtl8xxxu-8188eu-mon` (tick rtl8xxxu_mon), the
#            RTL8188EUS behind the external hub on the bench's "xhci0",
#            firmware 28.0 (the driver's fourth recorded patch turns the
#            role on)
#
# The M5 A/B arm (`--ab`) does not turn the Pi's MT7921AU into a client: the
# standard bench stays (lab-ap + wlan1 monitor ch5) and the same beacons are
# counted by both monitors in the same window.  The comparison is a separate
# verdict (`tools/rtos/mon-ab.py`), because it answers a different question
# from M1-M4.
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
# The Pi's monitor interface: the --ab arm is the only mode that uses it
# directly (the client arm hands both radios to pi-mon-sta.sh).
MON_IF=${MON_IF:-wlan1}
STA_IF=${STA_IF:-wlan1}
CARD=${CARD:-iwl7260}
MON_EXPECT_MAC=${MON_EXPECT_MAC:-}
AB=${AB:-0}
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
		--ab)       AB=1; shift ;;
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
	rtl8xxxu)
		CASE=rtl8xxxu_mon
		EXPECT_CASES=tick,rtl8xxxu_mon
		ANCHOR="rtl8xxxu_mon: core ready"
		MON_EXPECT_MAC=${MON_EXPECT_MAC:-20:f4:1b:52:8f:01}
		;;
	*)
		echo "mon-round: --card must be iwl7260, rtw89 or rtl8xxxu" >&2
		exit 2
		;;
esac
[ -n "$EVIDENCE" ] || { echo "mon-round: --evidence DIR is required" >&2; exit 2; }
mkdir -p "$EVIDENCE"
EVIDENCE=$(cd "$EVIDENCE" && pwd)
TAG=$(basename "$EVIDENCE")
PI_LOG=/tmp/pi-mon-loop-$TAG.log
PI_DIR=${PI_DIR:-/home/pi/mon-$TAG}

pi() { ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" "$@"; }
pisu() { pi "sudo -n $*"; }

RESTORED=0
restore_pi() {
	[ "$RESTORED" = 1 ] && return 0
	RESTORED=1
	pisu "pkill -f 'pi-mon-sta.sh loop' 2>/dev/null || true" >/dev/null 2>&1 || true
	[ "$KEEP_PI" = 1 ] && return 0
	[ "$AB" = 1 ] && return 0
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

if [ "$AB" = 1 ]; then
	# M5: the standard bench stays.  The Pi's MT7921AU must be the monitor on
	# the same channel the DUT's monitor is tuned to, and the bench AP must be
	# up, or the two monitors are looking at different air.
	pi 'systemctl is-active lab-ap' > "$EVIDENCE/bench-gate-up.txt" 2>&1 || true
	pisu "iw dev $MON_IF info" >> "$EVIDENCE/bench-gate-up.txt" 2>&1 || true
	pi 'systemctl is-active lab-ap' 2>/dev/null | grep -q '^active' \
		|| { echo "mon-round: --ab needs lab-ap up" >&2; exit 4; }
	pisu "iw dev $MON_IF info" 2>/dev/null | grep -q 'type monitor' \
		|| { echo "mon-round: --ab needs $MON_IF as a monitor" >&2; exit 4; }
	pisu "iw dev $MON_IF info" 2>/dev/null | grep -q "channel $CH " \
		|| { echo "mon-round: --ab needs $MON_IF on channel $CH" >&2; exit 4; }
else
	pisu "bash /tmp/pi-mon-sta.sh up" > "$EVIDENCE/bench-gate-up.txt" 2>&1
	grep -q 'STA_UP=1' "$EVIDENCE/bench-gate-up.txt" \
		|| { echo "mon-round: the Pi's STA did not join $SSID" >&2; exit 4; }

	# The traffic source runs for the boot's whole life: every window of the
	# case then has probe/auth/assoc and data frames next to the beacons.
	pisu "bash -c 'nohup bash /tmp/pi-mon-sta.sh loop 600 >$PI_LOG 2>&1 </dev/null &'" \
		|| true
fi

cp -f "$BIN" "$EVIDENCE/rtos.bin"
sha256sum "$EVIDENCE/rtos.bin" > "$EVIDENCE/rtos-bin.sha256"
"$BOARD/tftp.sh" put "$EVIDENCE/rtos.bin" --name rtos.bin \
	> "$EVIDENCE/tftp.txt" 2>&1

AB_CAP_PID=
if [ "$AB" = 1 ]; then
	AB_PCAP=$PI_DIR/mon-ab.pcap
	# tcpdump on this bench drops to its own user before writing: the output
	# directory must exist and be writable, or the capture dies with
	# "Couldn't change ownership of savefile" and an empty tsv (ab1, first try).
	pi "mkdir -p $PI_DIR" || { echo "mon-round: cannot create $PI_DIR" >&2; exit 5; }
	# The board prints `window ms=` immediately before it starts counting, so
	# a capture started on that line covers the DUT's window from its first
	# frame (the 20 s timeout is longer than the 15 s window on purpose; the
	# comparison uses each side's own rate, never the two counts raw).
	(
		for _i in $(seq 1 3000); do
			if [ -f "$EVIDENCE/serial.log" ] && \
			   tr -d '\r' < "$EVIDENCE/serial.log" | \
			   grep -aq "$CASE: window ms="; then
				break
			fi
			sleep 0.3
		done
		pisu "timeout 20 tcpdump -i $MON_IF -s 0 -w $AB_PCAP" \
			> "$EVIDENCE/ab-tcpdump.log" 2>&1
	) &
	AB_CAP_PID=$!
fi

"$BOARD/boot-rtos.sh" --bin rtos.bin --success "rtos: result" --quiet 0 \
	--timeout "$TIMEOUT" --power-cycle --retry 1 --evidence "$EVIDENCE" \
	>"$EVIDENCE/boot.out" 2>&1 || true
sleep 2
if [ -n "${AB_CAP_PID:-}" ]; then
	wait "$AB_CAP_PID" 2>/dev/null || true
fi

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

# M3's reference: the bench AP's own address (BSSID).  `iw dev wlan2 info`
# prints it as `addr`; the DUT's monitor must have a beacon from it among the
# frames it kept, and ch5 carries neighbours, so "some BSSID" is not M3.
AP_BSSID=$(pisu "iw dev $AP_IF info" 2>/dev/null | \
	sed -n 's/^[[:space:]]*addr \([0-9a-f:]*\)$/\1/p' | head -1)
echo "bench_ap_bssid=${AP_BSSID:-unknown}" > "$EVIDENCE/bench-ap-bssid.txt"
# The channel's beacon census (one line per BSSID the sink kept), so the run
# record can name the neighbours instead of reporting one anonymous BSS.
tr -d '\r' < "$SERIAL" 2>/dev/null | \
	sed -n "s/.*$CASE: frame .* fc=0x0080 hdr=\([0-9a-f]*\).*/\1/p" | \
	awk '{ if (length($0) >= 44) { b = substr($0, 33, 12); \
	       printf "%s:%s:%s:%s:%s:%s\n", substr(b,1,2), substr(b,3,2), \
	       substr(b,5,2), substr(b,7,2), substr(b,9,2), substr(b,11,2) } }' | \
	sort | uniq -c | sort -rn > "$EVIDENCE/kept-beacon-bssids.txt" 2>&1 || true

if [ -n "$AP_BSSID" ]; then
	python3 "$HERE/judge.py" "$SERIAL" --json "$EVIDENCE/judge.json" \
		--expect "cases=$EXPECT_CASES" --expect "mon_bssid=$AP_BSSID" \
		>"$EVIDENCE/judge.txt" 2>&1 || true
else
	python3 "$HERE/judge.py" "$SERIAL" --json "$EVIDENCE/judge.json" \
		--expect "cases=$EXPECT_CASES" \
		>"$EVIDENCE/judge.txt" 2>&1 || true
fi

if [ "$AB" = 1 ]; then
	pisu "chmod 644 $AB_PCAP" >/dev/null 2>&1 || true
	{
		echo "pcap=$AB_PCAP"
		pisu "sha256sum $AB_PCAP" 2>/dev/null || true
		pisu "stat -c '%n %s' $AB_PCAP" 2>/dev/null || true
	} > "$EVIDENCE/ab-pcap.txt" 2>&1 || true
	pisu "tshark -r $AB_PCAP -Y 'wlan.fc.type_subtype==8' -T fields \
		-e frame.time_relative -e wlan.bssid -e wlan.fixed.beacon" \
		2>/dev/null > "$EVIDENCE/ab-pi-beacons.tsv" || true
	python3 "$HERE/mon-ab.py" --serial "$SERIAL" \
		--pi-beacons "$EVIDENCE/ab-pi-beacons.tsv" \
		${AP_BSSID:+--bssid "$AP_BSSID"} \
		--json "$EVIDENCE/ab-compare.json" \
		>"$EVIDENCE/ab-compare.txt" 2>&1 || true
	cat "$EVIDENCE/ab-compare.txt" 2>/dev/null || true
fi

restore_pi
echo "mon-round: evidence -> $EVIDENCE"

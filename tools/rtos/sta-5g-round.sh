#!/bin/bash
# sta-5g-round.sh - one RTOS STA round on the 5 GHz band (docs/HANDOFF-5GHZ.md P3/P4).
#
#   tools/rtos/sta-5g-round.sh --evidence runs/<date>-iwl7260-5ghz/sta5g-1
#   tools/rtos/sta-5g-round.sh --evidence .../sta5g-wpa2-1 --wpa2
#
# The image is `rk3568-iwl7260-sta5g` (tick pcie iwl7260_sta5g) or its WPA2
# variant.  The Pi side is pi-g2-ap.sh with BAND=5g: RTL8812AU (wlan2) becomes an
# 802.11a/n AP on 36/5180 with `iw reg set US`, MT7921AU (wlan1) the monitor on
# the same channel, and the round restores lab-ap and the 2.4 GHz monitor after
# itself.
#
# The verdict input this round adds is the association's frequency: the case
# reads it back out of cfg80211 and prints `link freq=`, and the judge requires
# it to be the requested 5 GHz channel.  A STA that associates on 2.4 GHz is a
# failed 5 GHz attempt, not a pass.
#
# Outputs under --evidence: serial.log, bench-gate-*.txt, pi-scan.txt,
# pi-station.txt, beacon-fields.txt (the Pi AP's own beacons on the monitor),
# judge.json / judge.txt.

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BOARD="$ROOT/../skills/rk3568-board/scripts"
PI=${PI:-pi@192.168.0.17}
CH=${CH:-36}
FREQ=${FREQ:-5180}
SSID=${SSID:-labwifi-5g}
PI_IP=${PI_IP:-192.168.99.1}
BOARD_IP=${BOARD_IP:-192.168.99.2}
MON_IF=${MON_IF:-wlan1}
AP_IF=${AP_IF:-wlan2}
PSK=${PSK:-rtos-7260-wpa2-psk}
WPA2=${WPA2:-0}
# The open arm is the default: --wpa2 flips both the Pi's AP and the image.
OPEN=1
REGTMP=
BIN="$ROOT/tests/board/rtos/build/build/rtos-freertos.bin"
TIMEOUT=420
EVIDENCE=

while [ $# -gt 0 ]; do
	case "$1" in
		--evidence) EVIDENCE=${2:?}; shift 2 ;;
		--bin)      BIN=${2:?}; shift 2 ;;
		--timeout)  TIMEOUT=${2:?}; shift 2 ;;
		--channel)  CH=${2:?}; shift 2 ;;
		--freq)     FREQ=${2:?}; shift 2 ;;
		--ssid)     SSID=${2:?}; shift 2 ;;
		--wpa2)     WPA2=1; OPEN=0; shift ;;
		-h|--help)  sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "sta-5g-round: unknown argument $1" >&2; exit 2 ;;
	esac
done
[ -n "$EVIDENCE" ] || { echo "sta-5g-round: --evidence DIR is required" >&2; exit 2; }
mkdir -p "$EVIDENCE"
EVIDENCE=$(cd "$EVIDENCE" && pwd)
TAG=$(basename "$EVIDENCE")
PI_DIR=/home/pi/pi-sta5g-$TAG
BEACON_PCAP=$PI_DIR/beacon.pcap

pi() { ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" "$@"; }
pisu() { pi "sudo -n $*"; }
pih() { pi "sudo -n env BAND=5g OPEN=$OPEN CH=$CH FREQ=$FREQ SSID=$SSID PI_IP=$PI_IP BOARD_IP=$BOARD_IP PSK=$PSK bash /tmp/pi-g2-ap.sh $*"; }

RESTORED=0
beacon_pid=
restore_pi() {
	[ "$RESTORED" = 1 ] && return 0
	RESTORED=1
	[ -n "${beacon_pid:-}" ] && kill "$beacon_pid" 2>/dev/null || true
	pih "down" > "$EVIDENCE/pi-restore.txt" 2>&1 || true
}
trap restore_pi EXIT

scp -q -o BatchMode=yes "$HERE/pi-g2-ap.sh" "$PI:/tmp/pi-g2-ap.sh"
pisu "chmod 755 /tmp/pi-g2-ap.sh"
pi "mkdir -p $PI_DIR"

{
	echo "tag=$TAG"
	echo "time=$(date -Is)"
	echo "channel=$CH freq=$FREQ ssid=$SSID ap=$PI_IP sta=$BOARD_IP wpa2=$WPA2"
	pi 'systemctl is-active lab-ap 2>/dev/null || true'
	pih status
	pih reg
} > "$EVIDENCE/bench-gate-before.txt" 2>&1 || true

# The Pi AP comes up before the board boots: the board's scan runs in the first
# seconds of the case and a late AP would be a false negative.
pih "up" > "$EVIDENCE/bench-gate-up.txt" 2>&1
pisu "iw dev $AP_IF info" >> "$EVIDENCE/bench-gate-up.txt" 2>&1 || true
pisu "iw dev $MON_IF info" >> "$EVIDENCE/bench-gate-up.txt" 2>&1 || true
pisu "iw dev $MON_IF info" 2>/dev/null | grep -q "channel $CH" \
	|| { echo "sta-5g-round: $MON_IF is not a monitor on channel $CH" >&2; exit 3; }

pisu "timeout 60 tcpdump -i $MON_IF -s 0 -w $BEACON_PCAP" \
	>"$EVIDENCE/beacon-tcpdump.log" 2>&1 &
beacon_pid=$!

cp -f "$BIN" "$EVIDENCE/rtos.bin"
sha256sum "$EVIDENCE/rtos.bin" > "$EVIDENCE/rtos-bin.sha256"
"$BOARD/tftp.sh" put "$EVIDENCE/rtos.bin" --name rtos.bin \
	> "$EVIDENCE/tftp.txt" 2>&1

pisu "iw dev $AP_IF station dump" > "$EVIDENCE/pi-station-before.txt" 2>&1 || true

"$BOARD/boot-rtos.sh" --bin rtos.bin --success "rtos: result" --quiet 0 \
	--timeout "$TIMEOUT" --power-cycle --retry 1 --evidence "$EVIDENCE" \
	>"$EVIDENCE/boot.out" 2>&1 &
BOOT_PID=$!

SERIAL="$EVIDENCE/serial.log"
seen_peer=0

anchor() {
	[ -f "$SERIAL" ] || return 1
	tr -d '\r' < "$SERIAL" 2>/dev/null | grep -aqE "$1"
}

for _ in $(seq 1 2400); do
	if [ "$seen_peer" = 0 ] && anchor 'iwl7260_sta5g: peer ip='; then
		seen_peer=1
		pisu "iw dev $AP_IF station dump" > "$EVIDENCE/pi-station.txt" 2>&1 || true
		pih "station" >> "$EVIDENCE/pi-station.txt" 2>&1 || true
	fi
	if ! kill -0 "$BOOT_PID" 2>/dev/null; then
		break
	fi
	sleep 0.3
done

wait "$BOOT_PID" || true
sleep 2

if ! [ -f "$SERIAL" ] || ! tr -d '\r' < "$SERIAL" | grep -aq 'iwl7260_sta5g: core ready'; then
	echo "sta-5g-round: INVALID BOOT: no iwl7260_sta5g anchors (wrong image?)" >&2
	echo "invalid: wrong image (no iwl7260_sta5g anchor)" > "$EVIDENCE/invalid.txt"
	exit 2
fi
if [ "$WPA2" = 1 ] && ! tr -d '\r' < "$SERIAL" | grep -aq 'iwl7260_sta5g: security wpa2=1'; then
	echo "sta-5g-round: INVALID BOOT: --wpa2 but no security wpa2=1 anchor" >&2
	echo "invalid: wpa2 image not deployed" > "$EVIDENCE/invalid.txt"
	exit 2
fi

[ -n "${beacon_pid:-}" ] && wait "$beacon_pid" 2>/dev/null || true
pisu "iw dev $AP_IF station dump" > "$EVIDENCE/pi-station-after.txt" 2>&1 || true
pih "log" > "$EVIDENCE/pi-ap-log.txt" 2>&1 || true
pih "down" > "$EVIDENCE/bench-gate-after.txt" 2>&1 || true
RESTORED=1

if pi "sudo -n test -f $BEACON_PCAP"; then
	pisu "chmod 644 $BEACON_PCAP 2>/dev/null || true"
	{
		pisu "sha256sum $BEACON_PCAP 2>/dev/null || true"
		echo "== beacons: bssid ssid interval dtim freq"
		pisu "tshark -r $BEACON_PCAP -Y 'wlan.fc.type_subtype==8' -T fields -e wlan.bssid -e wlan.ssid -e wlan.fixed.beacon -e wlan.tim.dtim_period -e radiotap.channel.freq 2>/dev/null | sort -u | head -20" || true
		echo "== association: subtype sa da"
		pisu "tshark -r $BEACON_PCAP -Y 'wlan.fc.type_subtype==0 || wlan.fc.type_subtype==1 || wlan.fc.type_subtype==11' -T fields -e frame.time_relative -e wlan.fc.type_subtype -e wlan.sa -e wlan.da 2>/dev/null | head -40" || true
	} > "$EVIDENCE/beacon-fields.txt" 2>&1 || true
fi

mkdir -p "$EVIDENCE/pi"
pi "sudo -n tar -C $(dirname "$PI_DIR") -cf - $(basename "$PI_DIR")" \
	| tar -C "$EVIDENCE/pi" -xf - 2>/dev/null || true

python3 "$HERE/judge.py" "$SERIAL" --json "$EVIDENCE/judge.json" \
	--expect cases=tick,pcie,iwl7260_sta5g --expect "freq=$FREQ" \
	>"$EVIDENCE/judge.txt" 2>&1 || true

echo "sta-5g-round: evidence -> $EVIDENCE"

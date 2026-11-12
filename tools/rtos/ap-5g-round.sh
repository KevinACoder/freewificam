#!/bin/bash
# ap-5g-round.sh - one RTOS AP round on the 5 GHz band (docs/HANDOFF-5GHZ.md P1/P2).
#
#   tools/rtos/ap-5g-round.sh --evidence runs/<date>-iwl7260-5ghz/ap5g-1
#   tools/rtos/ap-5g-round.sh --evidence .../ap5g-wpa2-1 --wpa2
#
# The image is `rk3568-iwl7260-ap5g` (tick pcie iwl7260_ap5g) or its WPA2
# variant; both are built and deployed by the caller, because the image path is
# one shared build product and the round must not guess which profile it got -
# the case's own `security wpa2=` anchor is checked after the boot instead.
#
# The Pi side is freewificam/tools/rtos/pi-ap.sh with CH=36 FREQ=5180: wlan1
# (MT7921AU) becomes the STA and wlan2 (RTL8812AU) the monitor on the AP's
# channel, and the Pi's regulatory domain is recorded before and after (the
# bench's US domain is what makes 5 GHz usable for a client; the board side has
# no such help, which is the point of the round).
#
# What this round does NOT do: the phase table.  5 GHz's gate is function -
# beacon, association, and 20/20 ping in both directions - and the throughput
# table lives in the AP round's case, not here.
#
# Outputs under --evidence: serial.log / transcript.txt, bench-gate-*.txt,
# pi-scan.txt / pi-join.txt / pi-link.txt / pi-ping.txt, beacon-fields.txt,
# judge.json / judge.txt.

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BOARD="$ROOT/../skills/rk3568-board/scripts"
PI=${PI:-pi@192.168.0.17}
CH=${CH:-36}
FREQ=${FREQ:-5180}
SSID=${SSID:-rtos-7260-ap-5g}
BOARD_IP=${BOARD_IP:-192.168.99.1}
PI_IP=${PI_IP:-192.168.99.2}
MON_IF=${MON_IF:-wlan2}
PSK=${PSK:-rtos-7260-wpa2-psk}
WPA2=${WPA2:-0}
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
		--wpa2)     WPA2=1; shift ;;
		-h|--help)  sed -n '2,26p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "ap-5g-round: unknown argument $1" >&2; exit 2 ;;
	esac
done
[ -n "$EVIDENCE" ] || { echo "ap-5g-round: --evidence DIR is required" >&2; exit 2; }
mkdir -p "$EVIDENCE"
EVIDENCE=$(cd "$EVIDENCE" && pwd)
TAG=$(basename "$EVIDENCE")
PI_DIR=/home/pi/pi-ap5g-$TAG
BEACON_PCAP=$PI_DIR/beacon.pcap
WPA_PCAP=$PI_DIR/wpa4way.pcap

pi() { ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" "$@"; }
pisu() { pi "sudo -n $*"; }
pih() { pi "sudo -n env CH=$CH FREQ=$FREQ SSID=$SSID PI_IP=$PI_IP AP_IP=$BOARD_IP PSK=$PSK WPA2=$WPA2 bash /tmp/pi-ap.sh $*"; }

RESTORED=0
beacon_pid=
wpa_pid=
restore_pi() {
	[ "$RESTORED" = 1 ] && return 0
	RESTORED=1
	[ -n "${beacon_pid:-}" ] && kill "$beacon_pid" 2>/dev/null || true
	[ -n "${wpa_pid:-}" ] && kill "$wpa_pid" 2>/dev/null || true
	pih "servers-stop" >/dev/null 2>&1 || true
	pih "down" > "$EVIDENCE/pi-restore.txt" 2>&1 || true
}
trap restore_pi EXIT

scp -q -o BatchMode=yes "$HERE/pi-ap.sh" "$PI:/tmp/pi-ap.sh"
pisu "chmod 755 /tmp/pi-ap.sh"
pi "mkdir -p $PI_DIR"

# --- gate: the bench must be the standard one, and the domain must be known --
{
	echo "tag=$TAG"
	echo "time=$(date -Is)"
	echo "channel=$CH freq=$FREQ ssid=$SSID ap=$BOARD_IP sta=$PI_IP wpa2=$WPA2"
	pi 'systemctl is-active lab-ap 2>/dev/null || true'
	pih status
	pih reg-get
} > "$EVIDENCE/bench-gate-before.txt" 2>&1 || true

pih "up" > "$EVIDENCE/bench-gate-up.txt" 2>&1
pih "reg-get" >> "$EVIDENCE/bench-gate-up.txt" 2>&1 || true
pisu "iw dev $MON_IF info" >> "$EVIDENCE/bench-gate-up.txt" 2>&1 || true
pih "mac" > "$EVIDENCE/pi-mac.txt" 2>&1 || true
STA_MAC=$(tr -d '\r\n' < "$EVIDENCE/pi-mac.txt" 2>/dev/null || true)
echo "sta_mac=${STA_MAC:-?}" >> "$EVIDENCE/bench-gate-up.txt"
pisu "iw dev $MON_IF info" 2>/dev/null | grep -q "channel $CH" \
	|| { echo "ap-5g-round: $MON_IF is not a monitor on channel $CH" >&2; exit 3; }

pih "servers-stop" >/dev/null 2>&1 || true
cp -f "$BIN" "$EVIDENCE/rtos.bin"
sha256sum "$EVIDENCE/rtos.bin" > "$EVIDENCE/rtos-bin.sha256"
"$BOARD/tftp.sh" put "$EVIDENCE/rtos.bin" --name rtos.bin \
	> "$EVIDENCE/tftp.txt" 2>&1

"$BOARD/boot-rtos.sh" --bin rtos.bin --success "rtos: result" --quiet 0 \
	--timeout "$TIMEOUT" --power-cycle --retry 1 --evidence "$EVIDENCE" \
	>"$EVIDENCE/boot.out" 2>&1 &
BOOT_PID=$!

SERIAL="$EVIDENCE/serial.log"
seen_start=0
seen_peer=0

anchor() {
	[ -f "$SERIAL" ] || return 1
	tr -d '\r' < "$SERIAL" 2>/dev/null | grep -aqE "$1"
}

for _ in $(seq 1 2400); do
	if [ "$seen_start" = 0 ] && \
	   anchor 'iwl7260_ap5g: (hostap start rc=0|chan freq=[0-9]+ .*can_beacon=1)'; then
		seen_start=1
		pisu "timeout 15 tcpdump -i $MON_IF -s 0 -w $BEACON_PCAP" \
			>"$EVIDENCE/beacon-tcpdump.log" 2>&1 &
		beacon_pid=$!
		if [ "$WPA2" = 1 ]; then
			pisu "timeout 25 tcpdump -i $MON_IF -s 0 -w $WPA_PCAP" \
				>"$EVIDENCE/wpa-tcpdump.log" 2>&1 &
			wpa_pid=$!
		fi
		# Two scans 1 s apart: the first can land before the first beacon.
		pih "scan" > "$EVIDENCE/pi-scan.txt" 2>&1 || true
		sleep 1
		pih "scan" >> "$EVIDENCE/pi-scan.txt" 2>&1 || true
		if [ "$WPA2" = 1 ]; then
			pih "join-wpa" > "$EVIDENCE/pi-join.txt" 2>&1 || true
			pih "wpa-status" > "$EVIDENCE/pi-wpa-status.txt" 2>&1 || true
			pih "wpa-log" > "$EVIDENCE/pi-wpa.log" 2>&1 || true
		else
			pih "join" > "$EVIDENCE/pi-join.txt" 2>&1 || true
		fi
		pih "link" > "$EVIDENCE/pi-link.txt" 2>&1 || true
	fi

	if [ "$seen_peer" = 0 ] && anchor 'iwl7260_ap5g: peer window ms='; then
		seen_peer=1
		# The 4-way, when there is one, completes between the station table
		# entry and the ping; the case waits for it itself, so the peer's
		# ping only has to be late enough to land after association.
		[ "$WPA2" = 1 ] && sleep 6
		pih "ping" > "$EVIDENCE/pi-ping.txt" 2>&1 || true
	fi

	if ! kill -0 "$BOOT_PID" 2>/dev/null; then
		break
	fi
	sleep 0.3
done

wait "$BOOT_PID" || true
sleep 2

# --- the image identity check the banner cannot give ------------------------
if ! [ -f "$SERIAL" ] || ! tr -d '\r' < "$SERIAL" | grep -aq 'iwl7260_ap5g: core ready'; then
	echo "ap-5g-round: INVALID BOOT: no iwl7260_ap5g anchors (wrong image?)" >&2
	echo "invalid: wrong image (no iwl7260_ap5g anchor)" > "$EVIDENCE/invalid.txt"
	exit 2
fi
if [ "$WPA2" = 1 ] && ! tr -d '\r' < "$SERIAL" | grep -aq 'iwl7260_ap5g: security wpa2=1'; then
	echo "ap-5g-round: INVALID BOOT: --wpa2 but no security wpa2=1 anchor" >&2
	echo "invalid: wpa2 image not deployed" > "$EVIDENCE/invalid.txt"
	exit 2
fi

pih "servers-stop" >/dev/null 2>&1 || true
[ -n "${beacon_pid:-}" ] && wait "$beacon_pid" 2>/dev/null || true
[ -n "${wpa_pid:-}" ] && wait "$wpa_pid" 2>/dev/null || true
pih "link" > "$EVIDENCE/pi-link-after.txt" 2>&1 || true
pih "reg-get" > "$EVIDENCE/pi-reg-after.txt" 2>&1 || true
pih "down" > "$EVIDENCE/bench-gate-after.txt" 2>&1 || true
RESTORED=1

if pi "sudo -n test -f $BEACON_PCAP"; then
	pisu "chmod 644 $BEACON_PCAP $WPA_PCAP 2>/dev/null || true"
	{
		pisu "sha256sum $BEACON_PCAP $WPA_PCAP 2>/dev/null || true"
		echo "== beacons: bssid ssid interval dtim freq"
		pisu "tshark -r $BEACON_PCAP -Y 'wlan.fc.type_subtype==8' -T fields -e wlan.bssid -e wlan.ssid -e wlan.fixed.beacon -e wlan.tim.dtim_period -e radiotap.channel.freq 2>/dev/null | sort -u | head -20" || true
		echo "== count by subtype"
		pisu "tshark -r $BEACON_PCAP -T fields -e wlan.fc.type_subtype 2>/dev/null | sort | uniq -c | head -20" || true
	} > "$EVIDENCE/beacon-fields.txt" 2>&1 || true
fi

if [ "$WPA2" = 1 ] && pi "sudo -n test -f $WPA_PCAP"; then
	{
		pisu "sha256sum $WPA_PCAP 2>/dev/null || true"
		pisu "tshark -r $WPA_PCAP -Y 'eapol' -T fields -e frame.time_relative -e wlan.sa -e wlan.da -e eapol.type -e eapol.keydes.key_info 2>/dev/null" || true
		echo "== count"
		pisu "tshark -r $WPA_PCAP -Y 'eapol' -T fields -e frame.number 2>/dev/null | wc -l" || true
	} > "$EVIDENCE/air-eapol.txt" 2>&1 || true
fi

mkdir -p "$EVIDENCE/pi"
pi "sudo -n tar -C $(dirname "$PI_DIR") -cf - $(basename "$PI_DIR")" \
	| tar -C "$EVIDENCE/pi" -xf - 2>/dev/null || true

python3 "$HERE/judge.py" "$SERIAL" --json "$EVIDENCE/judge.json" \
	--expect cases=tick,pcie,iwl7260_ap5g --expect "freq=$FREQ" \
	>"$EVIDENCE/judge.txt" 2>&1 || true

echo "ap-5g-round: evidence -> $EVIDENCE"

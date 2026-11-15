#!/bin/bash
# f429-wifi-ap-round.sh - one STM32F429 AP round over mculab + the Kali bench.
#
# Usage:
#   MCU=/path/to/mculab/scripts/mcu tools/rtos/f429-wifi-ap-round.sh <evidence-dir> \
#       [--bin <image.bin>] [--timeout <s>] [--no-peer]
#
# The image is built by the caller (`./make.rtos image PROFILE=...`); this script
# copies it into the evidence directory first, so the measured bytes and the
# flashed bytes are one file, then:
#
#   1. puts the bench into the AP-round shape: the Pi's RTL8812AU (wlan2) becomes
#      a monitor on the AP's channel and the MT7921AU (wlan1) a managed station
#      with 192.168.99.2/24, while lab-ap is stopped (tools/rtos/pi-ap.sh up);
#   2. starts a bounded monitor capture, because the board's own beacons are the
#      A1 witness (the F429's frames are visible to the 8812AU, not to the
#      MT7921AU monitor used by the station rounds);
#   3. flashes the BIN (not the ELF: mculab rejects the >1 MiB debug image) and
#      cold-boots the board through the relay, because an MCU reset does not
#      reset the dongle;
#   4. follows the board's serial anchors and drives the Pi's scan/join/ping and
#      the throughput peer arms from them;
#   5. restores the bench and writes a verdict file.
#
# Per boot the evidence directory gets: image.sha256, build-info.txt, size.txt,
# doctor.json, flash.json, relay-cycle.txt, serial.log (the ring tail),
# serial-boot.log (sliced at the last banner), pi-*.txt, beacon-fields.txt,
# air-*.txt, verdict.txt.  The monitor pcap stays on the Pi with its hash.
#
# A boot with no banner, no case anchors, a different flashed sha, or a dongle
# stuck in the previous image's state (`rx_err=32` at the scan) is invalid: it
# is recorded in invalid.txt and must be repeated.
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BOARD="$ROOT/../skills/rk3568-board/scripts"
PI=${PI:-pi@192.168.0.17}
MCU=${MCU:-mcu}

SSID=${SSID:-rtos-8188-ap}
CH=${CH:-5}
FREQ=${FREQ:-2432}
BOARD_IP=${BOARD_IP:-192.168.99.1}
PI_IP=${PI_IP:-192.168.99.2}
STA_IF=${STA_IF:-wlan1}
MON_IF=${MON_IF:-wlan2}
UDP_UP_RATE=${UDP_UP_RATE:-10M}

BIN="$ROOT/tests/board/rtos/build/build/rtos-freertos.bin"
EVIDENCE=
TIMEOUT=300
NO_PEER=0

while [ $# -gt 0 ]; do
	case "$1" in
		--bin)      BIN=${2:?}; shift 2 ;;
		--timeout)  TIMEOUT=${2:?}; shift 2 ;;
		--no-peer)  NO_PEER=1; shift ;;
		-h|--help)  sed -n '2,32p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		-*)         echo "f429-wifi-ap-round: unknown argument $1" >&2; exit 2 ;;
		*)          EVIDENCE=$1; shift ;;
	esac
done
[ -n "$EVIDENCE" ] || { echo "usage: MCU=... $0 <evidence-dir> [--bin IMAGE]" >&2; exit 2; }
[ -f "$BIN" ] || { echo "f429-wifi-ap-round: no image at $BIN (build a profile first)" >&2; exit 2; }

mkdir -p "$EVIDENCE"
EVIDENCE=$(cd "$EVIDENCE" && pwd)
TAG=$(basename "$EVIDENCE")
PI_DIR=/home/pi/f429-ap-$TAG
SERIAL="$EVIDENCE/serial.log"

pi() { ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" "$@"; }
pisu() { pi "sudo -n $*"; }
pih() { pi "sudo -n env CH=$CH FREQ=$FREQ SSID=$SSID PI_IP=$PI_IP AP_IP=$BOARD_IP STA_IF=$STA_IF MON_IF=$MON_IF UDP_UP_RATE=$UDP_UP_RATE bash /tmp/pi-ap.sh $*"; }

say() { printf '%s\n' "$*"; }

restore() {
	[ -n "${MON_PID:-}" ] && kill "$MON_PID" 2>/dev/null || true
	if [ "$NO_PEER" = 0 ]; then
		pih "servers-stop" >/dev/null 2>&1 || true
		pih "down" > "$EVIDENCE/bench-gate-after.txt" 2>&1 || true
	fi
}
trap restore EXIT

# --- the image and the board service --------------------------------------
cp -f "$BIN" "$EVIDENCE/rtos.bin"
sha256sum "$EVIDENCE/rtos.bin" > "$EVIDENCE/image.sha256"
cp "$ROOT/tests/board/rtos/build/build/build-info-freertos.txt" "$EVIDENCE/build-info.txt" 2>/dev/null || true
{
	echo "bin bytes: $(stat -c%s "$EVIDENCE/rtos.bin")"
	arm-none-eabi-size "$ROOT/tests/board/rtos/build/build/rtos-freertos.elf" 2>/dev/null || true
} > "$EVIDENCE/size.txt" 2>&1

"$MCU" doctor --json > "$EVIDENCE/doctor.json" || {
	echo "f429-wifi-ap-round: mculab doctor failed (see doctor.json)" >&2
	exit 3
}
bash "$BOARD/power.sh" status --json > "$EVIDENCE/power-before.json" 2>&1 || true

# --- the bench ------------------------------------------------------------
if [ "$NO_PEER" = 0 ]; then
	{
		echo "tag=$TAG"
		echo "time=$(date -Is)"
		echo "ssid=$SSID channel=$CH freq=$FREQ board=$BOARD_IP peer=$PI_IP"
		pi 'systemctl is-active lab-ap 2>/dev/null || true'
		pih status
	} > "$EVIDENCE/bench-gate-before.txt" 2>&1 || true

	scp -q -o BatchMode=yes "$HERE/pi-ap.sh" "$PI:/tmp/pi-ap.sh"
	scp -q -o BatchMode=yes "$HERE/udp-sink.py" "$PI:/tmp/udp-sink-ap.py"
	pisu "chmod 755 /tmp/pi-ap.sh"
	pi "mkdir -p $PI_DIR"

	pih "up" > "$EVIDENCE/bench-gate-up.txt" 2>&1
	pisu "iw dev $MON_IF info" >> "$EVIDENCE/bench-gate-up.txt" 2>&1 || true
	pisu "iw dev $STA_IF info" >> "$EVIDENCE/bench-gate-up.txt" 2>&1 || true
	if ! pisu "iw dev $MON_IF info" 2>/dev/null | grep -q "channel $CH"; then
		echo "f429-wifi-ap-round: $MON_IF is not a monitor on channel $CH" >&2
		exit 3
	fi
	if ! pisu "iw dev $STA_IF info" 2>/dev/null | grep -q "type managed"; then
		echo "f429-wifi-ap-round: $STA_IF is not a managed station" >&2
		exit 3
	fi
	# A stale neighbour entry for the board's address on the peer is the one
	# bench state that makes a correct AP look broken (its replies then go to
	# another MAC); start from no entry at all.
	pisu "ip neigh del $BOARD_IP dev $STA_IF" >/dev/null 2>&1 || true
	pisu "ip neigh show $BOARD_IP dev $STA_IF" > "$EVIDENCE/peer-neigh-before.txt" 2>&1 || true
	pih "mac" > "$EVIDENCE/pi-mac.txt" 2>&1 || true

	# The A1 operator: the whole boot's frames, bounded; the pcap stays on the
	# Pi, this side keeps its hash and size.
	pisu "timeout 180 tcpdump -i $MON_IF -s 256 -w $PI_DIR/beacon.pcap" \
		> "$EVIDENCE/beacon-tcpdump.log" 2>&1 &
	MON_PID=$!
	sleep 1
fi

# --- flash, then cold boot ------------------------------------------------
# The flash needs the target powered: if the bench was switched off (or the
# relay is off), the probe cannot attach and the flash fails before it starts.
# The cold boot below still owns the reset; this only guarantees a target.
if ! bash "$BOARD/power.sh" status --json 2>/dev/null | grep -q '"power_on":true'; then
	echo "== relay on (target was off) =="
	bash "$BOARD/power.sh" on --confirm > "$EVIDENCE/relay-on.txt" 2>&1 || true
	sleep 3
fi
echo "== mcu flash (no reset; the relay cycle below is the reset) =="
"$MCU" flash "$EVIDENCE/rtos.bin" --bin-base 0x08000000 --verify --no-reset \
	--json > "$EVIDENCE/flash.json" || {
	echo "f429-wifi-ap-round: flash failed (see flash.json)" >&2
	exit 4
}
echo "== relay cold boot =="
bash "$BOARD/power.sh" cycle --confirm > "$EVIDENCE/relay-cycle.txt" 2>&1 || {
	echo "f429-wifi-ap-round: relay cycle failed (see relay-cycle.txt)" >&2
	exit 5
}

# --- follow the serial ----------------------------------------------------
seen_boot=0
seen_start_ap=0
seen_peer=0
seen_server=0
seen_phase2=0
seen_phase3=0
seen_end=0
PHASE_PIDS=()
idx=0
while [ $idx -lt $((TIMEOUT * 2)) ]; do
	if ! "$MCU" serial tail -n 20000 --json > "$EVIDENCE/serial-tail.json" 2>/dev/null; then
		sleep 0.5; idx=$((idx + 1)); continue
	fi
	python3 - "$EVIDENCE/serial-tail.json" "$SERIAL" <<'PY' || true
import json, sys
try:
    data = json.load(open(sys.argv[1]))
    open(sys.argv[2], "w").write(data.get("text", ""))
except Exception:
    pass
PY

	if [ "$seen_boot" = 0 ] && grep -aq "rtos: banner" "$SERIAL"; then
		seen_boot=1
		echo "boot at $(date -Is)" >> "$EVIDENCE/anchors.txt"
	fi

	if [ "$seen_start_ap" = 0 ] && grep -aq "f429_wifi_ap: start_ap rc=0" "$SERIAL"; then
		seen_start_ap=1
		echo "start_ap at $(date -Is)" >> "$EVIDENCE/anchors.txt"
		if [ "$NO_PEER" = 0 ]; then
			( pih "scan" > "$EVIDENCE/pi-scan.txt" 2>&1
			  pih "scan-all" >> "$EVIDENCE/pi-scan.txt" 2>&1
			  pih "join" > "$EVIDENCE/pi-join.txt" 2>&1
			  pih "link" > "$EVIDENCE/pi-link.txt" 2>&1 ) &
			PHASE_PIDS+=($!)
		fi
	fi

	if [ "$seen_peer" = 0 ] && grep -aq "f429_wifi_ap: peer window ms=" "$SERIAL"; then
		seen_peer=1
		echo "peer window at $(date -Is)" >> "$EVIDENCE/anchors.txt"
		if [ "$NO_PEER" = 0 ]; then
			( pih "ping" > "$EVIDENCE/pi-ping.txt" 2>&1
			  pih "station" > "$EVIDENCE/pi-station.txt" 2>&1 ) &
			PHASE_PIDS+=($!)
		fi
	fi

	if [ "$seen_server" = 0 ] && grep -aq "f429_wifi_ap: server tcp=" "$SERIAL"; then
		seen_server=1
		echo "server at $(date -Is)" >> "$EVIDENCE/anchors.txt"
		if [ "$NO_PEER" = 0 ]; then
			pih "server-tcp $PI_DIR/peer-tcpdown.txt 25" \
				> /dev/null 2>&1 &
			PHASE_PIDS+=($!)
		fi
	fi

	if [ "$seen_phase2" = 0 ] && grep -aq "f429_wifi_ap: phase 2 begin" "$SERIAL"; then
		seen_phase2=1
		echo "phase2 at $(date -Is)" >> "$EVIDENCE/anchors.txt"
		if [ "$NO_PEER" = 0 ]; then
			pih "client-tcp 20" > "$EVIDENCE/peer-tcpup.txt" 2>&1 &
			PHASE_PIDS+=($!)
		fi
	fi

	if [ "$seen_phase3" = 0 ] && grep -aq "f429_wifi_ap: phase 3 begin" "$SERIAL"; then
		seen_phase3=1
		echo "phase3 at $(date -Is)" >> "$EVIDENCE/anchors.txt"
		if [ "$NO_PEER" = 0 ]; then
			pih "client-udp 18" > "$EVIDENCE/peer-udpup.txt" 2>&1 &
			PHASE_PIDS+=($!)
			pih "sink /tmp/udp-sink-ap.py $PI_DIR/sink-udpdown.txt 25" \
				> /dev/null 2>&1 &
			PHASE_PIDS+=($!)
		fi
	fi

	if [ "$seen_end" = 0 ] && grep -aq "rtos: result " "$SERIAL"; then
		seen_end=1
		echo "result at $(date -Is)" >> "$EVIDENCE/anchors.txt"
		break
	fi
	if grep -aq "f429_wifi_ap: FAIL " "$SERIAL"; then
		echo "case FAIL at $(date -Is)" >> "$EVIDENCE/anchors.txt"
		# keep capturing a few lines after a FAIL, then stop
		sleep 2
		break
	fi
	sleep 0.5
	idx=$((idx + 1))
done

# --- collect --------------------------------------------------------------
sleep 2
[ -n "${MON_PID:-}" ] && { kill "$MON_PID" 2>/dev/null || true; wait "$MON_PID" 2>/dev/null || true; }
for p in "${PHASE_PIDS[@]:-}"; do wait "$p" 2>/dev/null || true; done 2>/dev/null || true

if [ "$NO_PEER" = 0 ]; then
	pih "servers-stop" >/dev/null 2>&1 || true
	pih "down" > "$EVIDENCE/bench-gate-after.txt" 2>&1 || true
fi

# The boot's own text: everything after the last banner in the ring.
python3 - "$SERIAL" "$EVIDENCE/serial-boot.log" <<'PY' || true
import sys
text = open(sys.argv[1], errors="replace").read()
lines = text.splitlines()
start = 0
for i, line in enumerate(lines):
    if "rtos: banner" in line:
        start = i
open(sys.argv[2], "w").write("\n".join(lines[start:]) + "\n")
PY

if [ "$NO_PEER" = 0 ]; then
	# The Pi's receivers and the monitor's beacon fields.
	if pi "test -d $PI_DIR" 2>/dev/null; then
		pi "sudo -n tar -C $(dirname "$PI_DIR") -cf - $(basename "$PI_DIR")" \
			| tar -C "$EVIDENCE" -xf - 2>/dev/null || true
	fi
	BOARD_MAC=$(sed -n 's/.*f429_wifi_ap: wlan0 present=1 ifindex=[0-9]* mac=\([0-9a-f:]*\).*/\1/p' \
		"$EVIDENCE/serial-boot.log" | head -1)
	{
		pisu "chmod 644 $PI_DIR/beacon.pcap 2>/dev/null || true"
		pisu "sha256sum $PI_DIR/beacon.pcap 2>/dev/null || true"
		pisu "stat -c '%n %s' $PI_DIR/beacon.pcap 2>/dev/null || true"
		echo "board_mac=${BOARD_MAC:-?}"
		if [ -n "${BOARD_MAC:-}" ]; then
			pisu "tshark -r $PI_DIR/beacon.pcap -Y 'wlan.fc.type_subtype==0x0008 && wlan.bssid==$BOARD_MAC' -T fields -e frame.time_epoch -e wlan.ssid -e wlan.fixed.beacon -e wlan.tim.dtim_period -e radiotap.channel.freq 2>/dev/null" \
				> "$EVIDENCE/beacon-frames.tsv" || true
			awk -F'\t' 'NF { n++ } END { printf "beacons=%d\n", n }' \
				"$EVIDENCE/beacon-frames.tsv" 2>/dev/null || true
			sed -n '1,5p' "$EVIDENCE/beacon-frames.tsv" 2>/dev/null || true
		fi
	} > "$EVIDENCE/beacon-fields.txt" 2>&1 || true
fi

# --- verdict --------------------------------------------------------------
{
	pass=1
	check() { # <label> <pattern>
		if grep -aqE "$2" "$EVIDENCE/serial-boot.log"; then
			echo "PASS $1"
		else
			echo "FAIL $1"
			pass=0
		fi
	}
	check "banner" "rtos: banner"
	check "case start" "rtos: case f429_wifi_ap start"
	check "A1 set_iface" "f429_wifi_ap: set_iface rc=0 type=3"
	check "A1 start" "f429_wifi_ap: start rc=0"
	check "A1 start_ap" "f429_wifi_ap: start_ap rc=0 freq=$FREQ"
	check "A1 link freq" "f429_wifi_ap: link freq=$FREQ"
	check "A1 pass" "f429_wifi_ap: PASS A1"
	check "A2 station" "f429_wifi_ap: sta n=[1-9]"
	check "A2 pass" "f429_wifi_ap: PASS A2"
	check "A3 small ping" "f429_wifi_ap: ping payload=56 tx=20 rx=20"
	check "A3 big ping" "f429_wifi_ap: ping payload=1400 tx=20 rx=20"
	check "A3 pass" "f429_wifi_ap: PASS A3"
	check "case pass" "rtos: case f429_wifi_ap PASS"
	check "image result" "rtos: result pass cases=1/1"

	if [ "$NO_PEER" = 0 ] && [ -f "$EVIDENCE/beacon-frames.tsv" ]; then
		n=$(awk -F'\t' 'NF { n++ } END { print n+0 }' "$EVIDENCE/beacon-frames.tsv")
		if [ "$n" -ge 10 ]; then echo "PASS A1 air witness (beacons=$n)"; else
			echo "FAIL A1 air witness (beacons=$n)"; pass=0; fi
	fi
	if [ "$NO_PEER" = 0 ]; then
		if grep -aq "Connected to" "$EVIDENCE/pi-link.txt" 2>/dev/null; then
			echo "PASS A2 peer link"
		else
			echo "FAIL A2 peer link"; pass=0
		fi
		if grep -aq "0% packet loss" "$EVIDENCE/pi-ping.txt" 2>/dev/null; then
			echo "PASS A3 peer ping"
		else
			echo "FAIL A3 peer ping"; pass=0
		fi
	fi
	if grep -aq "rx_err=32" "$EVIDENCE/serial-boot.log"; then
		echo "INVALID dongle state (rx_err=32)"; pass=0
	fi
	if [ "$pass" = 1 ]; then echo "VERDICT PASS"; else echo "VERDICT FAIL"; fi
} > "$EVIDENCE/verdict.txt" 2>&1

if grep -aq "VERDICT PASS" "$EVIDENCE/verdict.txt"; then
	echo "f429-wifi-ap-round: PASS ($EVIDENCE)"
	exit 0
fi
echo "f429-wifi-ap-round: FAIL (evidence in $EVIDENCE)" >&2
grep -a "FAIL\|INVALID" "$EVIDENCE/verdict.txt" >&2 || true
exit 1

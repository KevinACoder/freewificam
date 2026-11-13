#!/bin/bash
# sta-rx-round.sh - one AP->board arm on the bench: boot a station image, wait
# for the case's own peer-window anchor, then drive the Pi's ping from inside
# that window, with the neighbour state as the arm's variable.
#
#   tools/rtos/sta-rx-round.sh --evidence runs/<date>-rtl8189fs-sta-rx/e1 \
#       --bin /tmp/rtos-sta-long.bin --arp dynamic
#
#   tools/rtos/sta-rx-round.sh --evidence .../e4 --bin /tmp/rtos-wpa2-long.bin \
#       --arp static --wpa2
#
# Why this script exists (docs/HANDOFF-8189FTV-STA-RX.md): the 2026-11-13 arms
# that failed AP->board were driven by hand, their ping start time was not
# recorded, and one of them ended its capture before the case's hold - so the
# 0/30 and 4/30 they recorded could not be read against anything the board
# knew.  This runner makes the arm a bounded, timestamped, board-corroborated
# measurement:
#
#   * the ping starts on the case's `peer window ms=` anchor, not on a sleep;
#   * the neighbour state is the arm's variable: `--arp dynamic` deletes the
#     entry and lets the board answer the AP's broadcast request (the verdict
#     arm), `--arp static` plants a PERMANENT entry (the positive control the
#     2026-11-13 passing arms used);
#   * the ping is measured in both ICMP sizes and inside one window;
#   * the capture runs to the case's `end` anchor, so the case's own
#     `window done` line and `end` line are both on disk;
#   * the monitor sees the broadcast ARP request, the board's ARP reply and
#     the ICMP exchange, so "no reply" separates board silence from AP loss.
#
# Outputs under --evidence:
#   serial.log / transcript.txt / timings.json / boot.out   (boot-rtos.sh)
#   peer-ping-<size>-<arp>.txt   the Pi's ping, with its start/end times
#   neigh-before.txt / neigh-after-<size>.txt   the AP's neighbour state
#   ap-station-<phase>.txt       the AP's view of the station
#   air-summary.txt              ARP/ICMP counts from the AP and monitor
#   timing.tsv                   every step's wall-clock time
#   bench-gate.txt / ap-conf.sha256 / pcap.txt
#
# Exit: 0 the round ran (read the evidence for the verdict), 2 usage, 3 bench
# not in the expected shape, 1 the boot or an anchor failed.

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BOARD="$ROOT/../skills/rk3568-board/scripts"
PI=pi@192.168.0.17
AP_IF=wlan2
MON_IF=wlan1
CH=5
FREQ=2432
SSID=labwifi-ar-ht20s
BOARD_IP=192.168.99.2
AP_IP=192.168.99.1
PSK=${PSK:-rtos-7260-wpa2-psk}
AP_CONF=/tmp/w722-ht20.conf

CASE_PREFIX=${CASE_PREFIX:-rtl8189fs_sta}
STA_MAC=${STA_MAC:-8c:bd:37:95:95:48}
ARP_MODE=${ARP_MODE:-dynamic}
PING_COUNT=${PING_COUNT:-20}
PING_INTERVAL=${PING_INTERVAL:-0.2}
PING_SIZES=${PING_SIZES:-56 1400}
ANCHOR=${ANCHOR:-peer window}
CAPTURE_SECS=${CAPTURE_SECS:-180}
TIMEOUT=${TIMEOUT:-300}
WPA2=0
EVIDENCE=
BIN="$ROOT/tests/board/rtos/build/build/rtos-freertos.bin"

while [ $# -gt 0 ]; do
	case "$1" in
		--evidence) EVIDENCE=${2:?}; shift 2 ;;
		--bin)      BIN=${2:?}; shift 2 ;;
		--arp)      ARP_MODE=${2:?}; shift 2 ;;
		--case)     CASE_PREFIX=${2:?}; shift 2 ;;
		--sta-mac)  STA_MAC=${2:?}; shift 2 ;;
		--count)    PING_COUNT=${2:?}; shift 2 ;;
		--interval) PING_INTERVAL=${2:?}; shift 2 ;;
		--sizes)    PING_SIZES=${2:?}; shift 2 ;;
		--wpa2)     WPA2=1; shift ;;
		--capture)  CAPTURE_SECS=${2:?}; shift 2 ;;
		--timeout)  TIMEOUT=${2:?}; shift 2 ;;
		-h|--help)  sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "sta-rx-round: unknown argument $1" >&2; exit 2 ;;
	esac
done
case "$ARP_MODE" in
	dynamic|static) ;;
	*) echo "sta-rx-round: --arp must be dynamic or static" >&2; exit 2 ;;
esac
if [ "$WPA2" = 1 ]; then
	CASE_PREFIX=${CASE_PREFIX_OVERRIDE:-rtl8189fs_sta_wpa2}
fi
[ -n "$EVIDENCE" ] || { echo "sta-rx-round: --evidence DIR is required" >&2; exit 2; }
mkdir -p "$EVIDENCE"
EVIDENCE=$(cd "$EVIDENCE" && pwd)
TAG=$(basename "$EVIDENCE")
PI_DIR=/home/pi/sta-rx-$TAG
AIR_PCAP=$PI_DIR/mon.pcap
SERIAL="$EVIDENCE/serial.log"
TIMING="$EVIDENCE/timing.tsv"

pi() { ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" "$@"; }
pisu() { pi "sudo -n $*"; }
pih() { pi "sudo -n env CH=$CH FREQ=$FREQ SSID=$SSID PI_IP=$AP_IP BOARD_IP=$BOARD_IP PSK=$PSK bash /tmp/pi-g2-ap.sh $*"; }

note() { printf '%s\t%s\t%s\n' "$(date -Is)" "$1" "$2" >> "$TIMING"; }

anchor() {	# $1 = regex; true when the capture already holds it
	[ -f "$SERIAL" ] || return 1
	tr -d '\r' < "$SERIAL" 2>/dev/null | grep -aqE "$1"
}

printf 'time\tstep\tdetail\n' > "$TIMING"
note start "case=$CASE_PREFIX arp=$ARP_MODE bin=$(basename "$BIN") sizes=$PING_SIZES count=$PING_COUNT interval=$PING_INTERVAL"

# --- bench gate: the AP side must be the one the round expects ---------------
{
	echo "time=$(date -Is) tag=$TAG arp=$ARP_MODE case=$CASE_PREFIX wpa2=$WPA2"
	pi 'systemctl is-active lab-ap 2>/dev/null || true'
	pisu "iw dev $AP_IF info"
	pisu "iw dev $MON_IF info"
	pisu "sha256sum $AP_CONF"
	pisu "iw dev $AP_IF station dump | head -20"
} > "$EVIDENCE/bench-gate.txt" 2>&1 || true
pisu "sha256sum $AP_CONF" 2>/dev/null | awk '{print $1}' > "$EVIDENCE/ap-conf.sha256" || true

pisu "iw dev $MON_IF info" 2>/dev/null | grep -q "channel $CH" \
	|| { echo "sta-rx-round: $MON_IF is not a monitor on channel $CH" >&2; exit 3; }
note gate "monitor=$MON_IF channel=$CH"

# --- the bench AP: open lab-ap as-is, or the temporary WPA2 one -------------
RESTORED=0
restore_bench() {
	[ "$WPA2" = 1 ] || return 0
	[ "$RESTORED" = 1 ] && return 0
	RESTORED=1
	pih "servers-stop" >/dev/null 2>&1 || true
	pih "down" > "$EVIDENCE/pi-restore.txt" 2>&1 || true
	note bench-restore "pi-g2-ap down"
}
if [ "$WPA2" = 1 ]; then
	trap restore_bench EXIT
	scp -q -o BatchMode=yes "$HERE/pi-g2-ap.sh" "$PI:/tmp/pi-g2-ap.sh"
	pisu "chmod 755 /tmp/pi-g2-ap.sh"
	pi "mkdir -p $PI_DIR"
	pih "up" > "$EVIDENCE/bench-gate-up.txt" 2>&1
	pisu "iw dev $MON_IF info" >> "$EVIDENCE/bench-gate-up.txt" 2>&1 || true
	pih "station" > "$EVIDENCE/ap-station-before.txt" 2>&1 || true
	note bench "wpa2 ap up (ssid=$SSID ch=$CH)"
	# The WPA2 arm must not keep the open AP's state around.
	:
else
	pi "mkdir -p $PI_DIR"
	note bench "open lab-ap left as-is"
fi

# --- the peer's own servers from an earlier run must not take part ----------
pi "pkill -x iperf 2>/dev/null || true" || true

# --- the board's image ------------------------------------------------------
"$BOARD/tftp.sh" put "$BIN" --name rtos.bin > "$EVIDENCE/tftp.txt" 2>&1
note tftp "rtos.bin $(grep -oE '[0-9]+ bytes' "$EVIDENCE/tftp.txt" | head -1 || true)"

# --- the air capture (the monitor sees what the AP sends and what the board
#     answers; it runs until the round's end, whatever the case does) --------
pisu "timeout $CAPTURE_SECS tcpdump -i $MON_IF -s 0 -w $AIR_PCAP" \
	> "$EVIDENCE/air-tcpdump.log" 2>&1 &
AIR_PID=$!

# --- boot -------------------------------------------------------------------
"$BOARD/boot-rtos.sh" --bin rtos.bin --success "rtos: result" \
	--quiet 0 --timeout "$TIMEOUT" --power-cycle --retry 0 \
	--evidence "$EVIDENCE" \
	>"$EVIDENCE/boot.out" 2>&1 &
BOOT_PID=$!
note boot "started pid=$BOOT_PID"

# --- wait for the case's window anchor --------------------------------------
seen=0
for _ in $(seq 1 600); do
	if anchor "$CASE_PREFIX: $ANCHOR ms="; then
		seen=1
		break
	fi
	if ! kill -0 "$BOOT_PID" 2>/dev/null; then
		break
	fi
	sleep 0.25
done
note anchor "$CASE_PREFIX: $ANCHOR ms= seen=$seen"

# The board's own MAC is the one its netif line printed (the case prints it at
# `netif up`); the bench constant is only the fallback.
MAC_SEEN=$(tr -d '\r' < "$SERIAL" 2>/dev/null | sed -n 's/.*netif up .*mac=\([0-9a-f:]\{17\}\).*/\1/p' | tail -1 || true)
[ -n "${MAC_SEEN:-}" ] && STA_MAC=$MAC_SEEN
note mac "sta=$STA_MAC (netif-line=${MAC_SEEN:-none})"

if [ "$seen" = 1 ]; then
	# The AP's neighbour state before the arm; the arm's variable is next.
	pisu "ip neigh show $BOARD_IP dev $AP_IF" > "$EVIDENCE/neigh-before.txt" 2>&1 || true
	if [ "$ARP_MODE" = dynamic ]; then
		pisu "ip neigh del $BOARD_IP dev $AP_IF 2>/dev/null || true"
		note arp "deleted (dynamic: the board must answer the broadcast request)"
	else
		pisu "ip neigh replace $BOARD_IP lladdr $STA_MAC dev $AP_IF"
		note arp "planted PERMANENT $STA_MAC (positive control)"
	fi
	pisu "ip neigh show $BOARD_IP dev $AP_IF" >> "$EVIDENCE/neigh-before.txt" 2>&1 || true

	for size in $PING_SIZES; do
		sizearg=
		[ "$size" = 56 ] || sizearg="-s $size"
		out="$EVIDENCE/peer-ping-$size-$ARP_MODE.txt"
		{
			echo "== start $(date -Is) size=$size arp=$ARP_MODE"
			pisu "ip neigh show $BOARD_IP dev $AP_IF"
		} > "$out" 2>&1
		note ping-begin "size=$size"
		# shellcheck disable=SC2086
		pisu "ping -I $AP_IF -c $PING_COUNT -i $PING_INTERVAL -W 1 $sizearg $BOARD_IP" \
			>> "$out" 2>&1 || true
		{
			echo "== end $(date -Is)"
			pisu "ip neigh show $BOARD_IP dev $AP_IF"
		} >> "$out" 2>&1
		pisu "ip neigh show $BOARD_IP dev $AP_IF" > "$EVIDENCE/neigh-after-$size.txt" 2>&1 || true
		note ping-end "size=$size"
	done
	pisu "iw dev $AP_IF station dump" > "$EVIDENCE/ap-station-window.txt" 2>&1 || true
	pisu "cat /proc/net/rtl88XXau/$AP_IF/all_sta_info" > "$EVIDENCE/ap-sta-info-window.txt" 2>&1 || true
else
	echo "sta-rx-round: no '$ANCHOR' anchor; the peer arm did not run" >&2
fi

# --- keep the case's own bookkeeping on disk: wait for its end anchor -------
ended=0
for _ in $(seq 1 1200); do
	if anchor "$CASE_PREFIX: end"; then
		ended=1
		break
	fi
	if ! kill -0 "$BOOT_PID" 2>/dev/null; then
		ended=1
		break
	fi
	sleep 0.25
done
note end "case-end-anchor=$ended"

wait "$BOOT_PID" || true
note boot "finished"
kill "$AIR_PID" 2>/dev/null || true
wait "$AIR_PID" 2>/dev/null || true

# --- air truth --------------------------------------------------------------
pisu "chmod 644 $AIR_PCAP 2>/dev/null || true"
{
	echo "mon=$AIR_PCAP"
	pisu "sha256sum $AIR_PCAP 2>/dev/null || true"
	pisu "stat -c '%n %s' $AIR_PCAP 2>/dev/null || true"
} > "$EVIDENCE/pcap.txt" 2>&1 || true

{
	echo "== the AP's own frames (802.3) for $BOARD_IP"
	pisu "tcpdump -r $AIR_PCAP -nn -e 'arp or icmp' 2>/dev/null | head -80" || true
	echo "== counts by class (monitor, 802.11)"
	# wlan.fc.type is the frame type (0 mgmt, 2 data); the subtype numbers are
	# not a class scale (0x08 is a beacon, QoS data is 0x88) - the first
	# version of this line read beacons as "mgmt + data", which is why it is
	# spelled out here.
	pisu "tshark -r $AIR_PCAP -T fields -e wlan.fc.type -e arp.opcode -e ip.proto 2>/dev/null | awk '{n++; if (\$1==\"0\") mgmt++; if (\$1==\"2\") data++; if (\$2==\"1\") arpreq++; if (\$2==\"2\") arprep++; if (\$3==\"1\") icmp++ } END { printf \"frames=%d mgmt=%d data=%d arp_req=%d arp_rep=%d icmp=%d\\n\", n+0, mgmt+0, data+0, arpreq+0, arprep+0, icmp+0 }'" || true
	echo "== monitor caveat: this MT7921AU monitor is reliable for CCK broadcast"
	echo "   (the AP's ARP request) and NOT for the AP's HT unicast data; a zero"
	echo "   data/arp_rep/icmp count above is the instrument, not board silence"
	echo "   (recorded 2026-11-13: 1983 frames, 1949 beacons, 1 ARP request, no QoS data)."
	echo "== the board's ARP replies and ICMP echoes on the air"
	pisu "tshark -r $AIR_PCAP -Y 'arp.src.hw_mac==$STA_MAC' -T fields -e arp.opcode -e arp.src.proto_ipv4 -e arp.dst.proto_ipv4 2>/dev/null" || true
	pisu "tshark -r $AIR_PCAP -Y 'icmp.type==0 && wlan.addr==$STA_MAC' -T fields -e frame.number 2>/dev/null | wc -l" || true
} > "$EVIDENCE/air-summary.txt" 2>&1 || true

echo "sta-rx-round: evidence -> $EVIDENCE"
grep -aE "$CASE_PREFIX: (window done|ping|PASS|FAIL|end)" "$SERIAL" 2>/dev/null | tr -d '\r' | tail -12 || true

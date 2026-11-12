#!/bin/bash
# sta-wpa2-round.sh - one G2 Pi-arm round: start the Pi's temporary WPA2 AP,
# boot the board as a station, prove the link, and drive the C6 phases from
# the case's own anchors.
#
#   tools/rtos/sta-wpa2-round.sh --evidence runs/<date>-iwl7260-sta-wpa2-pi/pi1
#
# The image is rk3568-iwl7260-sta-wpa2-pi (tick pcie iwl7260_sta_wpa2
# iwl7260_c6).  The Pi helper is pi-g2-ap.sh: it replaces lab-ap with a WPA2
# AP on the same SSID/ch5, keeps wlan1 as the monitor, and restores lab-ap in
# a trap even when the round fails.
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
BIN="$ROOT/tests/board/rtos/build/build/rtos-freertos.bin"
TIMEOUT=300
EVIDENCE=

while [ $# -gt 0 ]; do
	case "$1" in
		--evidence) EVIDENCE=${2:?}; shift 2 ;;
		--bin)      BIN=${2:?}; shift 2 ;;
		--timeout)  TIMEOUT=${2:?}; shift 2 ;;
		-h|--help)  sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "sta-wpa2-round: unknown argument $1" >&2; exit 2 ;;
	esac
done
[ -n "$EVIDENCE" ] || { echo "sta-wpa2-round: --evidence DIR is required" >&2; exit 2; }
mkdir -p "$EVIDENCE"
EVIDENCE=$(cd "$EVIDENCE" && pwd)
TAG=$(basename "$EVIDENCE")
PI_DIR=/home/pi/sta-wpa2-$TAG
AP_PCAP=$PI_DIR/ap.pcap
MON_PCAP=$PI_DIR/mon.pcap

pi() { ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" "$@"; }
pisu() { pi "sudo -n $*"; }
pih() { pi "sudo -n env CH=$CH FREQ=$FREQ SSID=$SSID PI_IP=$AP_IP BOARD_IP=$BOARD_IP PSK=$PSK bash /tmp/pi-g2-ap.sh $*"; }

RESTORED=0
restore_pi() {
	[ "$RESTORED" = 1 ] && return 0
	RESTORED=1
	pih "servers-stop" >/dev/null 2>&1 || true
	pih "down" > "$EVIDENCE/pi-restore.txt" 2>&1 || true
}
trap restore_pi EXIT

scp -q -o BatchMode=yes "$HERE/pi-g2-ap.sh" "$PI:/tmp/pi-g2-ap.sh"
scp -q -o BatchMode=yes "$HERE/udp-sink.py" "$PI:/tmp/udp-sink-$TAG.py"
pisu "chmod 755 /tmp/pi-g2-ap.sh"
pi "mkdir -p $PI_DIR"

{
	echo "tag=$TAG time=$(date -Is) psk_len=${#PSK}"
	pi 'systemctl is-active lab-ap 2>/dev/null || true'
	pih status
	pi 'iperf -v 2>&1 | head -1'
} > "$EVIDENCE/bench-gate-before.txt" 2>&1 || true

pih "up" > "$EVIDENCE/bench-gate-up.txt" 2>&1
pisu "iw dev $MON_IF info" >> "$EVIDENCE/bench-gate-up.txt" 2>&1 || true
pih "station" > "$EVIDENCE/ap-station-before.txt" 2>&1 || true

"$BOARD/tftp.sh" put "$BIN" --name rtos.bin > "$EVIDENCE/tftp.txt" 2>&1

# Pi-side receivers.  The TCP server owns 5001 for the uplink phase; the raw
# UDP sink is started at tcpup so its first datagram is not lost.
pi "nohup iperf -s -p 5001 -i 5 > $PI_DIR/peer-tcp-server.log 2>&1 & echo \$! > $PI_DIR/peer-tcp.pid"
sleep 1

pisu "timeout 180 tcpdump -i $AP_IF -s 128 -w $AP_PCAP" > "$EVIDENCE/ap-tcpdump.log" 2>&1 &
APCAP_PID=$!
pisu "timeout 180 tcpdump -i $MON_IF -s 0 -w $MON_PCAP" > "$EVIDENCE/mon-tcpdump.log" 2>&1 &
MONCAP_PID=$!

"$BOARD/boot-rtos.sh" --bin rtos.bin --success "rtos: result" \
	--quiet 0 --timeout "$TIMEOUT" --power-cycle --retry 0 \
	--evidence "$EVIDENCE" >"$EVIDENCE/boot.out" 2>&1 &
BOOT_PID=$!

SERIAL="$EVIDENCE/serial.log"
seen_netif=0
seen_ping=0
seen_tcpdn=0
seen_udpdn=0
seen_tcpdn2=0
seen_udpdn2=0
seen_sink=0
sink_pid=

anchor() { [ -f "$SERIAL" ] && tr -d '\r' < "$SERIAL" | grep -aqE "$1"; }
peer_ping() {
	pisu "ip neigh del $BOARD_IP dev $AP_IF 2>/dev/null || true"
	pisu "ping -I $AP_IF -c 20 -i 0.1 -W 1 $BOARD_IP" > "$EVIDENCE/peer-ping.txt" 2>&1 || true
	pisu "ip neigh show $BOARD_IP dev $AP_IF" >> "$EVIDENCE/peer-ping.txt" 2>&1 || true
}
peer_tcp() { pih "client-tcp 28" > "$EVIDENCE/peer-$1.txt" 2>&1 || true; }
peer_udp() { pih "client-udp 18" > "$EVIDENCE/peer-$1.txt" 2>&1 || true; }

for _ in $(seq 1 3000); do
	if [ "$seen_netif" = 0 ] && anchor 'iwl7260_sta_wpa2: netif up '; then
		seen_netif=1
	fi
	if [ "$seen_ping" = 0 ] && anchor 'iwl7260_sta_wpa2: peer window ms='; then
		seen_ping=1
		peer_ping
		pih "station" > "$EVIDENCE/ap-station-connected.txt" 2>&1 || true
	fi
	if [ "$seen_tcpdn" = 0 ] && anchor 'name=tcpdn$'; then seen_tcpdn=1; peer_tcp tcpdn; fi
	if [ "$seen_udpdn" = 0 ] && anchor 'name=udpdn$'; then seen_udpdn=1; peer_udp udpdn; fi
	if [ "$seen_tcpdn2" = 0 ] && anchor 'name=tcpdn2$'; then seen_tcpdn2=1; peer_tcp tcpdn2; fi
	if [ "$seen_udpdn2" = 0 ] && anchor 'name=udpdn2$'; then seen_udpdn2=1; peer_udp udpdn2; fi
	if [ "$seen_sink" = 0 ] && anchor 'name=tcpup$'; then
		seen_sink=1
		pi "python3 /tmp/udp-sink-$TAG.py --port 5002 --seconds 40" > "$EVIDENCE/udp-sink.txt" 2>&1 &
		sink_pid=$!
	fi
	if ! kill -0 "$BOOT_PID" 2>/dev/null; then break; fi
	sleep 0.3
done

wait "$BOOT_PID" || true
sleep 2
pi "kill \$(cat $PI_DIR/peer-tcp.pid) 2>/dev/null || true" || true
[ -n "${sink_pid:-}" ] && wait "$sink_pid" 2>/dev/null || true
wait "$APCAP_PID" 2>/dev/null || true
wait "$MONCAP_PID" 2>/dev/null || true

pih "log" > "$EVIDENCE/ap-hostapd.log" 2>&1 || true
pih "station" > "$EVIDENCE/ap-station-after.txt" 2>&1 || true
pisu "chmod 644 $AP_PCAP $MON_PCAP 2>/dev/null || true"
{
	echo "ap=$AP_PCAP mon=$MON_PCAP"
	pisu "sha256sum $AP_PCAP $MON_PCAP 2>/dev/null || true"
	pisu "stat -c '%n %s' $AP_PCAP $MON_PCAP 2>/dev/null || true"
} > "$EVIDENCE/pcap.txt" 2>&1 || true
{
	echo "== EAPOL frames on monitor"
	pisu "tshark -r $MON_PCAP -Y 'eapol' -T fields -e frame.time_relative -e wlan.sa -e wlan.da -e eapol.type -e eapol.keydes.key_info 2>/dev/null" || true
	echo "== count"
	pisu "tshark -r $MON_PCAP -Y 'eapol' -T fields -e frame.number 2>/dev/null | wc -l" || true
} > "$EVIDENCE/air-eapol.txt" 2>&1 || true

echo "sta-wpa2-round: evidence -> $EVIDENCE"
tail -3 "$EVIDENCE/serial.log" 2>/dev/null | tr -d '\r' || true

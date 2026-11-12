#!/bin/bash
# qiqijia-round.sh - G2 QiQiJia arm.  The board joins the external WPA2 AP with
# DHCP (192.168.0.201 in the observed lab) and the Pi reaches it over eth0
# (192.168.0.17).  Only the four downlink C6 phases are driven; the uplink
# phases need a peer listener on the QiQiJia LAN and are out of this round.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BOARD="$ROOT/../skills/rk3568-board/scripts"
PI=pi@192.168.0.17
BOARD_IP=${BOARD_IP:-192.168.0.201}
BIN="$ROOT/tests/board/rtos/build/build/rtos-freertos.bin"
EVIDENCE=
TIMEOUT=150
while [ $# -gt 0 ]; do
	case "$1" in
		--evidence) EVIDENCE=${2:?}; shift 2 ;;
		--bin) BIN=${2:?}; shift 2 ;;
		--board-ip) BOARD_IP=${2:?}; shift 2 ;;
		--timeout) TIMEOUT=${2:?}; shift 2 ;;
		*) echo "qiqijia-round: unknown $1" >&2; exit 2 ;;
	esac
done
[ -n "$EVIDENCE" ] || { echo "qiqijia-round: --evidence DIR required" >&2; exit 2; }
mkdir -p "$EVIDENCE"; EVIDENCE=$(cd "$EVIDENCE" && pwd)
pi() { ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" "$@"; }
pi "pkill -x iperf 2>/dev/null || true" || true
"$BOARD/tftp.sh" put "$BIN" --name rtos.bin > "$EVIDENCE/tftp.txt" 2>&1
pi "ping -c 3 -W 1 $BOARD_IP" > "$EVIDENCE/peer-pre-ping.txt" 2>&1 || true

"$BOARD/boot-rtos.sh" --bin rtos.bin --success "rtos: result" --quiet 0 \
	--timeout "$TIMEOUT" --power-cycle --retry 0 --evidence "$EVIDENCE" \
	> "$EVIDENCE/boot.out" 2>&1 &
BOOT_PID=$!
SERIAL="$EVIDENCE/serial.log"
seen_tcpdn=0; seen_udpdn=0; seen_tcpdn2=0; seen_udpdn2=0
anchor() { [ -f "$SERIAL" ] && tr -d '\r' < "$SERIAL" | grep -aqE "$1"; }
peer_tcp() { pi "iperf -c $BOARD_IP -p 5001 -t 28 -i 5" > "$EVIDENCE/peer-$1.txt" 2>&1 || true; }
peer_udp() { pi "iperf -c $BOARD_IP -p 5002 -u -b 40M -t 18 -i 5" > "$EVIDENCE/peer-$1.txt" 2>&1 || true; }
for _ in $(seq 1 2000); do
	if [ "$seen_tcpdn" = 0 ] && anchor 'name=tcpdn$'; then seen_tcpdn=1; peer_tcp tcpdn; fi
	if [ "$seen_udpdn" = 0 ] && anchor 'name=udpdn$'; then seen_udpdn=1; peer_udp udpdn; fi
	if [ "$seen_tcpdn2" = 0 ] && anchor 'name=tcpdn2$'; then seen_tcpdn2=1; peer_tcp tcpdn2; fi
	if [ "$seen_udpdn2" = 0 ] && anchor 'name=udpdn2$'; then seen_udpdn2=1; peer_udp udpdn2; fi
	kill -0 "$BOOT_PID" 2>/dev/null || break
	sleep 0.3
done
wait "$BOOT_PID" || true
echo "qiqijia-round: evidence -> $EVIDENCE"

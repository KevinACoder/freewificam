#!/bin/sh
# f429-eth-tput-round.sh - one iperf round against the F429's wired NIC.
#
# Usage:
#   tools/rtos/f429-eth-tput-round.sh runs/<date>-<topic>/<boot>
#
# The board runs the f429_eth case's throughput arm: four phases, each
# announced by a `tput <phase> ready` line.  The peer is the Pi's iperf 2.2.1
# (the same binary lwIP's lwiperf speaks to).  The two uplink phases have the
# peer as the server, so both servers start before the boot; the two downlink
# phases are driven from the board's anchors as they appear in the capture.
#
# Outputs: build.log, build-info.txt, image.sha256, size.txt, flash.log,
# serial.log/serial.txt, peer-tcpdn.txt, peer-udpdn.txt,
# peer-tcp-server.log, peer-udp-server.log, judge.json, judge.txt.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT=${1:?usage: $0 <output-dir>}
PROFILE=${PROFILE:-stm32f429-eth-tput}
PI=${PI_HOST:-pi@192.168.0.17}
SERIAL_TTY=${SERIAL_TTY:-/dev/ttyUSB0}
CAPTURE_SECS=${CAPTURE_SECS:-180}
PEER_IP=${PEER_IP:-192.168.0.17}
UDP_DN_RATE=${UDP_DN_RATE:-90M}
TCP_SECS=${TCP_SECS:-10}
UDP_SECS=${UDP_SECS:-10}
ELF="$ROOT/tests/board/rtos/build/build/rtos-freertos.elf"
BIN="$ROOT/tests/board/rtos/build/build/rtos-freertos.bin"

mkdir -p "$OUT"

pi() { ssh "$PI" "$@"; }

echo "== build $PROFILE =="
( cd "$ROOT" && ./make.rtos image PROFILE="$PROFILE" ) > "$OUT/build.log" 2>&1
cp "$ROOT/tests/board/rtos/build/build/build-info-freertos.txt" \
	"$OUT/build-info.txt" 2>/dev/null || true
sha256sum "$ELF" "$BIN" > "$OUT/image.sha256"
( cd "$ROOT" && arm-none-eabi-size "$ELF" 2>/dev/null || true ) \
	> "$OUT/size.txt"
cp "$BIN" "$OUT/rtos-tput.bin"

echo "== peer uplink servers (they must listen before the board dials) =="
pi "pkill -x iperf 2>/dev/null || true"
pi "nohup iperf -s -p 5001 -i 5 > \$HOME/f429cam/peer-tcp-server.log 2>&1 & \
    echo \$! > \$HOME/f429cam/peer-tcp.pid"
# The UDP uplink is a raw paced blast with no iperf2 header, so the peer's
# receiver is the counting sink, not `iperf -s -u` (whose report has no test
# framing to work from).  It is deployed here and started at the UDP-down
# anchor: the down window (12 s) leaves it bound before the blast starts, and
# its own window then covers the blast without the boot's dead time.
scp -q "$ROOT/tools/rtos/udp-sink.py" "$PI:/tmp/f429-udp-sink.py"

echo "== Pi serial capture -> $OUT/serial.log =="
pi "stty -F $SERIAL_TTY 115200 raw -echo; timeout $CAPTURE_SECS cat $SERIAL_TTY" \
	> "$OUT/serial.log" 2>&1 &
CAP_PID=$!
sleep 1

echo "== power on, load, relay cold boot =="
{ date -Is; echo "== power on"; } > "$OUT/flash.log"
bash "$ROOT/../skills/rk3568-board/scripts/power.sh" on --confirm \
	>> "$OUT/flash.log" 2>&1 || true
sleep 3
{ date -Is; echo "== load + cycle"; } >> "$OUT/flash.log"
rc=0
bash "$ROOT/../skills/stm32f429-board/scripts/pi-flash.sh" "$BIN" --cycle \
	>> "$OUT/flash.log" 2>&1 || rc=$?
date -Is >> "$OUT/flash.log"

# --- drive the peer from the board's phase anchors --------------------------
seen_dn=0
seen_udp_dn=0
seen_result=0
board_ip=""
tcp_pid=""
deadline=$(( $(date +%s) + CAPTURE_SECS ))

while [ "$(date +%s)" -lt "$deadline" ]; do
	if [ -z "$board_ip" ] && grep -aq "f429_eth: dhcp ip=" "$OUT/serial.log" 2>/dev/null; then
		board_ip=$(tr -d '\r' < "$OUT/serial.log" | \
			sed -n 's/.*f429_eth: dhcp ip=\([0-9.][0-9.]*\).*/\1/p' | head -1)
		echo "board ip: $board_ip"
	fi
	if [ "$seen_dn" = 0 ] && tr -d '\r' < "$OUT/serial.log" 2>/dev/null | \
		grep -aq "f429_eth: tput tcpdn ready"; then
		seen_dn=1
		if [ -z "$board_ip" ]; then
			echo "anchor tcpdn but no DHCP address yet" >&2
		else
			echo "== peer -> board TCP ($TCP_SECS s)"
			pi "iperf -c $board_ip -p 5001 -t $TCP_SECS -i 5" \
				> "$OUT/peer-tcpdn.txt" 2>&1 || true
		fi
	fi
	if [ "$seen_udp_dn" = 0 ] && tr -d '\r' < "$OUT/serial.log" 2>/dev/null | \
		grep -aq "f429_eth: tput udpdn ready"; then
		seen_udp_dn=1
		pi "rm -f \$HOME/f429cam/udp-sink.ready \$HOME/f429cam/peer-udp-sink.json"
		pi "nohup python3 /tmp/f429-udp-sink.py --port 5002 --seconds 26 \
		    --ready \$HOME/f429cam/udp-sink.ready \
		    --out \$HOME/f429cam/peer-udp-sink.json \
		    > \$HOME/f429cam/peer-udp-sink.log 2>&1 & \
		    echo \$! > \$HOME/f429cam/peer-udp.pid"
		for _ in $(seq 1 50); do
			pi "test -f \$HOME/f429cam/udp-sink.ready" 2>/dev/null && break
			sleep 0.1
		done
		if [ -n "$board_ip" ]; then
			echo "== peer -> board UDP ($UDP_SECS s at $UDP_DN_RATE)"
			pi "iperf -c $board_ip -p 5002 -u -b $UDP_DN_RATE -t $UDP_SECS -i 5" \
				> "$OUT/peer-udpdn.txt" 2>&1 || true
		fi
	fi
	if [ "$seen_result" = 0 ] && tr -d '\r' < "$OUT/serial.log" 2>/dev/null | \
		grep -aq "rtos: result"; then
		seen_result=1
		break
	fi
	sleep 0.3
done

# The sink writes its JSON at the end of its own window (26 s from the
# udpdn anchor, ~4 s after the blast).  Wait for it before killing anything;
# killing early loses the receiver's own count.
for _ in $(seq 1 60); do
	pi "test -f \$HOME/f429cam/peer-udp-sink.json" 2>/dev/null && break
	sleep 0.5
done
kill "$CAP_PID" 2>/dev/null || true
pi "pkill -f 'cat /dev/ttyUSB[0-9]' 2>/dev/null || true"
wait "$CAP_PID" 2>/dev/null || true
pi "kill \$(cat \$HOME/f429cam/peer-tcp.pid) 2>/dev/null || true; \
    kill \$(cat \$HOME/f429cam/peer-udp.pid) 2>/dev/null || true; \
    pkill -x iperf 2>/dev/null || true"
pi "cat \$HOME/f429cam/peer-tcp-server.log" > "$OUT/peer-tcp-server.log" 2>&1 || true
pi "cat \$HOME/f429cam/peer-udp-sink.log" > "$OUT/peer-udp-sink.log" 2>&1 || true
scp -q "$PI:/home/pi/f429cam/peer-udp-sink.json" "$OUT/peer-udp-sink.json" 2>/dev/null || true

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
tail -25 "$OUT/judge.txt" || true

echo "== peer reports =="
grep -E "sender|receiver|Mbits/sec|bits/sec|%|connected|bytes" \
	"$OUT/peer-tcpdn.txt" "$OUT/peer-udpdn.txt" 2>/dev/null | tail -12 || true
[ -f "$OUT/peer-udp-sink.json" ] && cat "$OUT/peer-udp-sink.json" || true
grep -E "t=|bytes|pkts" "$OUT/peer-udp-sink.log" 2>/dev/null | tail -2 || true

echo "== image =="
cat "$OUT/image.sha256"
[ "$rc" -eq 0 ] || echo "flash returned $rc (see $OUT/flash.log)" >&2
exit "$jrc"

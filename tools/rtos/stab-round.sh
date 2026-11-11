#!/bin/bash
# stab-round.sh - one C6 stability round: a 30-minute TCP downlink plus ten
# disconnect/connect cycles, driven from the image's own anchors.
#
#   tools/rtos/stab-round.sh --evidence runs/<date>-iwl7260-stab/stab1
#
# The image is `rk3568-iwl7260-stab` (tick pcie iwl7260_c5 iwl7260_stab):
# C5 joins the open AP in the same boot, the stability case serves one long
# lwiperf session and then runs the reconnect loop.  The peer is the Pi's
# iperf 2.2.1 for the long session; the reconnect's traffic is the case's own
# ICMP.
#
# What is captured, and what is deliberately not:
#   * the serial log, whole boot (the health lines are 30 s apart);
#   * `iw dev wlan2 station dump` every 60 s - the AP's continuity witness;
#   * two 60 s AP-side pcaps (start and end of the long run) - a 30-minute
#     dump at 40 Mbit/s would be ~9 GB and buys nothing a sample does not;
#   * the peer's iperf output and the hostapd STA connect/disconnect lines.
#
# Outputs: serial.log, peer-long.txt, ap-station-samples.txt,
# ap-long-0.pcap / ap-long-1.pcap (on the Pi), air-summary.txt, judge.json.

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BOARD="$ROOT/../skills/rk3568-board/scripts"
PI=pi@192.168.0.17
AP_IF=wlan2
MON_IF=wlan1
BOARD_IP=192.168.99.2
STA_MAC=6c:29:95:12:f0:d9
AP_CONF=/tmp/w722-ht20.conf
LONG_SECONDS=${LONG_SECONDS:-1780}

EVIDENCE=
BIN="$ROOT/tests/board/rtos/build/build/rtos-freertos.bin"
TIMEOUT=2400

while [ $# -gt 0 ]; do
	case "$1" in
		--evidence) EVIDENCE=${2:?}; shift 2 ;;
		--bin)      BIN=${2:?}; shift 2 ;;
		--timeout)  TIMEOUT=${2:?}; shift 2 ;;
		-h|--help)  sed -n '2,26p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "stab-round: unknown argument $1" >&2; exit 2 ;;
	esac
done
[ -n "$EVIDENCE" ] || { echo "stab-round: --evidence DIR is required" >&2; exit 2; }
mkdir -p "$EVIDENCE"
EVIDENCE=$(cd "$EVIDENCE" && pwd)
TAG=$(basename "$EVIDENCE")
PI_DIR=/home/pi/stab-$TAG

pi() { ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" "$@"; }
pisu() { pi "sudo -n $*"; }

{
	echo "time=$(date -Is)"
	pi 'systemctl is-active lab-ap'
	pisu "iw dev $AP_IF info"
	pisu "iw dev $MON_IF info"
	pisu "sha256sum $AP_CONF"
	pi 'iperf -v 2>&1 | head -1'
	pisu "iw dev $AP_IF station dump" | head -20
} > "$EVIDENCE/ap-gate.txt" 2>&1 || true

pisu "iw dev $MON_IF info" 2>/dev/null | grep -q 'channel 5' \
	|| { echo "stab-round: $MON_IF is not a monitor on channel 5" >&2; exit 3; }

pi "pkill -x iperf 2>/dev/null || true" || true
pi "mkdir -p $PI_DIR"
"$BOARD/tftp.sh" put "$BIN" --name rtos.bin > "$EVIDENCE/tftp.txt" 2>&1

"$BOARD/boot-rtos.sh" --bin rtos.bin --success "rtos: result" \
	--quiet 0 --timeout "$TIMEOUT" --power-cycle --retry 1 \
	--evidence "$EVIDENCE" >"$EVIDENCE/boot.out" 2>&1 &
BOOT_PID=$!

SERIAL="$EVIDENCE/serial.log"
seen_neigh=0
seen_long=0
seen_report=0
peer_pid=
sampler_pid=

anchor() {
	[ -f "$SERIAL" ] || return 1
	tr -d '\r' < "$SERIAL" 2>/dev/null | grep -aqE "$1"
}

sampler() {
	while [ -d "$EVIDENCE" ]; do
		pisu "iw dev $AP_IF station dump" \
			>> "$EVIDENCE/ap-station-samples.txt" 2>&1 || true
		echo "=== $(date +%s)" >> "$EVIDENCE/ap-station-samples.txt"
		sleep 60
	done
}

for _ in $(seq 1 6000); do
	if anchor 'iwl7260_c5: netif up ' && [ "$seen_neigh" = 0 ]; then
		seen_neigh=1
		pisu "ip neigh replace $BOARD_IP lladdr $STA_MAC dev $AP_IF" \
			>"$EVIDENCE/peer-neigh.txt" 2>&1 || true
	fi
	if anchor 'iwl7260_stab: long begin ' && [ "$seen_long" = 0 ]; then
		seen_long=1
		sampler &
		sampler_pid=$!
		# the board's server is listening; the peer sets the pace
		pi "iperf -c $BOARD_IP -p 5001 -t $LONG_SECONDS -i 30" \
			>"$EVIDENCE/peer-long.txt" 2>&1 &
		peer_pid=$!
		# two 60 s air samples, at the start and at the end of the run
		(
			pisu "timeout 60 tcpdump -i $AP_IF -s 128 -w $PI_DIR/ap-long-0.pcap" \
				>"$EVIDENCE/ap-long-0-tcpdump.log" 2>&1
			sleep $((LONG_SECONDS - 120))
			pisu "timeout 60 tcpdump -i $AP_IF -s 128 -w $PI_DIR/ap-long-1.pcap" \
				>"$EVIDENCE/ap-long-1-tcpdump.log" 2>&1
		) &
		# The dynamic-ARP gate: at 60 s into the long run, delete the static
		# neighbour and let the AP resolve the board by broadcast.  The
		# allmulti/configure-filter path under test is the one the stability
		# case programs; a static entry would hide it.  The entry is restored
		# afterwards so the rest of the long run keeps its downlink.
		(
			sleep 60
			{
				echo "== before"
				pisu "ip neigh show $BOARD_IP"
				pisu "ip neigh del $BOARD_IP dev $AP_IF" || true
				pi "ping -c 3 -W 1 $BOARD_IP" || true
				echo "== after"
				pisu "ip neigh show $BOARD_IP"
			} > "$EVIDENCE/arp-gate.txt" 2>&1
			pisu "ip neigh replace $BOARD_IP lladdr $STA_MAC dev $AP_IF" \
				>> "$EVIDENCE/arp-gate.txt" 2>&1 || true
		) &
	fi
	if anchor 'iwl7260_stab: long report ' && [ "$seen_report" = 0 ]; then
		seen_report=1
	fi
	if ! kill -0 "$BOOT_PID" 2>/dev/null; then
		break
	fi
	sleep 0.5
done

wait "$BOOT_PID" || true
[ -n "${peer_pid:-}" ] && wait "$peer_pid" 2>/dev/null || true
[ -n "${sampler_pid:-}" ] && kill "$sampler_pid" 2>/dev/null || true
pisu "kill \$(pgrep -x iperf) 2>/dev/null || true" || true
sleep 2

pisu "tail -200 /tmp/KO1-ap.log" > "$EVIDENCE/ap-hostapd-tail.txt" 2>&1 || true
pisu "iw dev $AP_IF station dump" > "$EVIDENCE/ap-station-end.txt" 2>&1 || true

pisu "chmod 644 $PI_DIR/ap-long-0.pcap $PI_DIR/ap-long-1.pcap 2>/dev/null || true"
{
	echo "ap0=$PI_DIR/ap-long-0.pcap ap1=$PI_DIR/ap-long-1.pcap"
	pisu "sha256sum $PI_DIR/ap-long-0.pcap $PI_DIR/ap-long-1.pcap 2>/dev/null || true"
	pisu "stat -c '%n %s' $PI_DIR/ap-long-0.pcap $PI_DIR/ap-long-1.pcap 2>/dev/null || true"
	echo "== first window"
	pisu "tshark -r $PI_DIR/ap-long-0.pcap -Y 'ip.addr==$BOARD_IP' -T fields -e frame.len 2>/dev/null | awk '{n++; s+=\$1} END {printf \"frames=%d bytes=%d\\n\", n, s}'" || true
	echo "== last window"
	pisu "tshark -r $PI_DIR/ap-long-1.pcap -Y 'ip.addr==$BOARD_IP' -T fields -e frame.len 2>/dev/null | awk '{n++; s+=\$1} END {printf \"frames=%d bytes=%d\\n\", n, s}'" || true
} > "$EVIDENCE/air-summary.txt" 2>&1 || true

# station continuity: count the samples that carry the station and the gaps
{
	echo "samples=$(grep -c '^===' "$EVIDENCE/ap-station-samples.txt" 2>/dev/null || echo 0)"
	echo "with_station=$(grep -c "^Station $STA_MAC" "$EVIDENCE/ap-station-samples.txt" 2>/dev/null || echo 0)"
} > "$EVIDENCE/continuity.txt" 2>&1 || true

python3 "$HERE/judge.py" "$SERIAL" --json "$EVIDENCE/judge.json" \
	--expect cases=tick,pcie,iwl7260_c5,iwl7260_stab \
	>"$EVIDENCE/judge.txt" 2>&1 || true

echo "stab-round: evidence -> $EVIDENCE"
tail -6 "$EVIDENCE/judge.txt" 2>/dev/null || true
cat "$EVIDENCE/continuity.txt" 2>/dev/null || true

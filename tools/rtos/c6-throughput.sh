#!/bin/bash
# c6-throughput.sh - one C6T board round: boot the image, drive the peer's
# iperf2 sessions from the case's own phase anchors, and collect the evidence.
#
#   tools/rtos/c6-throughput.sh --evidence runs/<date>-iwl7260-c6-throughput/rtos1
#
# The image is `rk3568-iwl7260-c6` (tick pcie iwl7260_c5 iwl7260_c6): C5
# connects to the open bench AP and assembles wl0, C6T runs the seven phases.
# The peer side is the Kali Pi's iperf 2.2.1 (the same binary the GT arm uses);
# the board's TCP server/client is lwIP's lwiperf, which is iperf2.
#
# Captures: the AP interface (wlan2, 802.3 view) for the whole round, and the
# monitor (wlan1, 802.11) in two ~55 s windows - the downlink pair and the
# uplink pair - because the MTK monitor cannot see every A-MPDU member and a
# whole-round capture would only multiply the uncertainty.  Both live on the
# Pi's disk-backed /home/pi (the /tmp tmpfs is 3.9 GB and truncates silently).
#
# Outputs under --evidence:
#   serial.log / transcript.txt / timings.json   (boot-rtos.sh)
#   peer-tcpdn.txt / peer-tcpdn2.txt / peer-udpdn.txt / peer-udpdn2.txt
#   peer-reverse-ping-tcpdn.txt                   (REVERSE_PING=1 only)
#   peer-tcp-server.log                           (the TCP uplink receiver)
#   udp-sink.txt                                  (the raw-blast counting sink)
#   ap-station-<phase>.txt                       (AP's view at each phase)
#   ap-gate.txt / ap-conf.sha256
#   ap-summary.txt / air-summary.txt / air-fields.csv
#   judge.json
# The pcaps stay on the Pi in /home/pi/c6-<tag>/; their hashes and paths land
# in pcap.txt.

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BOARD="$ROOT/../skills/rk3568-board/scripts"
PI=pi@192.168.0.17
AP_IF=wlan2
MON_IF=wlan1
BOARD_IP=192.168.99.2
# The station under test and the case that brings it up.  The defaults are the
# AC 7260 C5/C6 pair; the 8189FTV throughput arm overrides both (its station
# case prints the same `peer window` / `netif up` anchors).
STA_MAC=${STA_MAC:-6c:29:95:12:f0:d9}
STA_CASE_PREFIX=${STA_CASE_PREFIX:-iwl7260_c5}
CASE_EXPECT=${CASE_EXPECT:-tick,pcie,iwl7260_c5,iwl7260_c6}
# The downlink UDP rate the counting sink is offered.  40M is the capacity
# shape; the 8189FTV arm offers 35M, the GT ladder's top rung.
UDP_DN_RATE=${UDP_DN_RATE:-40M}
# The reverse arm (docs/HANDOFF-8189FTV-STA-RX.md §4.1): with the downlink
# already proven in phase tcpdn, drop the static neighbour entry this runner
# plants at `netif up`, let the AP resolve the board by broadcast, and ping it.
# On: the arm says whether dynamic ARP works while the link is demonstrably
# carrying the downlink (and whether a failure is ARP-specific, since the
# phase's own byte count keeps coming from the adapter).
REVERSE_PING=${REVERSE_PING:-0}
REVERSE_PING_COUNT=${REVERSE_PING_COUNT:-20}
REVERSE_PING_INTERVAL=${REVERSE_PING_INTERVAL:-0.2}
# Wall-clock cap on the AP-side capture.  The phases finish well inside 200 s;
# a shorter cap only trims the post-run wait, never the measured window.
AP_CAP_SECS=${AP_CAP_SECS:-360}
AP_CONF=/tmp/w722-ht20.conf

EVIDENCE=
BIN="$ROOT/tests/board/rtos/build/build/rtos-freertos.bin"
TIMEOUT=420

while [ $# -gt 0 ]; do
	case "$1" in
		--evidence) EVIDENCE=${2:?}; shift 2 ;;
		--bin)      BIN=${2:?}; shift 2 ;;
		--timeout)  TIMEOUT=${2:?}; shift 2 ;;
		-h|--help)  sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "c6-throughput: unknown argument $1" >&2; exit 2 ;;
	esac
done
[ -n "$EVIDENCE" ] || { echo "c6-throughput: --evidence DIR is required" >&2; exit 2; }
mkdir -p "$EVIDENCE"
EVIDENCE=$(cd "$EVIDENCE" && pwd)
TAG=$(basename "$EVIDENCE")
PI_DIR=/home/pi/c6-$TAG
AP_PCAP=$PI_DIR/ap.pcap
MON_DOWN_PCAP=$PI_DIR/mon-down.pcap
MON_UP_PCAP=$PI_DIR/mon-up.pcap

pi() { ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" "$@"; }
pisu() { pi "sudo -n $*"; }

# --- gate: the bench must be the one the pre-registration names -------------
{
	echo "time=$(date -Is)"
	pi 'systemctl is-active lab-ap'
	pisu "iw dev $AP_IF info"
	pisu "iw dev $MON_IF info"
	pisu "sha256sum $AP_CONF"
	pi 'iperf -v 2>&1 | head -1'
	pisu "iw dev $AP_IF station dump | head -20"
} > "$EVIDENCE/ap-gate.txt" 2>&1 || true
pisu "sha256sum $AP_CONF" 2>/dev/null | awk '{print $1}' > "$EVIDENCE/ap-conf.sha256" || true

pisu "iw dev $MON_IF info" 2>/dev/null | grep -q 'channel 5' \
	|| { echo "c6-throughput: $MON_IF is not a monitor on channel 5" >&2; exit 3; }

# --- clean the peer's own servers from an earlier round ----------------------
pi "pkill -x iperf 2>/dev/null || true" || true
pi "mkdir -p $PI_DIR"

# --- the board's image ------------------------------------------------------
"$BOARD/tftp.sh" put "$BIN" --name rtos.bin > "$EVIDENCE/tftp.txt" 2>&1

# --- peer listeners (the uplink phases' receivers) --------------------------
# A server or client left by a killed run silently takes part in the next one
# (T7b's uplink read 96 kbit because a stale client was still sending).
pi "pkill -x iperf 2>/dev/null || true; sleep 0.5; nohup iperf -s -p 5001 -i 5 > $PI_DIR/peer-tcp-server.log 2>&1 & echo \$! > $PI_DIR/peer-tcp.pid"
scp -q -o BatchMode=yes "$HERE/udp-sink.py" "$PI:/tmp/udp-sink-$TAG.py"
sleep 1

# --- captures: AP whole round, monitor down/up windows ----------------------
pisu "timeout $AP_CAP_SECS tcpdump -i $AP_IF -s 128 -w $AP_PCAP" \
	> "$EVIDENCE/ap-tcpdump.log" 2>&1 &
APCAP_PID=$!

# --- boot -------------------------------------------------------------------
"$BOARD/boot-rtos.sh" --bin rtos.bin --success "rtos: result" \
	--quiet 0 --timeout "$TIMEOUT" --power-cycle --retry 1 \
	--evidence "$EVIDENCE" \
	>"$EVIDENCE/boot.out" 2>&1 &
BOOT_PID=$!

SERIAL="$EVIDENCE/serial.log"
seen_neigh=0
seen_mondown=0
seen_monup=0
seen_sink=0
sink_pid=
seen_tcpdn=0
seen_tcpdn2=0
seen_udpdn=0
seen_udpdn2=0
mon_down_pid=
mon_up_pid=

peer_tcp() {	# $1 = phase name, $2 = seconds
	local name=$1 secs=$2
	pi "iperf -c $BOARD_IP -p 5001 -t $secs -i 5" \
		>"$EVIDENCE/peer-$name.txt" 2>&1 || true
}
reverse_ping() {	# $1 = phase name
	local name=$1 out="$EVIDENCE/peer-reverse-ping-$1.txt"
	{
		echo "== start $(date -Is) arp=dynamic (the static entry is dropped)"
		pisu "ip neigh show $BOARD_IP dev $AP_IF"
		pisu "ip neigh del $BOARD_IP dev $AP_IF 2>/dev/null || true"
		pisu "ip neigh show $BOARD_IP dev $AP_IF"
	} >"$out" 2>&1 || true
	pisu "ping -I $AP_IF -c $REVERSE_PING_COUNT -i $REVERSE_PING_INTERVAL -W 1 $BOARD_IP" \
		>>"$out" 2>&1 || true
	{
		echo "== end $(date -Is)"
		pisu "ip neigh show $BOARD_IP dev $AP_IF"
		# the phase table's later phases still need the unicast path, so the
		# bench's static entry comes back and the arm's reading stays isolated
		pisu "ip neigh replace $BOARD_IP lladdr $STA_MAC dev $AP_IF"
		pisu "ip neigh show $BOARD_IP dev $AP_IF"
	} >>"$out" 2>&1 || true
}

peer_udp() {	# $1 = phase name, $2 = seconds
	local name=$1 secs=$2
	pi "iperf -c $BOARD_IP -p 5002 -u -b $UDP_DN_RATE -t $secs -i 5" \
		>"$EVIDENCE/peer-$name.txt" 2>&1 || true
}
station() {	# $1 = tag
	pisu "iw dev $AP_IF station dump" > "$EVIDENCE/ap-station-$1.txt" 2>&1 || true
	# The vendor proc carries the per-station fields iw does not print on this
	# driver: the BA state, the rate ids and the byte counters.  Same phase,
	# same station, so the GT arm can be compared field by field.
	pisu "cat /proc/net/rtl88XXau/$AP_IF/all_sta_info" \
		> "$EVIDENCE/ap-sta-info-$1.txt" 2>&1 || true
}

anchor() {	# $1 = regex; true when the capture already holds it
	# The capture keeps the console's CRLF; a `$` anchor must not trip on it.
	[ -f "$SERIAL" ] || return 1
	tr -d '\r' < "$SERIAL" 2>/dev/null | grep -aqE "$1"
}

for _ in $(seq 1 3000); do
	if anchor "$STA_CASE_PREFIX: netif up " && [ "$seen_neigh" = 0 ]; then
		seen_neigh=1
		pisu "ip neigh replace $BOARD_IP lladdr $STA_MAC dev $AP_IF" \
			>"$EVIDENCE/peer-neigh.txt" 2>&1 || true
		pisu "ip neigh show $BOARD_IP" >>"$EVIDENCE/peer-neigh.txt" 2>&1 || true
	fi

	if [ "$seen_mondown" = 0 ] && anchor 'name=tcpdn$'; then
		seen_mondown=1
		pisu "timeout 55 tcpdump -i $MON_IF -s 0 -w $MON_DOWN_PCAP" \
			>"$EVIDENCE/mon-down-tcpdump.log" 2>&1 &
		mon_down_pid=$!
	fi
	if [ "$seen_tcpdn" = 0 ] && anchor 'name=tcpdn$'; then
		seen_tcpdn=1
		if [ "$REVERSE_PING" = 1 ]; then
			reverse_ping tcpdn
		fi
		peer_tcp tcpdn 28
		station tcpdn
	fi
	if [ "$seen_udpdn" = 0 ] && anchor 'name=udpdn$'; then
		seen_udpdn=1
		peer_udp udpdn 18
		station udpdn
	fi
	if [ "$seen_tcpdn2" = 0 ] && anchor 'name=tcpdn2$'; then
		seen_tcpdn2=1
		peer_tcp tcpdn2 28
		station tcpdn2
	fi
	if [ "$seen_udpdn2" = 0 ] && anchor 'name=udpdn2$'; then
		seen_udpdn2=1
		peer_udp udpdn2 18
		station udpdn2
	fi
	if [ "$seen_monup" = 0 ] && anchor 'name=tcpup$'; then
		seen_monup=1
		pisu "timeout 40 tcpdump -i $MON_IF -s 0 -w $MON_UP_PCAP" \
			>"$EVIDENCE/mon-up-tcpdump.log" 2>&1 &
		mon_up_pid=$!
		# The station's own record at the up phase: the same proc fields the
		# GT arm captures, at the same phase boundary.
		station tcpup
	fi
	# The raw blast has no iperf2 header; the counting sink is started a
	# phase early so its first datagram is not lost, and it idles until the
	# blast begins.
	if [ "$seen_sink" = 0 ] && anchor 'name=tcpup$'; then
		seen_sink=1
		pi "python3 /tmp/udp-sink-$TAG.py --port 5001 --seconds 35" \
			>"$EVIDENCE/udp-sink.txt" 2>&1 &
		sink_pid=$!
	fi

	if ! kill -0 "$BOOT_PID" 2>/dev/null; then
		break
	fi
	sleep 0.3
done

wait "$BOOT_PID" || true
sleep 2
pi "kill \$(cat $PI_DIR/peer-tcp.pid) 2>/dev/null || true" || true
[ -n "${mon_down_pid:-}" ] && wait "$mon_down_pid" 2>/dev/null || true
[ -n "${mon_up_pid:-}" ] && wait "$mon_up_pid" 2>/dev/null || true
[ -n "${sink_pid:-}" ] && wait "$sink_pid" 2>/dev/null || true
wait "$APCAP_PID" 2>/dev/null || true

# --- collect the peer logs and the AP's last view ---------------------------
pisu "cat $PI_DIR/peer-tcp-server.log" > "$EVIDENCE/peer-tcp-server.log" 2>/dev/null || true
pisu "tail -8 /tmp/KO1-ap.log" > "$EVIDENCE/ap-hostapd-tail.txt" 2>&1 || true
station end

# --- pcap summaries (stay on the Pi, hash + summary land here) --------------
pisu "chmod 644 $AP_PCAP $MON_DOWN_PCAP $MON_UP_PCAP 2>/dev/null || true"
{
	echo "ap=$AP_PCAP mon_down=$MON_DOWN_PCAP mon_up=$MON_UP_PCAP"
	pisu "sha256sum $AP_PCAP $MON_DOWN_PCAP $MON_UP_PCAP 2>/dev/null || true"
	pisu "stat -c '%n %s' $AP_PCAP $MON_DOWN_PCAP $MON_UP_PCAP 2>/dev/null || true"
} > "$EVIDENCE/pcap.txt" 2>&1 || true

{
	echo "== AP-side (802.3) address 192.168.99.2"
	pisu "tshark -r $AP_PCAP -q -z io,phs 2>/dev/null" || true
	pisu "tshark -r $AP_PCAP -Y 'ip.addr==192.168.99.2' -T fields -e frame.len 2>/dev/null | awk '{n++; s+=\$1} END {printf \"frames=%d bytes=%d\\n\", n, s}'" || true
	pisu "tshark -r $AP_PCAP -Y 'ip.addr==192.168.99.2 && tcp && tcp.len>0' -T fields -e tcp.len 2>/dev/null | awk '{n++; s+=\$1} END {printf \"tcp_payload_frames=%d payload_bytes=%d\\n\", n, s}'" || true
	pisu "tshark -r $AP_PCAP -Y 'ip.addr==192.168.99.2 && udp && udp.length>8' -T fields -e udp.length 2>/dev/null | awk '{n++; s+=\$1-8} END {printf \"udp_payload_frames=%d payload_bytes=%d\\n\", n, s}'" || true
	echo "== monitor (802.11) frames for the station"
	pisu "tshark -r $MON_DOWN_PCAP -Y 'wlan.addr==$STA_MAC' -T fields -e wlan.fc.type_subtype -e wlan.fc.retry -e wlan_radio.11n.mcs_index -e radiotap.ampdu.flags -e frame.len 2>/dev/null" > "$EVIDENCE/air-fields-down.csv" || true
	pisu "tshark -r $MON_UP_PCAP -Y 'wlan.addr==$STA_MAC' -T fields -e wlan.fc.type_subtype -e wlan.fc.retry -e wlan_radio.11n.mcs_index -e radiotap.ampdu.flags -e frame.len 2>/dev/null" > "$EVIDENCE/air-fields-up.csv" || true
	awk -F'\t' 'NF{ n++; if ($1=="0x0019") ba++; if ($2=="1") retry++; if ($4!="") ampdu++ } END { printf "frames=%d ba=%d retry=%d ampdu=%d\n", n, ba+0, retry+0, ampdu+0 }' "$EVIDENCE/air-fields-down.csv" | sed 's/^/down /'
	awk -F'\t' 'NF{ n++; if ($1=="0x0019") ba++; if ($2=="1") retry++; if ($4!="") ampdu++ } END { printf "frames=%d ba=%d retry=%d ampdu=%d\n", n, ba+0, retry+0, ampdu+0 }' "$EVIDENCE/air-fields-up.csv" | sed 's/^/up   /'
} > "$EVIDENCE/air-summary.txt" 2>&1 || true

# --- judge -------------------------------------------------------------------
python3 "$HERE/judge.py" "$SERIAL" --json "$EVIDENCE/judge.json" \
	--expect cases=$CASE_EXPECT \
	>"$EVIDENCE/judge.txt" 2>&1 || true

echo "c6-throughput: evidence -> $EVIDENCE"
tail -5 "$EVIDENCE/judge.txt" 2>/dev/null || true

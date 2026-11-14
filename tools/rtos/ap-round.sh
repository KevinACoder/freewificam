#!/bin/bash
# ap-round.sh - one RTOS AP round: boot rk3568-iwl7260-ap, let the case start
# the AP, join the Pi's MT7921AU as the STA, prove the data plane, and drive
# the AP phase table from the case's own anchors.
#
#   tools/rtos/ap-round.sh --evidence runs/<date>-iwl7260-ap/rtos1
#
# The image is `rk3568-iwl7260-ap` (tick pcie iwl7260_ap).  The Pi side is
# freewificam/tools/rtos/pi-ap.sh: it stops lab-ap, turns wlan2 into a monitor
# on the AP's channel, turns wlan1 into the STA with 192.168.99.2/24, and is
# restored by a trap even when the round fails.
#
# Direction words are the round's: air=down is AP->STA (the board sends; the
# Pi's server/sink is the receiver), air=up is STA->AP (the Pi sends; this
# board's lwIP is the receiver).  The Pi's receiver files and the board's phase
# reports are the two app sources of the report; the monitor's windows are the
# air source.
#
# Outputs under --evidence:
#   serial.log / transcript.txt / timings.json    (boot-rtos.sh)
#   pi-ap-<tag>/peer-*.txt, sink-*.txt            (the Pi's receivers)
#   peer-tcpup.txt / peer-udpup.txt               (the Pi's senders)
#   pi-scan.txt / pi-join.txt / pi-link.txt / pi-ping.txt / pi-mac.txt
#   air-fields-{down,up}.csv, air-payload-{down,up}.txt
#   pcap.txt, judge.json, judge.txt
# The pcaps stay on the Pi (disk-backed /home/pi); their hashes land here.

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BOARD="$ROOT/../skills/rk3568-board/scripts"
PI=${PI:-pi@192.168.0.17}
CH=${CH:-11}
FREQ=${FREQ:-2462}
SSID=${SSID:-rtos-7260-ap}
BOARD_IP=${BOARD_IP:-192.168.99.1}
PI_IP=${PI_IP:-192.168.99.2}
MON_IF=${MON_IF:-wlan2}
PSK=${PSK:-rtos-7260-wpa2-psk}
WPA2=${WPA2:-0}
# The case under test.  The default is the AC 7260 AP arm; the 8189FTV arm
# overrides both (its anchors carry its own prefix and its own case list).
CASE=${CASE:-iwl7260_ap}
CASE_EXPECT=${CASE_EXPECT:-tick,pcie,iwl7260_ap}
BIN="$ROOT/tests/board/rtos/build/build/rtos-freertos.bin"
TIMEOUT=480
EVIDENCE=
DIAG=0
NO_PEER=0
# The stall arm keeps the boot alive through a silence and sends a liveness
# ping every 2 s; the clean arm stays free of that extra traffic.
STALL=0
BOOT_RETRY=1

while [ $# -gt 0 ]; do
	case "$1" in
		--evidence) EVIDENCE=${2:?}; shift 2 ;;
		--bin)      BIN=${2:?}; shift 2 ;;
		--timeout)  TIMEOUT=${2:?}; shift 2 ;;
		--channel)  CH=${2:?}; shift 2 ;;
		--freq)     FREQ=${2:?}; shift 2 ;;
		--wpa2)     WPA2=1; shift ;;
		--diag)     DIAG=1; shift ;;
		--no-peer)  NO_PEER=1; shift ;;
		--stall-arm) STALL=1; TIMEOUT=900; BOOT_RETRY=0; shift ;;
		-h|--help)  sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "ap-round: unknown argument $1" >&2; exit 2 ;;
	esac
done
[ -n "$EVIDENCE" ] || { echo "ap-round: --evidence DIR is required" >&2; exit 2; }
mkdir -p "$EVIDENCE"
EVIDENCE=$(cd "$EVIDENCE" && pwd)
TAG=$(basename "$EVIDENCE")
PI_DIR=/home/pi/pi-ap-$TAG
MON_DOWN_PCAP=$PI_DIR/mon-down.pcap
MON_UP_PCAP=$PI_DIR/mon-up.pcap

pi() { ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" "$@"; }
pisu() { pi "sudo -n $*"; }
pih() { pi "sudo -n env CH=$CH FREQ=$FREQ SSID=$SSID PI_IP=$PI_IP AP_IP=$BOARD_IP PSK=$PSK WPA2=$WPA2 bash /tmp/pi-ap.sh $*"; }

RESTORED=0
mon_down_pid=
mon_up_pid=
restore_pi() {
	[ "$RESTORED" = 1 ] && return 0
	RESTORED=1
	[ -n "${mon_down_pid:-}" ] && kill "$mon_down_pid" 2>/dev/null || true
	[ -n "${mon_up_pid:-}" ] && kill "$mon_up_pid" 2>/dev/null || true
	if [ "$NO_PEER" = 0 ]; then
		pih "servers-stop" >/dev/null 2>&1 || true
		pih "down" > "$EVIDENCE/pi-restore.txt" 2>&1 || true
	fi
}
trap restore_pi EXIT

# The Pi helper and the counting sink land before the gate.
scp -q -o BatchMode=yes "$HERE/pi-ap.sh" "$PI:/tmp/pi-ap.sh"
scp -q -o BatchMode=yes "$HERE/udp-sink.py" "$PI:/tmp/udp-sink-ap.py"
pisu "chmod 755 /tmp/pi-ap.sh"
pi "mkdir -p $PI_DIR"

# --- gate: the bench must be the standard one before we switch it -----------
{
	echo "tag=$TAG"
	echo "time=$(date -Is)"
	echo "channel=$CH ssid=$SSID ap=$BOARD_IP sta=$PI_IP"
	echo "mode=diag:$DIAG no_peer:$NO_PEER"
	if [ "$NO_PEER" = 0 ]; then
		pi 'systemctl is-active lab-ap 2>/dev/null || true'
		pih status
		pi 'iperf -v 2>&1 | head -1'
	fi
} > "$EVIDENCE/bench-gate-before.txt" 2>&1 || true

STA_MAC=
if [ "$NO_PEER" = 0 ]; then
	pih "up" > "$EVIDENCE/bench-gate-up.txt" 2>&1
	pisu "iw dev $MON_IF info" >> "$EVIDENCE/bench-gate-up.txt" 2>&1 || true
	pih "mac" > "$EVIDENCE/pi-mac.txt" 2>&1 || true
	STA_MAC=$(tr -d '\r\n' < "$EVIDENCE/pi-mac.txt" 2>/dev/null || true)
	echo "sta_mac=${STA_MAC:-?}" >> "$EVIDENCE/bench-gate-up.txt"

	pisu "iw dev $MON_IF info" 2>/dev/null | grep -q "channel $CH" \
		|| { echo "ap-round: $MON_IF is not a monitor on channel $CH" >&2; exit 3; }
	# The peer must be a station, not just "not a monitor": the 2026-11-12
	# regression boot lost a join to EOPNOTSUPP because wlan1 reported managed
	# while nl80211 still had it in another mode, and the round then recorded
	# a board FAIL for a bench fault.  Validity first.
	STA_IF=${STA_IF:-wlan1}
	pisu "iw dev $STA_IF info" 2>/dev/null | grep -q "type managed" \
		|| { echo "ap-round: $STA_IF is not a managed station" >&2; exit 3; }
fi

# --- clean the peer, put the image on the TFTP root -------------------------
# The image is copied into the evidence directory first: a concurrent build
# writes the same build/build/rtos-freertos.bin, and 2026-11-11's first rtos1
# boot came up with the CI's last profile (tick usb_ehci) for exactly that
# reason.  The copy makes the measured bytes and the deployed bytes one file.
if [ "$NO_PEER" = 0 ]; then
	pih "servers-stop" >/dev/null 2>&1 || true
fi
cp -f "$BIN" "$EVIDENCE/rtos.bin"
sha256sum "$EVIDENCE/rtos.bin" > "$EVIDENCE/rtos-bin.sha256"
"$BOARD/tftp.sh" put "$EVIDENCE/rtos.bin" --name rtos.bin \
	> "$EVIDENCE/tftp.txt" 2>&1

# --- boot -------------------------------------------------------------------
"$BOARD/boot-rtos.sh" --bin rtos.bin --success "rtos: result" --quiet 0 \
	--timeout "$TIMEOUT" --power-cycle --retry "$BOOT_RETRY" --evidence "$EVIDENCE" \
	>"$EVIDENCE/boot.out" 2>&1 &
BOOT_PID=$!

SERIAL="$EVIDENCE/serial.log"
seen_start_ap=0
seen_peer=0
seen_hostap_up=0
seen_mondown=0
seen_monup=0
beacon_pid=
diag_mon_pid=

declare -A seen_phase=()
declare -A seen_done=()
declare -A phase_t0=()
declare -A phase_t1=()
phase_pids=()
phase_order=()
a4_started=0
a4_mon_pid=
a4_ip_pid=
alive_pid=

anchor() {	# $1 = regex
	[ -f "$SERIAL" ] || return 1
	tr -d '\r' < "$SERIAL" 2>/dev/null | grep -aqE "$1"
}

# One whole-A4 monitor capture (not one per direction): the frames carry both
# directions, so the phase is the time slice, not the file.  The old down
# window spanned four phases and its payload totals were reported as one.
start_a4_captures() {
	pisu "timeout 200 tcpdump -i $MON_IF -s 256 -w $PI_DIR/mon-a4.pcap" \
		>>"$EVIDENCE/mon-a4-tcpdump.log" 2>&1 &
	a4_mon_pid=$!
	pisu "timeout 200 tcpdump -i $STA_IF -s 96 -w $PI_DIR/ip-a4.pcap" \
		>>"$EVIDENCE/ip-a4-tcpdump.log" 2>&1 &
	a4_ip_pid=$!
}

# The peer receivers are started before the board's phase, not on its begin
# line: the 2026-11-14 a4-5 boot lost phase 1 because the ssh + iperf startup
# had not finished when the board connected.
start_peer_tcp() {	# $1 = tag, $2 = seconds
	pih "server-tcp $PI_DIR/peer-$1.txt $2" >/dev/null 2>&1 &
	phase_pids+=($!)
}

start_peer_sink() {	# $1 = tag, $2 = seconds
	pih "sink /tmp/udp-sink-ap.py $PI_DIR/sink-$1.txt $2" >/dev/null 2>&1 &
	phase_pids+=($!)
}

# Both downlink TCP servers from one sequential job: the first serves phase 1
# and exits when the client closes; the second then binds the now-free port
# and waits through phase 2 for phase 3.  Starting the second at phase 1's
# done raced the first server's 40 s timeout and it died on "Address already
# in use" (2026-11-14 rtos-ap1: phase 3 0 bytes).
start_tcp_pair() {
	( pih "server-tcp $PI_DIR/peer-tcpdn.txt 25" >/dev/null 2>&1
	  pih "server-tcp $PI_DIR/peer-tcpdn2.txt 70" >/dev/null 2>&1 ) &
	phase_pids+=($!)
}

alive_ping() {
	pi "sudo -n ping -I $STA_IF -c 600 -i 2 -W 1 $BOARD_IP" \
		> "$EVIDENCE/alive.txt" 2>&1 &
	alive_pid=$!
}

on_phase() {		# $1 = name
	case "$1" in
	tcpup)
		pih "client-tcp 24" > "$EVIDENCE/peer-tcpup.txt" 2>&1 &
		phase_pids+=($!)
		;;
	udpup)
		pih "client-udp 18" > "$EVIDENCE/peer-udpup.txt" 2>&1 &
		phase_pids+=($!)
		;;
	esac
	pih "link" > "$EVIDENCE/link-$1.txt" 2>&1 &
	phase_pids+=($!)
}

for _ in $(seq 1 3000); do
	if [ "$seen_start_ap" = 0 ] && anchor "$CASE: hostap start rc=0 ready=1"; then
		seen_start_ap=1
		# WPA2: capture the 4-way before the join; the open path's beacon
		# capture stays as it was.
		if [ "$WPA2" = 1 ]; then
			pisu "timeout 25 tcpdump -i $MON_IF -s 0 -w $PI_DIR/wpa4way.pcap" \
				>"$EVIDENCE/wpa4way-tcpdump.log" 2>&1 &
			wpa_mon_pid=$!
		fi
		# A1's on-air probe: full-length beacons on the AP's channel, started
		# before the scans so a beacon that does appear is not missed.
		pisu "timeout 12 tcpdump -i $MON_IF -s 0 -w $PI_DIR/beacon.pcap" \
			>"$EVIDENCE/beacon-tcpdump.log" 2>&1 &
		beacon_pid=$!
		# The diagnostic arm needs the whole round on the air, not the A1
		# 12 s window: the freeze and the last beacon are minutes apart.
		if [ "$DIAG" = 1 ] && [ "$NO_PEER" = 0 ]; then
			pisu "timeout 500 tcpdump -i $MON_IF -s 256 -w $PI_DIR/mon-diag.pcap" \
				>"$EVIDENCE/mon-diag-tcpdump.log" 2>&1 &
			diag_mon_pid=$!
		fi
		if [ "$NO_PEER" = 0 ]; then
			# Two channel scans 1 s apart (PREREG A1): the first can land
			# before the first beacon of a freshly started BSS, and the 2026-11-11
			# round's single scan listed the neighbour but not this AP.
			pih "scan" > "$EVIDENCE/pi-scan.txt" 2>&1 || true
			sleep 1
			pih "scan" >> "$EVIDENCE/pi-scan.txt" 2>&1 || true
			pih "scan-all" > "$EVIDENCE/pi-scan-all.txt" 2>&1 || true
			if [ "$WPA2" = 1 ]; then
				pih "join-wpa" > "$EVIDENCE/pi-join.txt" 2>&1 || true
				pih "wpa-status" > "$EVIDENCE/pi-wpa-status.txt" 2>&1 || true
				pih "wpa-log" > "$EVIDENCE/pi-wpa.log" 2>&1 || true
			else
				pih "join" > "$EVIDENCE/pi-join.txt" 2>&1 || true
			fi
			pih "link" > "$EVIDENCE/pi-link.txt" 2>&1 || true
		fi
	fi

	if [ "$DIAG" = 1 ] && [ "$NO_PEER" = 1 ] && [ "$seen_peer" = 0 ] && \
	   anchor "$CASE: peer window ms="; then
		seen_peer=1
		# The idle arm has no peer by construction; nothing to start.
		echo "idle arm: no peer at $(date -Is)" >> "$EVIDENCE/phase-seen.txt"
	fi

	if [ "$DIAG" = 1 ] && [ "$NO_PEER" = 0 ] && [ "$seen_hostap_up" = 0 ] && \
	   anchor "$CASE: PASS A2"; then
		seen_hostap_up=1
		pih "sink /tmp/udp-sink-ap.py $PI_DIR/sink-diag.txt 120" \
			>/dev/null 2>&1 &
		phase_pids+=($!)
	fi

	if [ "$NO_PEER" = 0 ] && [ "$seen_peer" = 0 ] && \
	   anchor "$CASE: peer window ms="; then
		seen_peer=1
		if [ "$WPA2" = 1 ]; then
			# join-wpa has already waited for COMPLETED; ping after it,
			# not at the station-table anchor (which precedes the 4-way).
			pih "ping" > "$EVIDENCE/pi-ping.txt" 2>&1 || true
		else
			pih "ping" > "$EVIDENCE/pi-ping.txt" 2>&1 || true
		fi
	fi

	if [ "$a4_started" = 0 ] && [ "$NO_PEER" = 0 ] && \
	   anchor "$CASE: server tcp="; then
		a4_started=1
		echo "a4-ready at $(date -Is)" >> "$EVIDENCE/phase-seen.txt"
		start_a4_captures
		# both TCP receivers are up before the board's first connect.
		start_tcp_pair
		if [ "$STALL" = 1 ]; then
			alive_ping
		fi
	fi

	PHASES=""
	if [ "$NO_PEER" = 0 ]; then
		# double-quoted on purpose: $CASE must expand.  A single-quoted sed
		# here leaves the literal "$CASE" in the pattern, the phase list stays
		# empty, and every peer-driven phase reads 0 bytes for a harness
		# reason (the 2026-11-13 A4 boot 1 did exactly that).
		[ -f "$SERIAL" ] && PHASES=$(tr -d '\r' < "$SERIAL" 2>/dev/null | sed -n "s/.*$CASE: phase [0-9][0-9]* begin .*name=\([a-z0-9]*\) peer_ms=.*/\1/p")
	fi
	while IFS= read -r ph; do
		[ -n "$ph" ] || continue
		[ -n "${seen_phase[$ph]:-}" ] && continue
		seen_phase[$ph]=1
		phase_t0[$ph]=$(date +%s.%N)
		phase_order+=("$ph")
		echo "phase $ph at $(date -Is) epoch=${phase_t0[$ph]}" \
			>> "$EVIDENCE/phase-seen.txt"
		# The downlink sink is up ~15 s before phase 4 (started during
		# phase 3), so its bind cannot race the blast.
		if [ "$ph" = tcpdn2 ]; then
			start_peer_sink udpdn 40
		fi
		on_phase "$ph"
	done <<< "$PHASES"

	# Phase ends: the Nth done line maps to the Nth begin in phase_order.
	DONE=""
	if [ -f "$SERIAL" ]; then
		DONE=$(tr -d '\r' < "$SERIAL" 2>/dev/null | \
			sed -n "s/.*$CASE: phase \([0-9][0-9]*\) done ok=\([01]\).*/\1 \2/p")
	fi
	while read -r num ok; do
		[ -n "$num" ] || continue
		[ -n "${seen_done[$num]:-}" ] && continue
		seen_done[$num]=1
		ph=${phase_order[$((num - 1))]:-phase$num}
		phase_t1[$ph]=$(date +%s.%N)
		printf '%s\t%s\t%s\t%s\n' "$ph" "${phase_t0[$ph]:-}" \
			"${phase_t1[$ph]}" "$ok" >> "$EVIDENCE/phase-window.tsv"
		# Backgrounded: a phase's snapshots must not delay the next
		# phase's receiver (the gt-ap3 run's loop fell behind and its
		# peer actions landed after the board's flows).
		pih "station" > "$EVIDENCE/pi-$ph-station.txt" 2>&1 &
		phase_pids+=($!)
		pih "netdev" > "$EVIDENCE/pi-$ph-netdev.txt" 2>&1 &
		phase_pids+=($!)
		pih "nstat" > "$EVIDENCE/pi-$ph-nstat.txt" 2>&1 &
		phase_pids+=($!)
	done <<< "$DONE"

	if ! kill -0 "$BOOT_PID" 2>/dev/null; then
		break
	fi
	sleep 0.3
done

wait "$BOOT_PID" || true
sleep 2
if [ -n "${diag_mon_pid:-}" ]; then
	kill "$diag_mon_pid" 2>/dev/null || true
	pisu "pkill -f mon-diag.pcap" >/dev/null 2>&1 || true
	diag_mon_pid=
fi

# The image identity check the banner cannot give: the case list is what says
# this is the AP image and not whatever the last build left behind.
if ! [ -f "$SERIAL" ] || ! tr -d '\r' < "$SERIAL" | grep -aq "$CASE: core ready"; then
	echo "ap-round: INVALID BOOT: no $CASE anchors (wrong image?)" >&2
	echo "invalid: wrong image (no $CASE anchor)" > "$EVIDENCE/invalid.txt"
	if [ "$NO_PEER" = 0 ]; then
		pih "servers-stop" >/dev/null 2>&1 || true
		pih "down" > "$EVIDENCE/pi-restore.txt" 2>&1 || true
	fi
	RESTORED=1
	exit 2
fi
if [ "$WPA2" = 1 ] && ! tr -d '\r' < "$SERIAL" | grep -aq "$CASE: security wpa2=1"; then
	echo "ap-round: INVALID BOOT: --wpa2 but no security wpa2=1 anchor" >&2
	echo "invalid: wpa2 image not deployed" > "$EVIDENCE/invalid.txt"
	if [ "$NO_PEER" = 0 ]; then
		pih "servers-stop" >/dev/null 2>&1 || true
		pih "down" > "$EVIDENCE/pi-restore.txt" 2>&1 || true
	fi
	RESTORED=1
	exit 2
fi
if [ "$NO_PEER" = 0 ]; then
	pih "servers-stop" >/dev/null 2>&1 || true
fi
# The A4 captures run under `timeout`; stop them now so the tar below copies
# complete files.  `pkill -x tcpdump` is exact-name on purpose: the earlier
# `pkill -f <pcap>` matched the ssh command that carried the pattern and killed
# the session (2026-11-14).
pisu "pkill -x tcpdump" >/dev/null 2>&1 || true
[ -n "${a4_mon_pid:-}" ] && wait "$a4_mon_pid" 2>/dev/null || true
[ -n "${a4_ip_pid:-}" ] && wait "$a4_ip_pid" 2>/dev/null || true
if [ -n "${alive_pid:-}" ]; then
	kill "$alive_pid" 2>/dev/null || true
	pisu "pkill -x ping" >/dev/null 2>&1 || true
fi
[ -n "${beacon_pid:-}" ] && wait "$beacon_pid" 2>/dev/null || true
[ -n "${wpa_mon_pid:-}" ] && wait "$wpa_mon_pid" 2>/dev/null || true
for p in "${phase_pids[@]:-}"; do wait "$p" 2>/dev/null || true; done 2>/dev/null || true

# --- A3 evidence from the AP side: the round's own log ----------------------
if [ "$NO_PEER" = 0 ]; then
	pih "down" > "$EVIDENCE/bench-gate-after.txt" 2>&1 || true
fi
RESTORED=1

# --- collect the Pi's receivers --------------------------------------------
if [ "$NO_PEER" = 0 ] && pi "sudo -n test -d $PI_DIR"; then
	pi "sudo -n tar -C $(dirname "$PI_DIR") -cf - $(basename "$PI_DIR")" \
		| tar -C "$EVIDENCE" -xf -
fi
# ap-report.py reads run/<tag>/sink-<phase>.txt; the counting sink writes inside
# the Pi directory, so surface its files one level up too (the 2026-11-12 round
# had them only under pi-ap-<tag>/ and the report saw 0 bytes).
for f in "$EVIDENCE"/"pi-ap-$TAG"/sink-*.txt*; do
	[ -e "$f" ] && cp "$f" "$EVIDENCE/" || true
done

if [ "$NO_PEER" = 0 ]; then
{
	echo "beacon=$PI_DIR/beacon.pcap mon=$PI_DIR/mon-a4.pcap ip=$PI_DIR/ip-a4.pcap"
	pisu "chmod 644 $PI_DIR/beacon.pcap $PI_DIR/mon-a4.pcap $PI_DIR/ip-a4.pcap 2>/dev/null || true"
	pisu "sha256sum $PI_DIR/beacon.pcap $PI_DIR/mon-a4.pcap $PI_DIR/ip-a4.pcap 2>/dev/null || true"
	pisu "stat -c '%n %s' $PI_DIR/beacon.pcap $PI_DIR/mon-a4.pcap $PI_DIR/ip-a4.pcap 2>/dev/null || true"
	pisu "tshark -r $PI_DIR/beacon.pcap -Y 'wlan.fc.type_subtype==0x0008' -T fields -e wlan.bssid -e wlan.ssid -e wlan.fixed.beacon -e wlan.tim.dtim_period -e radiotap.channel.freq -e wlan.tag.number -e wlan.tag.length 2>/dev/null | sort -u | head -5" || true
	echo "== tcpdump drop lines"
	tail -2 "$EVIDENCE/mon-a4-tcpdump.log" "$EVIDENCE/ip-a4-tcpdump.log" 2>/dev/null || true
} > "$EVIDENCE/beacon-fields.txt" 2>&1 || true
fi

# --- per-phase air and IP ledgers ------------------------------------------
# Each phase is a time slice of one whole-A4 capture.  The 2026-11-14 round's
# "down" window spanned four phases, which is how a 5% board->air loss got
# written from a total that mixed them.
if [ "$NO_PEER" = 0 ]; then
	# the AP's BSSID is the second address the air ledger needs; the Pi's
	# link line is the authority (the RTOS case's MAC anchor differs per case).
	AP_MAC=$(sed -n 's/^Connected to \([0-9a-f:]*\).*/\1/p' "$EVIDENCE/pi-link.txt" 2>/dev/null | head -1)
	for ph in "${phase_order[@]:-}"; do
		t0=${phase_t0[$ph]:-}
		t1=${phase_t1[$ph]:-}
		[ -n "$t0" ] && [ -n "$t1" ] || continue
		pisu "tshark -r $PI_DIR/mon-a4.pcap -Y 'frame.time_epoch>=$t0 && frame.time_epoch<=$t1 && wlan.addr==$STA_MAC' -T fields -e wlan.fc.type_subtype -e wlan.fc.retry -e wlan_radio.11n.mcs_index -e radiotap.ampdu.flags -e frame.len -e wlan.ta -e wlan.ra -e tcp.len -e udp.length 2>/dev/null" \
			> "$EVIDENCE/air-$ph.csv" || true
		python3 "$HERE/c6-air.py" "$EVIDENCE/air-$ph.csv" \
			> "$EVIDENCE/air-$ph.txt" 2>&1 || true
		# direction and payload, from the same CSV: ta == AP is this board
		# sending (down); ta == STA is the peer sending (up).
		awk -F'\t' -v ap="$AP_MAC" -v sta="$STA_MAC" '
			NF {
				n++
				if ($2 == "True" || $2 == "1") retry++
				if (ap != "" && $6 == ap) {
					down++
					if ($8 != "" && $8 > 0) { dtcp++; dtcpb += $8 }
					if ($9 != "" && $9 > 0) { dudp++; dudpb += $9 - 8 }
				} else if ($6 == sta) {
					up++
					if ($8 != "" && $8 > 0) { utcp++; utcpb += $8 }
					if ($9 != "" && $9 > 0) { uudp++; uudpb += $9 - 8 }
				}
			}
			END {
				printf "phase frames=%d down=%d up=%d retry=%d ", n, down, up, retry
				printf "down_tcp=%d/%d down_udp=%d/%d up_tcp=%d/%d up_udp=%d/%d\n", \
					dtcp, dtcpb, dudp, dudpb, utcp, utcpb, uudp, uudpb
			}' "$EVIDENCE/air-$ph.csv" >> "$EVIDENCE/air-$ph.txt" 2>/dev/null || true
		pisu "tshark -r $PI_DIR/ip-a4.pcap -Y 'frame.time_epoch>=$t0 && frame.time_epoch<=$t1' -T fields -e frame.len -e ip.proto 2>/dev/null" \
			> "$EVIDENCE/ip-$ph.csv" || true
		awk -F'\t' '
			NF {
				n++; b += $1
				if ($2 == "6") { tcp++; tcpb += $1 }
				else if ($2 == "17") { udp++; udpb += $1 }
			}
			END { printf "ip frames=%d bytes=%d tcp=%d/%d udp=%d/%d\n", n, b, tcp, tcpb, udp, udpb }
		' "$EVIDENCE/ip-$ph.csv" > "$EVIDENCE/ip-$ph.txt" 2>/dev/null || true
	done
	python3 "$HERE/c6-air.py" "$EVIDENCE"/air-*.csv > "$EVIDENCE/air-summary.txt" 2>&1 || true
fi

# --- judge -------------------------------------------------------------------
python3 "$HERE/judge.py" "$SERIAL" --json "$EVIDENCE/judge.json" \
	--expect cases=$CASE_EXPECT \
	>"$EVIDENCE/judge.txt" 2>&1 || true

echo "ap-round: evidence -> $EVIDENCE"
tail -5 "$EVIDENCE/judge.txt" 2>/dev/null || true

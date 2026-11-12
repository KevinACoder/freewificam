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
BIN="$ROOT/tests/board/rtos/build/build/rtos-freertos.bin"
TIMEOUT=480
EVIDENCE=
DIAG=0
NO_PEER=0

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
	--timeout "$TIMEOUT" --power-cycle --retry 1 --evidence "$EVIDENCE" \
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
phase_pids=()

anchor() {	# $1 = regex
	[ -f "$SERIAL" ] || return 1
	tr -d '\r' < "$SERIAL" 2>/dev/null | grep -aqE "$1"
}

start_mon_window() {	# $1 = down|up
	pisu "timeout $2 tcpdump -i $MON_IF -s 128 -w $PI_DIR/mon-$1.pcap" \
		>>"$EVIDENCE/mon-$1-tcpdump.log" 2>&1 &
}

on_phase() {		# $1 = name
	case "$1" in
	tcpdn)
		pih "server-tcp $PI_DIR/peer-tcpdn.txt 25" >/dev/null 2>&1 &
		phase_pids+=($!)
		start_mon_window down 75
		mon_down_pid=$!
		;;
	udpdn)
		pih "sink /tmp/udp-sink-ap.py $PI_DIR/sink-udpdn.txt 25" >/dev/null 2>&1 &
		phase_pids+=($!)
		;;
	tcpdn2)
		pih "server-tcp $PI_DIR/peer-tcpdn2.txt 25" >/dev/null 2>&1 &
		phase_pids+=($!)
		;;
	udpdn2)
		pih "sink /tmp/udp-sink-ap.py $PI_DIR/sink-udpdn2.txt 25" >/dev/null 2>&1 &
		phase_pids+=($!)
		;;
	tcpup)
		start_mon_window up 60
		mon_up_pid=$!
		pih "client-tcp 28" > "$EVIDENCE/peer-tcpup.txt" 2>&1
		;;
	udpup)
		pih "client-udp 18" > "$EVIDENCE/peer-udpup.txt" 2>&1
		;;
	esac
	pih "link" > "$EVIDENCE/link-$1.txt" 2>&1 || true
}

for _ in $(seq 1 3000); do
	if [ "$seen_start_ap" = 0 ] && anchor 'iwl7260_ap: (start_ap|hostap start) rc=0'; then
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
	   anchor 'iwl7260_ap: peer window ms='; then
		seen_peer=1
		# The idle arm has no peer by construction; nothing to start.
		echo "idle arm: no peer at $(date -Is)" >> "$EVIDENCE/phase-seen.txt"
	fi

	if [ "$DIAG" = 1 ] && [ "$NO_PEER" = 0 ] && [ "$seen_hostap_up" = 0 ] && \
	   anchor 'iwl7260_ap: hostap up ready=1 sta='; then
		seen_hostap_up=1
		pih "sink /tmp/udp-sink-ap.py $PI_DIR/sink-diag.txt 120" \
			>/dev/null 2>&1 &
		phase_pids+=($!)
	fi

	if [ "$NO_PEER" = 0 ] && [ "$seen_peer" = 0 ] && \
	   anchor 'iwl7260_ap: peer window ms='; then
		seen_peer=1
		if [ "$WPA2" = 1 ]; then
			# join-wpa has already waited for COMPLETED; ping after it,
			# not at the station-table anchor (which precedes the 4-way).
			pih "ping" > "$EVIDENCE/pi-ping.txt" 2>&1 || true
		else
			pih "ping" > "$EVIDENCE/pi-ping.txt" 2>&1 || true
		fi
	fi

	PHASES=""
	if [ "$NO_PEER" = 0 ]; then
		[ -f "$SERIAL" ] && PHASES=$(tr -d '\r' < "$SERIAL" 2>/dev/null | sed -n 's/.*iwl7260_ap: phase [0-9][0-9]* begin .*name=\([a-z0-9]*\) peer_ms=.*/\1/p')
	fi
	while IFS= read -r ph; do
		[ -n "$ph" ] || continue
		[ -n "${seen_phase[$ph]:-}" ] && continue
		seen_phase[$ph]=1
		echo "phase $ph at $(date -Is)" >> "$EVIDENCE/phase-seen.txt"
		on_phase "$ph"
	done <<< "$PHASES"

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
if ! [ -f "$SERIAL" ] || ! tr -d '\r' < "$SERIAL" | grep -aq 'iwl7260_ap: core ready'; then
	echo "ap-round: INVALID BOOT: no iwl7260_ap anchors (wrong image?)" >&2
	echo "invalid: wrong image (no iwl7260_ap anchor)" > "$EVIDENCE/invalid.txt"
	if [ "$NO_PEER" = 0 ]; then
		pih "servers-stop" >/dev/null 2>&1 || true
		pih "down" > "$EVIDENCE/pi-restore.txt" 2>&1 || true
	fi
	RESTORED=1
	exit 2
fi
if [ "$WPA2" = 1 ] && ! tr -d '\r' < "$SERIAL" | grep -aq 'iwl7260_ap: security wpa2=1'; then
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
[ -n "${beacon_pid:-}" ] && wait "$beacon_pid" 2>/dev/null || true
[ -n "${wpa_mon_pid:-}" ] && wait "$wpa_mon_pid" 2>/dev/null || true
[ -n "${mon_down_pid:-}" ] && wait "$mon_down_pid" 2>/dev/null || true
[ -n "${mon_up_pid:-}" ] && wait "$mon_up_pid" 2>/dev/null || true
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
for f in "$EVIDENCE"/"pi-ap-$TAG"/sink-*.txt; do
	[ -e "$f" ] && cp "$f" "$EVIDENCE/" || true
done

if [ "$NO_PEER" = 0 ]; then
{
	echo "beacon=$PI_DIR/beacon.pcap wpa4way=$PI_DIR/wpa4way.pcap"
	pisu "chmod 644 $PI_DIR/beacon.pcap 2>/dev/null || true"
	pisu "sha256sum $PI_DIR/beacon.pcap 2>/dev/null || true"
	pisu "tshark -r $PI_DIR/beacon.pcap -Y 'wlan.fc.type_subtype==8' -T fields -e wlan.bssid -e wlan.ssid -e wlan.fixed.beacon -e wlan.tim.dtim_period -e radiotap.channel.freq 2>/dev/null | sort -u | head -20" || true
} > "$EVIDENCE/beacon-fields.txt" 2>&1 || true
fi

# The diagnostic arm needs the whole round on the air: when did the last
# beacon leave the AP, relative to the last host interrupt?  tshark runs on
# the Pi (the host has no dissector); the summary is computed here.
if [ "$DIAG" = 1 ] && [ "$NO_PEER" = 0 ]; then
	pisu "chmod 644 $PI_DIR/mon-diag.pcap 2>/dev/null || true"
	pisu "sha256sum $PI_DIR/mon-diag.pcap 2>/dev/null || true"
	pisu "tshark -r $PI_DIR/mon-diag.pcap -Y 'wlan.fc.type_subtype==8' -T fields -e frame.time_relative -e wlan.sa -e wlan.seq 2>/dev/null" \
		> "$EVIDENCE/air-beacon-diag.txt" || true
	awk -F'\t' 'NF { n++; last=$1; if (n==1) first=$1; c[$2]++ }
		END { printf "beacons=%d first=%s last=%s\n", n, first, last;
		      for (k in c) printf "sa=%s frames=%d\n", k, c[k] }' \
		"$EVIDENCE/air-beacon-diag.txt" > "$EVIDENCE/air-beacon-diag-summary.txt" 2>/dev/null || true
fi

if [ "$WPA2" = 1 ]; then
	{
		pisu "chmod 644 $PI_DIR/wpa4way.pcap 2>/dev/null || true"
		pisu "sha256sum $PI_DIR/wpa4way.pcap 2>/dev/null || true"
		echo "== EAPOL frames (802.1X, type 3 = EAPOL-Key)"
		pisu "tshark -r $PI_DIR/wpa4way.pcap -Y 'eapol' -T fields -e frame.time_relative -e wlan.sa -e wlan.da -e eapol.type -e eapol.keydes.key_info 2>/dev/null" || true
		echo "== count"
		pisu "tshark -r $PI_DIR/wpa4way.pcap -Y 'eapol' -T fields -e frame.number 2>/dev/null | wc -l" || true
	} > "$EVIDENCE/air-eapol.txt" 2>&1 || true
fi

{
	echo "down=$MON_DOWN_PCAP up=$MON_UP_PCAP"
	pisu "chmod 644 $MON_DOWN_PCAP $MON_UP_PCAP 2>/dev/null || true"
	pisu "sha256sum $MON_DOWN_PCAP $MON_UP_PCAP 2>/dev/null || true"
	pisu "stat -c '%n %s' $MON_DOWN_PCAP $MON_UP_PCAP 2>/dev/null || true"
	grep -c 'packets dropped by kernel' "$EVIDENCE/mon-down-tcpdump.log" "$EVIDENCE/mon-up-tcpdump.log" 2>/dev/null || true
} > "$EVIDENCE/pcap.txt" 2>&1 || true

{
	echo "== monitor (802.11) frames for the station, per direction window"
	pisu "tshark -r $MON_DOWN_PCAP -Y 'wlan.addr==$STA_MAC' -T fields -e wlan.fc.type_subtype -e wlan.fc.retry -e wlan_radio.11n.mcs_index -e radiotap.ampdu.flags -e frame.len 2>/dev/null" > "$EVIDENCE/air-fields-down.csv" || true
	pisu "tshark -r $MON_UP_PCAP -Y 'wlan.addr==$STA_MAC' -T fields -e wlan.fc.type_subtype -e wlan.fc.retry -e wlan_radio.11n.mcs_index -e radiotap.ampdu.flags -e frame.len 2>/dev/null" > "$EVIDENCE/air-fields-up.csv" || true
	awk -F'\t' 'NF{ n++; if ($1=="0x0019") ba++; if ($2=="1") retry++; if ($4!="") ampdu++ } END { printf "down frames=%d ba=%d retry=%d ampdu=%d\n", n, ba+0, retry+0, ampdu+0 }' "$EVIDENCE/air-fields-down.csv" || true
	awk -F'\t' 'NF{ n++; if ($1=="0x0019") ba++; if ($2=="1") retry++; if ($4!="") ampdu++ } END { printf "up   frames=%d ba=%d retry=%d ampdu=%d\n", n, ba+0, retry+0, ampdu+0 }' "$EVIDENCE/air-fields-up.csv" || true
} > "$EVIDENCE/air-summary.txt" 2>&1 || true

{
	pisu "tshark -r $MON_DOWN_PCAP -Y 'wlan.addr==$STA_MAC && tcp && tcp.len>0' -T fields -e tcp.len 2>/dev/null | awk '{n++; s+=\$1} END {printf \"tcp_frames=%d tcp_bytes=%d\\n\", n, s}'" || true
	pisu "tshark -r $MON_DOWN_PCAP -Y 'wlan.addr==$STA_MAC && udp && udp.length>8' -T fields -e udp.length 2>/dev/null | awk '{n++; s+=\$1-8} END {printf \"udp_frames=%d udp_bytes=%d\\n\", n, s}'" || true
} > "$EVIDENCE/air-payload-down.txt" 2>&1 || true
{
	pisu "tshark -r $MON_UP_PCAP -Y 'wlan.addr==$STA_MAC && tcp && tcp.len>0' -T fields -e tcp.len 2>/dev/null | awk '{n++; s+=\$1} END {printf \"tcp_frames=%d tcp_bytes=%d\\n\", n, s}'" || true
	pisu "tshark -r $MON_UP_PCAP -Y 'wlan.addr==$STA_MAC && udp && udp.length>8' -T fields -e udp.length 2>/dev/null | awk '{n++; s+=\$1-8} END {printf \"udp_frames=%d udp_bytes=%d\\n\", n, s}'" || true
} > "$EVIDENCE/air-payload-up.txt" 2>&1 || true

# --- judge -------------------------------------------------------------------
python3 "$HERE/judge.py" "$SERIAL" --json "$EVIDENCE/judge.json" \
	--expect cases=tick,pcie,iwl7260_ap \
	>"$EVIDENCE/judge.txt" 2>&1 || true

echo "ap-round: evidence -> $EVIDENCE"
tail -5 "$EVIDENCE/judge.txt" 2>/dev/null || true

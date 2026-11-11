#!/bin/bash
# c5-round.sh - one C5/C6 board round: boot the image, capture the air, and
# drive the peer side (AP -> board ping + station dump) while the case's
# window is open.
#
#   tools/rtos/c5-round.sh --evidence runs/<date>-iwl7260-c5-open/clean1
#
# The image must already be uploaded as rtos.bin (boot-rtos.sh does its own
# TFTP put when given --bin; this script calls it with the local build).
#
# Prereqs (checked, not repaired): the bench Pi's AP (wlan2, 192.168.99.1) and
# monitor (wlan1, channel 5) are up.  The peer arm is the AP host pinging the
# board's 192.168.99.2 while the board waits in its 6 s window; the station
# dump is taken at the same moment, before the case's own pings and before the
# image resets.
#
# Outputs, under --evidence:
#   serial.log / transcript.txt / timings.json   (boot-rtos.sh)
#   ap-station.txt                                (iw dev wlan2 station dump)
#   peer-ping.txt                                 (AP -> board ping)
#   air-summary.txt                               (tshark frame counts)
# The pcap itself stays on the Pi in /tmp (air-truth's /tmp rule); the summary
# and the capture metadata are the evidence.

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BOARD="$ROOT/../skills/rk3568-board/scripts"
PI=pi@192.168.0.17
AP_IF=wlan2
MON_IF=wlan1
BOARD_IP=192.168.99.2
PCAP=/tmp/c5-round.pcap

EVIDENCE=
BIN="$ROOT/tests/board/rtos/build/build/rtos-freertos.bin"
TIMEOUT=240

while [ $# -gt 0 ]; do
    case "$1" in
        --evidence) EVIDENCE=${2:?}; shift 2 ;;
        --bin)      BIN=${2:?}; shift 2 ;;
        --timeout)  TIMEOUT=${2:?}; shift 2 ;;
        -h|--help)  sed -n '2,26p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "c5-round: unknown argument $1" >&2; exit 2 ;;
    esac
done
[ -n "$EVIDENCE" ] || { echo "c5-round: --evidence DIR is required" >&2; exit 2; }
mkdir -p "$EVIDENCE"
EVIDENCE=$(cd "$EVIDENCE" && pwd)

# The monitor must exist and be on the AP channel; a capture elsewhere is not
# this round's air truth.
ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" \
    "iw dev $MON_IF info >/dev/null 2>&1 && iw dev $MON_IF info | grep -q 'channel 5'" \
    || { echo "c5-round: $MON_IF is not a monitor on channel 5" >&2; exit 3; }

# 1) put the image where the board's TFTP read finds it, then capture the
#    whole round (the power-cycle + boot + connect + pings fit in 45 s on
#    this bench; the pcap stays in /tmp on the Pi).
"$BOARD/tftp.sh" put "$BIN" --name rtos.bin
ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" \
    "sudo -n timeout 45 tcpdump -i $MON_IF -w $PCAP -U 2>/tmp/c5-round.tcpdump.log" \
    >/dev/null 2>&1 &
CAP_PID=$!
sleep 1

# 2) boot; the evidence directory is also where the anchor is watched.
"$BOARD/boot-rtos.sh" --bin rtos.bin --success "rtos: result" \
    --quiet 0 --timeout "$TIMEOUT" --power-cycle --evidence "$EVIDENCE" \
    >"$EVIDENCE/boot.out" 2>&1 &
BOOT_PID=$!

# 3) wait for the netif anchor (the case prints it right after PASS C5).
for _ in $(seq 1 240); do
    if grep -aq "iwl7260_c5: peer window ms=" "$EVIDENCE/serial.log" 2>/dev/null; then
        break
    fi
    if ! kill -0 "$BOOT_PID" 2>/dev/null; then
        break
    fi
    sleep 0.5
done

if grep -aq "iwl7260_c5: peer window ms=" "$EVIDENCE/serial.log" 2>/dev/null; then
    sleep 0.2
    # 4) the peer arm: the AP host pings the board inside its 6 s window.
    #    The AP's *dynamic* ARP for the board is not answered: this port never
    #    programs the firmware multicast filter (iwl_mvm_recalc_multicast warns
    #    mcast_filter_cmd == NULL), so broadcast frames do not reach the board.
    #    A static neighbour entry isolates the unicast data path; the missing
    #    broadcast ARP is recorded as a known gap, not hidden.
    { echo "== at $(date +%H:%M:%S)"; \
      ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" \
        "sudo -n ip neigh replace $BOARD_IP lladdr 6c:29:95:12:f0:d9 dev $AP_IF; \
         ip neigh show $BOARD_IP; ping -c 5 -W 1 $BOARD_IP"; } \
        >"$EVIDENCE/peer-ping.txt" 2>&1 || true
    # 5) the AP's view of the station, still connected.
    ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" \
        "sudo -n iw dev $AP_IF station dump" >"$EVIDENCE/ap-station.txt" 2>&1 || true
    # 6) the AP's own connect record for this round.
    ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" \
        "sudo -n tail -6 /tmp/KO1-ap.log" >"$EVIDENCE/ap-hostapd-tail.txt" 2>&1 || true
else
    echo "c5-round: no netif anchor; peer arm skipped" >&2
fi

wait "$BOOT_PID" || true
wait "$CAP_PID" || true

# 6) air summary: the frame classes this round must contain.
ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" \
    "sudo -n tshark -r $PCAP -q -z io,phs 2>/dev/null; \
     sudo -n tshark -r $PCAP -Y 'wlan.addr==6c:29:95:12:f0:d9' -T fields \
        -e wlan.fc.type_subtype 2>/dev/null | sort | uniq -c | sort -rn; \
     sudo -n tcpdump -r $PCAP -nn 2>/dev/null | grep -c 'ICMP echo' || true" \
    >"$EVIDENCE/air-summary.txt" 2>&1 || true

echo "c5-round: evidence -> $EVIDENCE"
tail -3 "$EVIDENCE/boot.out" 2>/dev/null || true

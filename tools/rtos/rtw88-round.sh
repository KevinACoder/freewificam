#!/bin/bash
# rtw88-round.sh - one RTL8821CU (rtw88, USB) board round: upload the image,
# boot it, let the case's own anchors drive the peer side, and collect the
# evidence.
#
#   tools/rtos/rtw88-round.sh --mode attach --evidence runs/<date>-rtw88-rtos/c3-1
#   tools/rtos/rtw88-round.sh --mode scan   --evidence runs/<date>-rtw88-rtos/c4-1
#   tools/rtos/rtw88-round.sh --mode sta    --evidence runs/<date>-rtw88-rtos/c5-1
#
# Modes:
#   attach  C3: the USB bring-up, the driver's `Firmware version` line, wlan0
#           and dev_open.  No peer needed.
#   scan    C4: C3 plus the nl80211 scan; the air capture is taken for the whole
#           boot so the probe requests/responses are in the pcap.
#   sta     C5/C6: C3+C4 plus the open association and the ICMP rounds.  On the
#           case's `peer window` anchor the AP host pings the board and its
#           station dump is taken, inside the window the case holds open.
#
# The image must already be built; this script puts it into the Pi's TFTP root
# under --bin (boot-rtos.sh does the TFTP byte check and the crc32 gate).
#
# Outputs under --evidence:
#   serial.log / transcript.txt / timings.json   (boot-rtos.sh)
#   judge.json                                   (tools/rtos/judge.py)
#   ap-station.txt                               (AP's view of the station)
#   peer-ping.txt                                (AP -> board, both payloads)
#   air-summary.txt                              (frame classes from the pcap)
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BOARD="$ROOT/../skills/rk3568-board/scripts"
PI=pi@192.168.0.17
AP_IF=${AP_IF:-wlan2}
MON_IF=${MON_IF:-wlan1}
BOARD_IP=${BOARD_IP:-192.168.99.2}
MODE=attach
BIN="$ROOT/tests/board/rtos/build/build/rtos-freertos.bin"
EVIDENCE=
TIMEOUT=180
POWER_CYCLE=1
PCAP=/tmp/rtw88-round.pcap

while [ $# -gt 0 ]; do
    case "$1" in
        --mode)     MODE=${2:?}; shift 2 ;;
        --evidence) EVIDENCE=${2:?}; shift 2 ;;
        --bin)      BIN=${2:?}; shift 2 ;;
        --timeout)  TIMEOUT=${2:?}; shift 2 ;;
        --no-power-cycle) POWER_CYCLE=0; shift ;;
        -h|--help)  sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "rtw88-round: unknown argument $1" >&2; exit 2 ;;
    esac
done
[ -n "$EVIDENCE" ] || { echo "rtw88-round: --evidence DIR is required" >&2; exit 2; }
case "$MODE" in
    attach|scan|sta) ;;
    *) echo "rtw88-round: --mode must be attach, scan or sta" >&2; exit 2 ;;
esac
mkdir -p "$EVIDENCE"
EVIDENCE=$(cd "$EVIDENCE" && pwd)
TAG=$(basename "$EVIDENCE")
# The capture is evidence, not a scratch file: keep it on the Pi's disk-backed
# home and copy its hash into the evidence directory (the 2026-11-14 rounds
# left it in /tmp and could not re-read it).
PCAP=${PCAP:-/home/pi/rtw88-round-$TAG.pcap}

CASE=rtw88_$MODE
[ "$MODE" = attach ] && CASE=rtw88_c3
[ "$MODE" = scan ] && CASE=rtw88_c4
[ "$MODE" = sta ] && CASE=rtw88_c5

BOOT_ARGS=(--bin "$(basename "$BIN")" --success "rtos: result" --quiet 0
           --timeout "$TIMEOUT" --evidence "$EVIDENCE")
[ "$POWER_CYCLE" = 1 ] && BOOT_ARGS+=(--power-cycle)

# 1) the image, then the capture: the pcap covers the whole boot, because the
#    scan's probe traffic is over in the case's own window.
"$BOARD/tftp.sh" put "$BIN" --name "$(basename "$BIN")" > "$EVIDENCE/tftp.txt"
AP_PCAP=${AP_PCAP:-/home/pi/rtw88-ap-$TAG.pcap}
ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" \
    "sudo -n rm -f $PCAP; sudo -n timeout 120 tcpdump -i $MON_IF -s 0 -w $PCAP -U 2>/tmp/rtw88-tcpdump.log" \
    >/dev/null 2>&1 &
CAP_PID=$!
# The AP's own 802.3 view of the ICMP exchange, in parallel with the monitor.
# This is the ledger that separates "the board's request never reached the AP"
# from "the AP's reply never reached the board": the monitor on this bench sees
# the AP's beacons but not its unicast data (the 2026-11-14 D1 boot and the GT
# round's empty air CSVs), so the AP's kernel is the receiver of record.
ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" \
    "sudo -n rm -f $AP_PCAP; sudo -n timeout 90 tcpdump -i $AP_IF -s 0 -w $AP_PCAP icmp -U 2>/tmp/rtw88-ap-tcpdump.log" \
    >/dev/null 2>&1 &
AP_CAP_PID=$!
sleep 1

# 2) boot
"$BOARD/boot-rtos.sh" "${BOOT_ARGS[@]}" > "$EVIDENCE/boot.out" 2>&1 &
BOOT_PID=$!

if [ "$MODE" = sta ]; then
    # 3) the peer arm inside the case's 6 s window: the AP pings the board and
    #    the AP's station dump is taken while the link is up.
    for _ in $(seq 1 600); do
        if grep -aq "rtw88_c5: peer window ms=" "$EVIDENCE/serial.log" 2>/dev/null; then
            sleep 0.2
            STA_MAC=$(sed -n 's/.*rtw88_c5: wlan0 present=1 ifindex=[0-9]* mac=\([0-9a-f:]*\).*/\1/p' \
                "$EVIDENCE/serial.log" | tail -1)
            { echo "== at $(date +%H:%M:%S) sta_mac=${STA_MAC:-unknown}"
              ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" \
                "sudo -n ip neigh replace $BOARD_IP ${STA_MAC:+lladdr $STA_MAC} dev $AP_IF; \
                 ip neigh show $BOARD_IP; \
                 ping -c 20 -i 0.2 -W 1 $BOARD_IP; \
                 ping -c 20 -i 0.2 -W 1 -s 1400 $BOARD_IP" ; } \
                > "$EVIDENCE/peer-ping.txt" 2>&1 || true
            ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" \
                "sudo -n iw dev $AP_IF station dump" > "$EVIDENCE/ap-station.txt" 2>&1 || true
            # The 8812AU's vendor view: per-station RX/TX packet and byte
            # counters plus the aggregation state (the 2026-11-14 C6T rounds
            # read agg_enable_bitmap from the same file).  iw's station dump on
            # this driver carries only signal/inactive, so this is the AP's
            # per-direction ledger for the station under test.
            ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" \
                "sudo -n cat /proc/net/rtl88XXau/$AP_IF/all_sta_info" \
                > "$EVIDENCE/ap-sta-info.txt" 2>&1 || true
            break
        fi
        if ! kill -0 "$BOOT_PID" 2>/dev/null; then
            break
        fi
        sleep 0.5
    done
fi

wait "$BOOT_PID" || true
wait "$CAP_PID" || true
wait "${AP_CAP_PID:-}" || true

# 4) the air: which frame classes the round carried, from the monitor pcap.
ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" \
    "sudo -n tshark -r $PCAP -T fields -e wlan.fc.type_subtype 2>/dev/null | sort | uniq -c | sort -rn; \
     sudo -n tshark -r $PCAP -Y 'wlan.fc.type_subtype==0x08' -T fields -e frame.len 2>/dev/null | wc -l" \
    > "$EVIDENCE/air-summary.txt" 2>&1 || true

# 4b) the board's own air ledger: rate, MCS, RSSI, retry and subtype of every
#     frame that carries the station's MAC either way.  This is the face the
#     2026-11-14 EHCI round did not have (its pcap was class counts only), so
#     "the board sends at OFDM 6 Mbit/s" or "at MCS7 and -24 dBm" could not be
#     told apart from the AP's single station-dump number.
STA_MAC_FROM_LOG=$(sed -n 's/.*: wlan0 present=1 ifindex=[0-9]* mac=\([0-9a-f:]*\).*/\1/p' \
    "$EVIDENCE/serial.log" 2>/dev/null | tail -1)
AIR_FILTER=""
[ -n "$STA_MAC_FROM_LOG" ] && AIR_FILTER="-Y wlan.addr==$STA_MAC_FROM_LOG"
{
    echo "pcap=$PCAP sta_mac=${STA_MAC_FROM_LOG:-unknown} filter=${AIR_FILTER:-none}"
    ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" \
        "sudo -n sha256sum $PCAP 2>/dev/null; sudo -n stat -c '%n %s' $PCAP 2>/dev/null"
    ssh -o BatchMode=yes -o ConnectTimeout=20 "$PI" \
        "sudo -n tshark -r $PCAP $AIR_FILTER -T fields -e frame.time_relative \
            -e wlan.fc.type_subtype -e wlan.fc.retry -e wlan_radio.data_rate \
            -e wlan_radio.11n.mcs_index -e wlan_radio.signal_dbm \
            -e wlan.ta -e wlan.ra -e frame.len 2>/dev/null" \
        > "$EVIDENCE/air-board.csv" 2>&1 || true
} > "$EVIDENCE/pcap-facts.txt" 2>&1 || true
python3 "$HERE/sta-air.py" "$EVIDENCE" > "$EVIDENCE/air-board.txt" 2>&1 || true

# 4c) the AP-side ICMP ledger: four counters (board->AP request, AP->board
#     reply, AP->board request, board->AP reply).  A ping that the board reports
#     lost is one of these four lines being short.
{
    echo "pcap=$AP_PCAP if=$AP_IF"
    ssh -o BatchMode=yes -o ConnectTimeout=10 "$PI" \
        "sudo -n sha256sum $AP_PCAP 2>/dev/null; sudo -n stat -c '%n %s' $AP_PCAP 2>/dev/null" || true
    ssh -o BatchMode=yes -o ConnectTimeout=20 "$PI" \
        "sudo -n tshark -r $AP_PCAP -Y icmp -T fields -e ip.src -e ip.dst -e icmp.type 2>/dev/null" \
        > "$EVIDENCE/icmp-ap.csv" || true
} > "$EVIDENCE/ap-pcap-facts.txt" 2>&1 || true
python3 "$HERE/icmp-ledger.py" "$EVIDENCE" > "$EVIDENCE/icmp-ledger.txt" 2>&1 || true

# 5) the judge over the captured serial log.
if [ -r "$EVIDENCE/serial.log" ]; then
    python3 "$ROOT/tools/rtos/judge.py" "$EVIDENCE/serial.log" \
        --json "$EVIDENCE/judge.json" --expect "cases=$CASE" \
        > "$EVIDENCE/judge.txt" 2>&1 || true
fi

echo "rtw88-round: evidence -> $EVIDENCE"
tail -4 "$EVIDENCE/boot.out" 2>/dev/null || true

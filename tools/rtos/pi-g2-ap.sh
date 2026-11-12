#!/bin/bash
# pi-g2-ap.sh - the Kali Pi's side of the G2 Pi arm: turn wlan2 from the open
# lab AP into a temporary WPA2-PSK/CCMP AP on the same SSID/channel, keep
# wlan1 as the monitor, and run the peer half of the C6 phases.
#
# Run as root on the Pi.  The host copies it to /tmp and calls
#   sudo -n env PSK=... bash /tmp/pi-g2-ap.sh <subcommand> ...
# `down` restores lab-ap and the standard monitor even after a failed round.
set -u
export PATH=/usr/sbin:/sbin:/usr/bin:/bin

# The band is the one knob the 5 GHz round adds (docs/HANDOFF-5GHZ.md P3/P4):
# BAND=5g makes the AP an 802.11a/n AP on 36/5180 and sets the Pi's regulatory
# domain to US for the duration, because the card's world regdomain marks 5 GHz
# passive and hostapd refuses an AP on a NO_IR channel.  BAND=2g is the bench's
# own G2 configuration and is unchanged.
BAND=${BAND:-2g}
if [ "$BAND" = 5g ]; then
    CH=${CH:-36}
    FREQ=${FREQ:-5180}
    HW_MODE=${HW_MODE:-a}
    REG_SET=${REG_SET:-US}
    REG_RESTORE=${REG_RESTORE:-00}
else
    CH=${CH:-5}
    FREQ=${FREQ:-2432}
    HW_MODE=${HW_MODE:-g}
    REG_SET=${REG_SET:-}
    REG_RESTORE=${REG_RESTORE:-}
fi
SSID=${SSID:-labwifi-ar-ht20s}
PI_IP=${PI_IP:-192.168.99.1}
BOARD_IP=${BOARD_IP:-192.168.99.2}
PSK=${PSK:-rtos-7260-wpa2-psk}
# OPEN=1 makes the AP open (the STA round's P3 arm: the band gate must not be
# able to hide behind a handshake).  The default stays the G2 WPA2 bench.
OPEN=${OPEN:-0}
MON_IF=${MON_IF:-wlan1}
AP_IF=${AP_IF:-wlan2}
CONF=/tmp/g2-hostapd.conf
LOG=/tmp/g2-hostapd.log
PID=/tmp/g2-hostapd.pid

say() { printf '%s\n' "$*"; }

# The regulatory state is recorded before and after, never assumed: a 5 GHz AP
# that fails because the domain was left at 00 is a bench fault, not a finding.
do_reg() {
    say "== reg domain now"
    iw reg get 2>&1 | sed 's/^/   /'
    if [ -n "$REG_SET" ]; then
        say "== set reg $REG_SET (band $BAND)"
        iw reg set "$REG_SET" 2>&1 | sed 's/^/   /'
        sleep 1
        iw reg get 2>&1 | sed 's/^/   /'
    fi
}

reg_restore() {
    [ -n "$REG_RESTORE" ] || return 0
    say "== restore reg $REG_RESTORE"
    iw reg set "$REG_RESTORE" 2>&1 | sed 's/^/   /'
}

do_status() {
    say "== lab-ap: $(systemctl is-active lab-ap 2>/dev/null || true)"
    say "== g2 hostapd: $(cat "$PID" 2>/dev/null || echo none)"
    for i in "$AP_IF" "$MON_IF"; do
        say "== $i"
        ip -4 addr show dev "$i" 2>/dev/null | sed 's/^/   /'
        iw dev "$i" info 2>/dev/null | grep -E 'type|channel|ssid' | sed 's/^/   /'
    done
    say "== iperf: $(iperf -v 2>&1 | head -1)"
}

do_up() {
    say "== stop any previous g2 hostapd"
    if [ -f "$PID" ]; then
        kill "$(cat "$PID")" 2>/dev/null || true
        rm -f "$PID"
    fi
    pkill -f "hostapd.*$CONF" 2>/dev/null || true
    sleep 1
    say "== stop lab-ap"
    systemctl stop lab-ap 2>/dev/null || true
    sleep 1
    do_reg
    command -v nmcli >/dev/null 2>&1 && nmcli device set "$AP_IF" managed no >/dev/null 2>&1 || true
    command -v nmcli >/dev/null 2>&1 && nmcli device set "$MON_IF" managed no >/dev/null 2>&1 || true
    sleep 1
    # wlan3 carries a stale 192.168.99.50/24 from an earlier line; it must not
    # win the route/ARP for the board.
    ip route del 192.168.99.0/24 dev wlan3 2>/dev/null || true
    ip addr flush dev "$AP_IF" 2>/dev/null || true
    ip link set "$AP_IF" up
    ip addr add "$PI_IP/24" dev "$AP_IF" 2>/dev/null || true
    cat > "$CONF" <<EOF
interface=$AP_IF
driver=nl80211
ssid=$SSID
hw_mode=$HW_MODE
channel=$CH
ieee80211n=1
ht_capab=[SHORT-GI-20]
wmm_enabled=1
auth_algs=1
ignore_broadcast_ssid=0
EOF
    if [ "$OPEN" = 1 ]; then
        say "== open AP (no RSN)"
    else
        cat >> "$CONF" <<EOF
wpa=2
wpa_key_mgmt=WPA-PSK
wpa_pairwise=CCMP
rsn_pairwise=CCMP
wpa_passphrase=$PSK
EOF
    fi
    chmod 600 "$CONF"
    : > "$LOG"
    rm -f "$PID"
    say "== start hostapd $AP_IF hw_mode=$HW_MODE ch$CH ($FREQ MHz) open=$OPEN"
    hostapd -B -P "$PID" -f "$LOG" "$CONF" || { say "hostapd start failed"; tail -20 "$LOG"; return 1; }
    sleep 2
    ip link set "$MON_IF" down 2>/dev/null || true
    iw dev "$MON_IF" set type monitor 2>/dev/null || true
    ip link set "$MON_IF" up
    iw dev "$MON_IF" set channel "$CH" 2>/dev/null || true
    do_status
}

do_down() {
    say "== stop g2 hostapd"
    if [ -f "$PID" ]; then
        kill "$(cat "$PID")" 2>/dev/null || true
        rm -f "$PID"
    fi
    pkill -f "hostapd.*$CONF" 2>/dev/null || true
    sleep 1
    ip addr flush dev "$AP_IF" 2>/dev/null || true
    ip link set "$AP_IF" down 2>/dev/null || true
    say "== restart lab-ap"
    systemctl restart lab-ap 2>/dev/null || true
    sleep 1
    reg_restore
    ip link set "$MON_IF" down 2>/dev/null || true
    iw dev "$MON_IF" set type monitor 2>/dev/null || true
    ip link set "$MON_IF" up
    iw dev "$MON_IF" set channel 5 2>/dev/null || true
    do_status
}

do_station() { iw dev "$AP_IF" station dump 2>&1; }
do_log() { tail -80 "$LOG" 2>/dev/null || true; }
do_scan() { ip link set "$MON_IF" up; iw dev "$MON_IF" scan freq "$FREQ" 2>&1; }

servers_stop() {
    pkill -x iperf 2>/dev/null || true
    pkill -f 'udp-sink' 2>/dev/null || true
    sleep 1
}

do_server_tcp() { local out=${1:?out} secs=${2:-25}; : > "$out"; timeout "$secs" iperf -s -p 5001 -i 5 >>"$out" 2>&1 || true; }
do_server_udp() { local out=${1:?out} secs=${2:-25}; : > "$out"; timeout "$secs" iperf -s -u -p 5002 -i 5 >>"$out" 2>&1 || true; }
do_sink() { local py=${1:?script} out=${2:?out} secs=${3:-25}; : > "$out"; python3 "$py" --port 5002 --seconds "$secs" >>"$out" 2>&1 || true; }
do_client_tcp() { iperf -c "$BOARD_IP" -p 5001 -t "${1:-28}" -i 5 2>&1; }
do_client_udp() { iperf -c "$BOARD_IP" -p 5002 -u -b 40M -t "${1:-18}" -i 5 2>&1; }

cmd=${1:-status}; [ $# -gt 0 ] && shift
case "$cmd" in
    status) do_status ;;
    up) do_up ;;
    down) do_down ;;
    station) do_station ;;
    log) do_log ;;
    scan) do_scan ;;
    reg) do_reg ;;
    reg-restore) reg_restore ;;
    servers-stop) servers_stop ;;
    server-tcp) do_server_tcp "$@" ;;
    server-udp) do_server_udp "$@" ;;
    sink) do_sink "$@" ;;
    client-tcp) do_client_tcp "$@" ;;
    client-udp) do_client_udp "$@" ;;
    *) echo "pi-g2-ap: unknown command $cmd" >&2; exit 2 ;;
esac

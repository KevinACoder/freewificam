#!/bin/bash
# pi-ap.sh - the Kali Pi's side of the AC 7260 AP round: turn the bench from
# "8812AU AP + MT7921AU monitor" into "MT7921AU STA + 8812AU monitor" and back,
# and run the peer half of the phase table.
#
# Run as root on the Pi (the host copies it to /tmp and calls
# `sudo -n bash /tmp/pi-ap.sh <subcommand> ...`).  Every subcommand prints what
# it did; the host redirects the output into its evidence directory.
#
#   status                 what the bench looks like now (gate record)
#   up                     stop lab-ap; wlan2 -> monitor ch$CH; wlan1 -> managed
#                          STA with $PI_IP/24; power save off; NM out of the way
#   down                   restore the standard bench (lab-ap + wlan1 monitor ch5)
#   scan                   `iw dev wlan1 scan` on the AP's channel (A1 evidence)
#   join                   `iw dev wlan1 connect -w` to $SSID (A2 evidence)
#   link                   `iw dev wlan1 link` + station dump
#   ping                   A3: 20/20 ICMP + the dynamic-ARP gate (neighbour
#                          deleted, one ping, `ip neigh show` must be REACHABLE)
#   mac                    print wlan1's MAC
#   server-tcp <out> <sec> iperf2 TCP server for one down phase (foreground,
#                          bounded; the GT arm's receiver)
#   server-udp <out> <sec> iperf2 UDP server for one down phase (foreground,
#                          bounded; the GT arm's receiver)
#   sink <py> <out> <sec>  the raw-blast counting sink (RTOS arm's down UDP)
#   client-tcp <sec>       iperf2 TCP client (up phase; board is the receiver)
#   client-udp <sec>       iperf2 UDP client (up phase)
#   servers-stop           kill iperf/sink from an earlier phase
#
# The standard bench's lease is restored by `down` even when a round fails; the
# host calls it from a trap.
set -u
export PATH=/usr/sbin:/sbin:/usr/bin:/bin

CH=${CH:-11}
FREQ=${FREQ:-2462}
SSID=${SSID:-rtos-7260-ap}
PI_IP=${PI_IP:-192.168.99.2}
AP_IP=${AP_IP:-192.168.99.1}
PSK=${PSK:-rtos-7260-wpa2-psk}
WPA_LOG=${WPA_LOG:-/tmp/pi-ap-wpa.log}
WPA_CTRL=${WPA_CTRL:-/tmp/pi-ap-wpa-ctrl}
STA_IF=${STA_IF:-wlan1}
MON_IF=${MON_IF:-wlan2}
MON_RESTORE_CH=${MON_RESTORE_CH:-5}
NM_MARK=/tmp/pi-ap-nm-unmanaged
RPF_MARK=/tmp/pi-ap-rpfilter-wlan1

say() { printf '%s\n' "$*"; }

nm_release() {
    command -v nmcli >/dev/null 2>&1 || return 0
    for i in "$@"; do
        nmcli device set "$i" managed no >/dev/null 2>&1 || true
        grep -qx "$i" "$NM_MARK" 2>/dev/null || echo "$i" >> "$NM_MARK"
        say "nm: $i managed=no"
    done
}

# lab-ap's ExecStop hands wlan2 back to NetworkManager (lab-ap-down.sh:
# `nmcli device set wlan2 managed yes`) after hostapd exits.  Wait until NM
# really lets go before changing the type, or NM re-applies managed mode under
# the monitor and the channel is tuned by a device we no longer own.
nm_wait_unmanaged() {
    command -v nmcli >/dev/null 2>&1 || return 0
    local n=0
    while [ "$n" -lt 20 ]; do
        nmcli -t -f DEVICE,STATE device status 2>/dev/null | grep -q "^$1:unmanaged$" && return 0
        n=$((n + 1))
        sleep 0.5
    done
    say "nm: $1 still not unmanaged"
    return 1
}

nm_restore() {
    [ -f "$NM_MARK" ] || return 0
    command -v nmcli >/dev/null 2>&1 || return 0
    while IFS= read -r i; do
        [ -n "$i" ] || continue
        nmcli device set "$i" managed yes >/dev/null 2>&1 || true
        say "nm: $i managed=yes"
    done < "$NM_MARK"
    rm -f "$NM_MARK"
}

flush_if() {
    ip addr flush dev "$1" 2>/dev/null || true
    ip link set "$1" down 2>/dev/null || true
}

# --- status ---------------------------------------------------------------
# The regulatory record: the AP round's 5 GHz arm needs the Pi's STA on 5180,
# and the value before/after is evidence (the world domain marks 5 GHz passive;
# the bench's US domain does not).
do_reg_get() { iw reg get 2>&1; }
do_reg_set() { local cc=${1:?country}; iw reg set "$cc" 2>&1; sleep 1; iw reg get 2>&1; }

do_status() {
    say "== lab-ap: $(systemctl is-active lab-ap 2>/dev/null || true)"
    for i in "$STA_IF" "$MON_IF"; do
        say "== $i: $(cat /sys/class/net/$i/address 2>/dev/null || echo none)"
        ip -4 addr show dev "$i" 2>/dev/null | sed 's/^/   /'
        iw dev "$i" info 2>/dev/null | sed 's/^/   /'
    done
    say "== route 192.168.99.0/24"
    ip route show 192.168.99.0/24 2>/dev/null | sed 's/^/   /'
    say "== route to the AP"
    ip route get "$AP_IP" 2>/dev/null | sed 's/^/   /'
    say "== rp_filter $STA_IF=$(cat /proc/sys/net/ipv4/conf/$STA_IF/rp_filter 2>/dev/null)"
    say "== nmcli"
    command -v nmcli >/dev/null 2>&1 && nmcli -t -f DEVICE,STATE,CONNECTION device status 2>/dev/null | sed 's/^/   /' || say "   (no nmcli)"
    say "== iperf procs"
    pgrep -a -x iperf 2>/dev/null | sed 's/^/   /' || say "   none"
    pgrep -af 'udp-sink' 2>/dev/null | sed 's/^/   /' || true
    say "== iperf: $(iperf -v 2>&1 | head -1)"
}

# The STA side of the WPA2 round: one supplicant on wlan1, no NM, no ctrl
# daemon beyond wpa_cli.  A leftover supplicant from the previous arm is the
# one process that can make a fresh join look like a flake.
stop_wpa() {
    pkill -f "wpa_supplicant.*$STA_IF" 2>/dev/null || true
    sleep 1
}

# --- up: the AP round's bench ---------------------------------------------
do_up() {
    stop_wpa
    say "== stop lab-ap"
    systemctl stop lab-ap 2>/dev/null || true
    sleep 1
    say "lab-ap=$(systemctl is-active lab-ap 2>/dev/null || echo unknown)"

    # Take both interfaces away from NM first: the unit's ExecStop just gave
    # wlan2 back, and NM would otherwise re-manage it after our type change.
    nm_release "$STA_IF" "$MON_IF"
    nm_wait_unmanaged "$MON_IF" || true
    nm_wait_unmanaged "$STA_IF" || true
    sleep 1

    flush_if "$MON_IF"
    ip addr flush dev "$MON_IF" 2>/dev/null || true

    say "== $MON_IF -> monitor ch$CH"
    iw dev "$MON_IF" set type monitor 2>&1 || say "set type monitor failed"
    ip link set "$MON_IF" up
    iw dev "$MON_IF" set channel "$CH" 2>&1 || say "set channel $CH failed"
    sleep 1
    if ! iw dev "$MON_IF" info 2>/dev/null | grep -q 'type monitor'; then
        say "monitor type did not stick; one retry"
        ip link set "$MON_IF" down
        iw dev "$MON_IF" set type monitor 2>&1 || true
        ip link set "$MON_IF" up
        iw dev "$MON_IF" set channel "$CH" 2>&1 || true
        sleep 1
    fi
    iw dev "$MON_IF" info | grep -E 'type|channel' || true

    say "== $STA_IF -> managed STA $PI_IP/24"
    ip link set "$STA_IF" down 2>/dev/null || true
    iw dev "$STA_IF" set type managed 2>&1 || say "set type managed failed"
    ip link set "$STA_IF" up
    iw dev "$STA_IF" set power_save off 2>&1 || say "power_save off failed"
    ip addr flush dev "$STA_IF" 2>/dev/null || true
    ip addr add "$PI_IP/24" dev "$STA_IF" 2>&1 || say "addr add failed"

    # A leftover connected route from lab-ap on wlan2 is one contamination;
    # the other is another line's stale 192.168.99.0/24 (2026-11-11: wlan3
    # 192.168.99.50, linkdown).  With rp_filter=2 the Pi then answered ARP for
    # the AP out of the dead interface and dropped the AP's unicast: the
    # monitor saw the AP's ARP requests and SYN retries, never a reply.  Pin
    # the AP's address to the STA interface and relax rp_filter for the round.
    ip route del 192.168.99.0/24 dev "$MON_IF" 2>/dev/null || true
    ip route replace "$AP_IP/32" dev "$STA_IF" src "$PI_IP" 2>/dev/null || \
        say "route $AP_IP/32 via $STA_IF failed"
    rpf=/proc/sys/net/ipv4/conf/$STA_IF/rp_filter
    if [ -r "$rpf" ]; then
        cat "$rpf" > "$RPF_MARK" 2>/dev/null || true
        echo 0 > "$rpf" 2>/dev/null || true
        say "rp_filter $STA_IF: $(cat "$RPF_MARK" 2>/dev/null) -> $(cat "$rpf" 2>/dev/null)"
    fi
    if ! ip route get "$AP_IP" 2>/dev/null | grep -q "dev $STA_IF"; then
        say "route to the AP is not on $STA_IF (the one-way trap):"
        ip route get "$AP_IP" 2>&1
        return 1
    fi
    say "route to AP: $(ip route get "$AP_IP" 2>/dev/null | head -1)"
    do_status
}

# --- down: restore the standard bench --------------------------------------
do_down() {
    say "== teardown"
    stop_wpa
    ip route del "$AP_IP/32" dev "$STA_IF" 2>/dev/null || true
    if [ -r "$RPF_MARK" ]; then
        rpf=/proc/sys/net/ipv4/conf/$STA_IF/rp_filter
        cat "$RPF_MARK" > "$rpf" 2>/dev/null || true
        say "rp_filter $STA_IF restored to $(cat "$rpf" 2>/dev/null)"
        rm -f "$RPF_MARK"
    fi
    iw dev "$STA_IF" disconnect 2>/dev/null || true
    ip addr flush dev "$STA_IF" 2>/dev/null || true
    ip link set "$STA_IF" down 2>/dev/null || true
    iw dev "$STA_IF" set type monitor 2>&1 || say "sta -> monitor failed"
    ip link set "$STA_IF" up
    iw dev "$STA_IF" set channel "$MON_RESTORE_CH" 2>&1 || say "monitor ch$MON_RESTORE_CH failed"
    ip link set "$MON_IF" down 2>/dev/null || true
    iw dev "$MON_IF" set type managed 2>&1 || say "monitor -> managed failed"
    say "== restart lab-ap"
    # restart, not start: the unit is a RemainAfterExit oneshot, so `start`
    # on an already-active unit is a no-op and leaves wlan2 on whatever type
    # this script put it (2026-11-11: wlan2 stayed managed with a stale
    # hostapd holding it).
    systemctl restart lab-ap 2>/dev/null || true
    say "lab-ap=$(systemctl is-active lab-ap 2>/dev/null || echo unknown)"
    # The standard bench keeps both radios out of NetworkManager: restoring
    # "managed yes" made NM take wlan1 back as a managed station, so the
    # monitor role was lost after every round (2026-11-11).  Re-release them
    # and put wlan1 back on its monitor channel after lab-ap has run.
    nm_release "$STA_IF" "$MON_IF"
    ip link set "$STA_IF" down 2>/dev/null || true
    iw dev "$STA_IF" set type monitor 2>/dev/null || true
    ip link set "$STA_IF" up
    iw dev "$STA_IF" set channel "$MON_RESTORE_CH" 2>/dev/null || true
    rm -f "$NM_MARK"
    do_status
}

# --- A1: scan --------------------------------------------------------------
do_scan() {
    ip link set "$STA_IF" up
    iw dev "$STA_IF" scan freq "$FREQ" 2>&1
}

# The AP's channel is the case's argument, but if the driver's channel switch
# did not take effect the beacon is elsewhere: a full scan is the cheapest way
# to tell "no beacon" from "beacon on another channel".
do_scan_all() {
    ip link set "$STA_IF" up
    iw dev "$STA_IF" scan 2>&1
}

# --- A2: join --------------------------------------------------------------
do_join() {
    local i out
    ip link set "$STA_IF" up
    for i in 1 2 3; do
        out=$(iw dev "$STA_IF" connect -w "$SSID" "$FREQ" 2>&1)
        printf '%s\n' "$out"
        case "$out" in
        *"Not connected"*) ;;
        *) break ;;
        esac
        # -95 (EOPNOTSUPP) is the type being wrong, not the AP being absent:
        # the interface reports managed but the wdev is still in its previous
        # mode, and nl80211 refuses CONNECT for anything but a station.  A
        # down/type/up round trip is the fix; retrying connect alone is not.
        case "$out" in
        *"-95"*|*"not supported"*)
            say "join attempt $i: EOPNOTSUPP, re-creating the station interface"
            ip link set "$STA_IF" down 2>/dev/null || true
            iw dev "$STA_IF" set type managed 2>&1 || true
            ip link set "$STA_IF" up
            ip addr flush dev "$STA_IF" 2>/dev/null || true
            ip addr add "$PI_IP/24" dev "$STA_IF" 2>&1 || true
            ;;
        *)
            say "join attempt $i failed; retry in 2 s"
            ;;
        esac
        sleep 2
    done
    sleep 1
    do_link
}

do_link() {
    iw dev "$STA_IF" link 2>&1
}

# --- A2 (WPA2): wpa_supplicant join ---------------------------------------
do_join_wpa() {
    local i rc=1

    ip link set "$STA_IF" up
    stop_wpa
    mkdir -p "$WPA_CTRL"
    : > "$WPA_LOG"
    cat > /tmp/pi-ap-wpa.conf <<EOF
ctrl_interface=$WPA_CTRL
network={
    ssid="$SSID"
    psk="$PSK"
    key_mgmt=WPA-PSK
    proto=RSN
    pairwise=CCMP
    group=CCMP
    ieee80211w=0
    scan_ssid=1
}
EOF
    say "== wpa_supplicant $STA_IF ssid=$SSID"
    wpa_supplicant -B -i "$STA_IF" -c /tmp/pi-ap-wpa.conf -f "$WPA_LOG" \
        -D nl80211 2>&1 || say "wpa_supplicant start failed"
    for i in $(seq 1 40); do
        if grep -q 'WPA: Key negotiation completed' "$WPA_LOG" 2>/dev/null; then
            rc=0
            break
        fi
        sleep 0.5
    done
    say "== wpa_supplicant negotiation rc=$rc after ~$(($i / 2)) s"
    sleep 1
    do_wpa_status
    do_link
}

do_wpa_status() {
    wpa_cli -p "$WPA_CTRL" -i "$STA_IF" status 2>&1 || true
    say "== wpa log (tail)"
    tail -30 "$WPA_LOG" 2>/dev/null || true
}

do_wpa_log() {
    cat "$WPA_LOG" 2>/dev/null || true
}

# --- A3: ping + dynamic ARP ------------------------------------------------
do_ping() {
    say "== icmp 56 B 20/20"
    ping -I "$STA_IF" -c 20 -i 0.2 -W 2 "$AP_IP" 2>&1 || true
    say "== icmp 1400 B 20/20"
    ping -I "$STA_IF" -c 20 -i 0.2 -W 2 -s 1400 "$AP_IP" 2>&1 || true
    say "== dynamic arp: neighbour deleted, one ping, state must rebuild"
    ip neigh del "$AP_IP" dev "$STA_IF" 2>/dev/null || true
    ip neigh show "$AP_IP" dev "$STA_IF" 2>&1 || true
    ping -I "$STA_IF" -c 1 -W 2 "$AP_IP" >/dev/null 2>&1 || true
    ip neigh show "$AP_IP" dev "$STA_IF" 2>&1 || true
}

do_mac() { cat "/sys/class/net/$STA_IF/address"; }

# --- peer half of the phase table -----------------------------------------
servers_stop() {
    pkill -x iperf 2>/dev/null || true
    pkill -f 'udp-sink' 2>/dev/null || true
    sleep 1
}

do_server_tcp() {	# $1 = out file, $2 = seconds
    local out=${1:?out} secs=${2:-25}
    : > "$out"
    timeout "$secs" iperf -s -p 5001 -i 5 >> "$out" 2>&1 || true
}

do_server_udp() {	# $1 = out file, $2 = seconds
    local out=${1:?out} secs=${2:-25}
    : > "$out"
    timeout "$secs" iperf -s -u -p 5002 -i 5 >> "$out" 2>&1 || true
}

do_sink() {		# $1 = sink script, $2 = out, $3 = seconds
    local py=${1:?script} out=${2:?out} secs=${3:-25}
    : > "$out"
    python3 "$py" --port 5002 --seconds "$secs" >> "$out" 2>&1 || true
}

do_client_tcp() {	# $1 = seconds
    iperf -c "$AP_IP" -p 5001 -t "${1:-28}" -i 5 2>&1
}

do_client_udp() {	# $1 = seconds
    iperf -c "$AP_IP" -p 5002 -u -b 40M -t "${1:-18}" -i 5 2>&1
}

cmd=${1:-status}
[ $# -gt 0 ] && shift
case "$cmd" in
    status)     do_status ;;
    reg-get)    do_reg_get ;;
    reg-set)    do_reg_set "${1:-US}" ;;
    up)         do_up ;;
    down)       do_down ;;
    scan)       do_scan ;;
    scan-all)   do_scan_all ;;
    join)       do_join ;;
    join-wpa)   do_join_wpa ;;
    wpa-status) do_wpa_status ;;
    wpa-log)    do_wpa_log ;;
    link)       do_link ;;
    ping)       do_ping ;;
    mac)        do_mac ;;
    server-tcp) do_server_tcp "$@" ;;
    server-udp) do_server_udp "$@" ;;
    sink)       do_sink "$@" ;;
    client-tcp) do_client_tcp "$@" ;;
    client-udp) do_client_udp "$@" ;;
    servers-stop) servers_stop ;;
    *) echo "pi-ap: unknown command $cmd" >&2; exit 2 ;;
esac

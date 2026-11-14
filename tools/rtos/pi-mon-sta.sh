#!/bin/bash
# pi-mon-sta.sh - the Kali Pi's client half of the AC 7260 monitor round.
#
# The standard bench is "8812AU wlan2 = AP labwifi-ar-ht20s ch5 open,
# MT7921AU wlan1 = monitor ch5".  The monitor round keeps the AP exactly as it
# is (lab-ap stays up: it is the beacon source both arms count) and only turns
# wlan1 into a client, so every window has a station's auth/assoc/probe/data
# frames as well as the AP's beacons.
#
# Run as root on the Pi (the host copies it to /tmp and calls
# `sudo -n bash /tmp/pi-mon-sta.sh <subcommand>`).
#
#   status            what the bench looks like now (gate record)
#   up                wlan1 -> managed, join $SSID (open), $STA_IP/24, ping gate
#   loop <seconds>    reconnect + ping + probe scan until the deadline; the
#                     capture window's control source (the frames a monitor
#                     must see, not only the AP's beacons)
#   down              restore the standard bench: wlan1 -> monitor ch5
#
# The AP interface is never touched: `down` restores only wlan1 and the
# one-way traps pi-ap.sh records (NM managed, rp_filter), so a failed round
# still leaves the standard bench whole.
set -u
export PATH=/usr/sbin:/sbin:/usr/bin:/bin

STA_IF=${STA_IF:-wlan1}
AP_IF=${AP_IF:-wlan2}
SSID=${SSID:-labwifi-ar-ht20s}
CH=${CH:-5}
FREQ=${FREQ:-2432}
STA_IP=${STA_IP:-192.168.99.50}
AP_IP=${AP_IP:-192.168.99.1}
NM_MARK=/tmp/pi-mon-nm-unmanaged
RPF_MARK=/tmp/pi-mon-rpfilter-wlan1

say() { printf '%s\n' "$*"; }
pi_ok() { [ "$(id -u)" = 0 ] || { say "must run as root"; exit 2; }; }

nm_release() {
    command -v nmcli >/dev/null 2>&1 || return 0
    for i in "$@"; do
        nmcli device set "$i" managed no >/dev/null 2>&1 || true
        grep -qx "$i" "$NM_MARK" 2>/dev/null || echo "$i" >> "$NM_MARK"
        say "nm: $i managed=no"
    done
}

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
        # The standard bench's wlan1 is an *unmanaged* monitor.  Restoring it
        # to managed makes NetworkManager turn it back into a managed client
        # right after the type is set, and the "restored" line then lies
        # (2026-11-14 monitor round: nm managed=yes, type managed).  Hand it
        # back unmanaged and set the monitor type after this.
        nmcli device set "$i" managed no >/dev/null 2>&1 || true
        say "nm: $i managed=no"
    done < "$NM_MARK"
    rm -f "$NM_MARK"
}

stop_sta_supplicants() {
    # Only wlan1's: lab-ap owns wlan2's hostapd, and a global pkill would take
    # the AP's own bench tooling with it.
    pkill -f "wpa_supplicant.*$STA_IF" 2>/dev/null || true
    pkill -f "wpa_supplicant.*-i$STA_IF" 2>/dev/null || true
}

do_status() {
    say "== lab-ap: $(systemctl is-active lab-ap 2>/dev/null || true)"
    for i in "$AP_IF" "$STA_IF"; do
        say "== $i: $(cat /sys/class/net/$i/address 2>/dev/null || echo none)"
        ip -4 addr show dev "$i" 2>/dev/null | sed 's/^/   /'
        iw dev "$i" info 2>/dev/null | sed 's/^/   /'
    done
    say "== AP address is local (the Pi is the AP); traffic is broadcast"
    say "== rp_filter $STA_IF=$(cat /proc/sys/net/ipv4/conf/$STA_IF/rp_filter 2>/dev/null)"
    command -v nmcli >/dev/null 2>&1 && \
        nmcli -t -f DEVICE,STATE,CONNECTION device status 2>/dev/null | sed 's/^/   /' || true
    pgrep -af 'wpa_supplicant' 2>/dev/null | sed 's/^/   /' || say "   (no wpa_supplicant)"
}

do_up() {
    pi_ok
    stop_sta_supplicants
    nm_release "$STA_IF"
    nm_wait_unmanaged "$STA_IF" || true
    ip addr flush dev "$STA_IF" 2>/dev/null || true
    ip link set "$STA_IF" down 2>/dev/null || true
    iw dev "$STA_IF" set type managed 2>&1 || say "set type managed failed"
    ip link set "$STA_IF" up
    iw dev "$STA_IF" set power_save off 2>/dev/null || true
    ip addr add "$STA_IP/24" dev "$STA_IF" 2>/dev/null || say "addr add failed"
    # The monitor round's AP is open; no supplicant is involved.
    iw dev "$STA_IF" connect -w "$SSID" 2>&1 || say "connect failed"
    sleep 1
    ip route replace "$AP_IP/32" dev "$STA_IF" src "$STA_IP" 2>/dev/null || \
        say "route $AP_IP/32 failed"
    rpm=/proc/sys/net/ipv4/conf/$STA_IF/rp_filter
    if [ -r "$rpm" ]; then
        cat "$rpm" > "$RPF_MARK" 2>/dev/null || true
        echo 0 > "$rpm" 2>/dev/null || true
    fi
    # The Pi *is* the AP (192.168.99.1 is local), so an ICMP to the AP address
    # never reaches the air and cannot gate the join.  The gate is the link's
    # own report; the traffic that makes the window representative is a
    # broadcast ICMP (it leaves the STA on the air).
    iw dev "$STA_IF" link 2>/dev/null | sed 's/^/   /'
    local joined
    joined=$(iw dev "$STA_IF" link 2>/dev/null | grep -c '^Connected')
    ping -b -I "$STA_IF" -c 2 -W 1 192.168.99.255 >/dev/null 2>&1 || true
    say "STA_UP=$([ "${joined:-0}" -ge 1 ] && echo 1 || echo 0) ssid=$SSID"
}

do_loop() {
    pi_ok
    local seconds=${1:?seconds}
    local end=$(( $(date +%s) + seconds ))
    local n=0
    say "loop: ${seconds}s reconnect/ping/probe on $STA_IF ($SSID)"
    while [ "$(date +%s)" -lt "$end" ]; do
        n=$((n + 1))
        iw dev "$STA_IF" disconnect >/dev/null 2>&1 || true
        sleep 1
        iw dev "$STA_IF" connect -w "$SSID" >/dev/null 2>&1 || true
        # Broadcast leaves this station on the air; a unicast to the Pi's own
        # 192.168.99.1 would route to lo and make the window look empty.
        ping -b -I "$STA_IF" -c 2 -i 0.3 -W 1 192.168.99.255 >/dev/null 2>&1 || true
        if [ $((n % 3)) = 0 ]; then
            iw dev "$STA_IF" scan freq "$FREQ" >/dev/null 2>&1 || true
        fi
        say "loop: round=$n at=$(date +%s)"
        sleep 2
    done
    say "loop: done rounds=$n"
}

do_down() {
    local rx
    iw dev "$STA_IF" disconnect >/dev/null 2>&1 || true
    ip addr flush dev "$STA_IF" 2>/dev/null || true
    ip link set "$STA_IF" down 2>/dev/null || true
    # Hand the interface back to the unmanaged state *before* the type change:
    # nm_restore no longer sets managed=yes, so NetworkManager will not flip it
    # back to a client after the monitor type is set.
    nm_restore
    iw dev "$STA_IF" set type monitor 2>&1 || say "set type monitor failed"
    ip link set "$STA_IF" up 2>/dev/null || true
    iw dev "$STA_IF" set channel "$CH" 2>&1 || say "set channel $CH failed"
    if [ -f "$RPF_MARK" ]; then
        cat "$RPF_MARK" > /proc/sys/net/ipv4/conf/$STA_IF/rp_filter 2>/dev/null || true
        rm -f "$RPF_MARK"
    fi
    iw dev "$STA_IF" info 2>/dev/null | grep -E 'type|channel' | sed 's/^/   /' || true
    say "restored: $STA_IF monitor ch$CH, lab-ap untouched"
}

case "${1:-status}" in
    status) do_status ;;
    up)     do_up ;;
    loop)   shift; do_loop "${1:-180}" ;;
    down)   do_down ;;
    *) say "usage: $0 status|up|loop <seconds>|down"; exit 2 ;;
esac

#!/bin/sh
# @file    check.sh
# @brief   Wrapper for the WLAN bridge chain suite: run the binary, map its
#          anchor lines to container cases, then prove the suite can go red.
# @author  zhugengyu
# @date    08.10.2026
#
# The binary (tests/host/wlan-bridge-test) links the real chain and prints one
# `WLANBRIDGE: <id> PASS|FAIL <detail>` line per case plus a
# `WLANBRIDGE: SUITE ...` sentinel. This wrapper turns those into cases and runs
# each --fault mode, which must fail while naming the case it invalidated: a
# suite that cannot go red proves nothing.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
log_dir=$root/tools/mock/build/wlan-bridge-test
mkdir -p "$log_dir"

bin=$root/tests/host/wlan-bridge-test/build/wlan-bridge-test
if [ ! -x "$bin" ]; then
    echo "MOCK-BUILD wlan-bridge-test: $bin was not built" >&2
    exit 2
fi

failures=0

case_line() {
    printf 'CASE wlan-bridge-test/%s %s\n' "$1" "$2"
    [ "$2" = "PASS" ] || failures=$((failures + 1))
}

# ---- the suite itself ---------------------------------------------------------

if timeout 180 "$bin" > "$log_dir/stream.log" 2>&1; then
    pass=$(grep -c '^WLANBRIDGE: B[0-9]* PASS' "$log_dir/stream.log" || true)
    if [ "$pass" = 19 ] &&
        grep -q '^WLANBRIDGE: SUITE PASS cases=19$' "$log_dir/stream.log" &&
        ! grep -q '^WLANBRIDGE: B[0-9]* FAIL' "$log_dir/stream.log"; then
        case_line attach-chain PASS
        case_line scan-order-and-walk PASS
        case_line scan-results PASS
        case_line join-chain PASS
        case_line eapol-routing PASS
        case_line data-path PASS
        case_line ccmp-roundtrip PASS
        case_line driver-link-closure PASS
        case_line balance-and-order PASS
        case_line presentation-layer PASS
        case_line supplicant-backend PASS
        case_line parked-resweep-probes PASS
        case_line midsweep-join-recovery PASS
        case_line assoc-wiring PASS
        case_line uplink-key-selection PASS
        case_line rx-status-flags PASS
        case_line tx-headroom PASS
        case_line rx-channel PASS
        case_line sweep-window-not-visible-early PASS
    else
        case_line attach-chain FAIL
        case_line tx-headroom FAIL
        case_line rx-channel FAIL
        case_line sweep-window-not-visible-early FAIL
        echo "wlan-bridge-test: the run did not report nineteen passing cases ($pass)" >&2
        grep '^WLANBRIDGE' "$log_dir/stream.log" | tail -12 >&2 || true
    fi
else
    case_line attach-chain FAIL
    case_line tx-headroom FAIL
    case_line rx-channel FAIL
    case_line sweep-window-not-visible-early FAIL
    echo "wlan-bridge-test: the run failed, see $log_dir/stream.log" >&2
    grep '^WLANBRIDGE' "$log_dir/stream.log" | tail -8 >&2 || true
fi

# ---- the red controls (each must fail, naming its case) -----------------------

# fault -> the case the fault must invalidate
for pair in no-filter:B2 tamper-auth:B4 double-free-skb:B9 no-manual:B11 no-rearm:B12; do
    fault=${pair%%:*}
    want=${pair##*:}
    if timeout 120 "$bin" --fault "$fault" > "$log_dir/fault-$fault.log" 2>&1; then
        case_line "red-$fault" FAIL
        echo "wlan-bridge-test: the $fault control passed (it must fail)" >&2
    elif grep -q "^WLANBRIDGE: $want FAIL" "$log_dir/fault-$fault.log"; then
        case_line "red-$fault" PASS
    else
        case_line "red-$fault" FAIL
        echo "wlan-bridge-test: the $fault control failed without naming $want" >&2
        grep '^WLANBRIDGE' "$log_dir/fault-$fault.log" | tail -4 >&2 || true
    fi
done

# an unknown fault name is a usage error, not a red mode
if timeout 20 "$bin" --fault bogus > "$log_dir/fault-bogus.log" 2>&1; then
    case_line fault-usage FAIL
else
    if grep -q 'unknown fault' "$log_dir/fault-bogus.log"; then
        case_line fault-usage PASS
    else
        case_line fault-usage FAIL
        echo "wlan-bridge-test: an unknown fault name must be a usage error" >&2
    fi
fi

# ---- the headroom red control -------------------------------------------------

# The bridge owes the driver hw->extra_tx_headroom bytes in front of every frame
# it hands over, because the driver builds its transmit descriptor by pushing it
# on (rtw89 pushes 48). The control builds the same chain against a copy of the
# bridge's driver half whose reserve is a no-op: the frames then arrive with only
# the pool's own pad, and the headroom case must fail. Without it, "the frames
# carry the driver's headroom" would be a claim no implementation could
# contradict. The copy keeps its sibling header one directory up (the file
# includes "../seam.h"), so the mutated tree mirrors that one relative edge.
mut=$log_dir/mutated-seam
rm -rf "$mut"
mkdir -p "$mut/bridge"
sed 's/lkpi_skb_reserve(skb, headroom);/lkpi_skb_reserve(skb, 0);/' \
    "$root/ports/net80211/bridge/wlan_seam_adapter.c" > "$mut/bridge/wlan_seam_adapter.c"
cp "$root/ports/net80211/seam.h" "$mut/seam.h"
if grep -q 'lkpi_skb_reserve(skb, 0);' "$mut/bridge/wlan_seam_adapter.c" &&
    make -C "$root/tests/host/wlan-bridge-test" \
        SEAM_ADAPTER_SRC="$mut/bridge/wlan_seam_adapter.c" \
        BUILD_DIR="$mut/build" > "$log_dir/mutated-build.log" 2>&1; then
    if timeout -k 5 180 "$mut/build/wlan-bridge-test" > "$log_dir/mutated-run.log" 2>&1; then
        case_line tx-headroom-red-control FAIL
        echo "wlan-bridge-test: the no-reserve bridge passed (it must fail)" >&2
    else
        if grep -q '^WLANBRIDGE: B17 FAIL' "$log_dir/mutated-run.log"; then
            case_line tx-headroom-red-control PASS
        else
            case_line tx-headroom-red-control FAIL
            echo "wlan-bridge-test: the red control failed elsewhere" >&2
            grep '^WLANBRIDGE' "$log_dir/mutated-run.log" | tail -6 >&2 || true
        fi
    fi
else
    case_line tx-headroom-red-control FAIL
    echo "wlan-bridge-test: the mutated bridge did not build" >&2
    tail -5 "$log_dir/mutated-build.log" >&2 || true
fi

# ---- the rx-channel red control -----------------------------------------------

# net80211's beacon parser takes a frame's receive channel from the interface's
# current channel, so the bridge must point it at the channel the driver reported
# for the frame being processed. The control builds the chain against a copy of
# the bridge whose pointing is a no-op: a frame delivered across a dwell is then
# judged against the wrong channel and discarded off-channel, and the rx-channel
# case must fail. Without it, "the frame's own channel reaches the parser" would
# be a claim no implementation could contradict.
rxmut=$log_dir/mutated-bridge
rm -rf "$rxmut"
mkdir -p "$rxmut"
sed 's/ic->ic_curchan = rx_chan;/ic->ic_curchan = saved_chan;/' \
    "$root/ports/net80211/bridge/wlan_bridge.c" > "$rxmut/wlan_bridge.c"
if grep -q 'ic->ic_curchan = saved_chan;' "$rxmut/wlan_bridge.c" &&
    make -C "$root/tests/host/wlan-bridge-test" \
        BRIDGE_SRC="$rxmut/wlan_bridge.c" \
        BUILD_DIR="$rxmut/build" > "$log_dir/rxmut-build.log" 2>&1; then
    if timeout -k 5 180 "$rxmut/build/wlan-bridge-test" > "$log_dir/rxmut-run.log" 2>&1; then
        case_line rx-channel-red-control FAIL
        echo "wlan-bridge-test: the no-rx-channel bridge passed (it must fail)" >&2
    else
        if grep -q '^WLANBRIDGE: B18 FAIL' "$log_dir/rxmut-run.log"; then
            case_line rx-channel-red-control PASS
        else
            case_line rx-channel-red-control FAIL
            echo "wlan-bridge-test: the rx-channel control failed elsewhere" >&2
            grep '^WLANBRIDGE' "$log_dir/rxmut-run.log" | tail -6 >&2 || true
        fi
    fi
else
    case_line rx-channel-red-control FAIL
    echo "wlan-bridge-test: the mutated bridge did not build" >&2
    tail -5 "$log_dir/rxmut-build.log" >&2 || true
fi

# ---- the sweep-window ordering red control ------------------------------------

# The sweep's bookkeeping flag must not go up until the state transition that
# opens the window has finished, because the dwell pump gates on it and the
# transition can outlast a dwell period. The control builds the chain against a
# copy of the bridge that raises the flag before the transition (the ordering
# this case exists to forbid) and requires the case to fail. Without it, "the
# window is not visible early" would be a claim no implementation could
# contradict.
ordmut=$log_dir/mutated-bridge-order
rm -rf "$ordmut"
mkdir -p "$ordmut"
sed 's/bridge\.scan_starts++;/bridge.scanning = 1; bridge.scan_starts++;/' \
    "$root/ports/net80211/bridge/wlan_bridge.c" > "$ordmut/wlan_bridge.c"
if grep -q 'bridge.scanning = 1; bridge.scan_starts++;' "$ordmut/wlan_bridge.c" &&
    make -C "$root/tests/host/wlan-bridge-test" \
        BRIDGE_SRC="$ordmut/wlan_bridge.c" \
        BUILD_DIR="$ordmut/build" > "$log_dir/ordmut-build.log" 2>&1; then
    if timeout -k 5 180 "$ordmut/build/wlan-bridge-test" > "$log_dir/ordmut-run.log" 2>&1; then
        case_line sweep-window-red-control FAIL
        echo "wlan-bridge-test: the early-flag bridge passed (it must fail)" >&2
    else
        if grep -q '^WLANBRIDGE: B19 FAIL' "$log_dir/ordmut-run.log"; then
            case_line sweep-window-red-control PASS
        else
            case_line sweep-window-red-control FAIL
            echo "wlan-bridge-test: the ordering control failed elsewhere" >&2
            grep '^WLANBRIDGE' "$log_dir/ordmut-run.log" | tail -6 >&2 || true
        fi
    fi
else
    case_line sweep-window-red-control FAIL
    echo "wlan-bridge-test: the early-flag bridge did not build" >&2
    tail -5 "$log_dir/ordmut-build.log" >&2 || true
fi

exit "$failures"

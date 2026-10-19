#!/bin/sh
# @file    check.sh
# @brief   Wrap the net80211 protocol-path mock into container cases.
# @author  zhugengyu
# @date    07.10.2026
# @design adapter - ref net80211:tests/run-host-tests.sh:1
#
# The suite binary is not duplicated here: this wrapper runs the binaries its own
# Makefile built and turns the anchor lines into one CASE line each, so the
# container and its aggregated log speak the same language as every other suite.
#
# Three groups are judged here: the green protocol run (every anchor it claims to
# cover must be present and green, and the transmit-before-receive order must
# hold), the library's own two host tests re-anchored onto this port, and the
# fault invocation whose failing anchor set must equal the set registered below -
# the pollution may only redden the segment it entered.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
suite=net80211-frame-test
smoke=$root/tests/host/net80211-frame-test
binary=$smoke/build/net80211-frame-test
mbuf_test=$smoke/build/upstream-mbuf-trim
ccm_test=$smoke/build/upstream-ccm

failures=0

case_line() {
    if [ "$2" = "1" ]; then
        printf 'CASE %s/%s PASS\n' "$suite" "$1"
    else
        printf 'CASE %s/%s FAIL\n' "$suite" "$1"
        failures=$((failures + 1))
    fi
}

for f in "$binary" "$mbuf_test" "$ccm_test"; do
    if [ ! -x "$f" ]; then
        echo "MOCK-BUILD $suite: $f was not built" >&2
        exit 2
    fi
done

log=$(mktemp)
flog=$(mktemp)
trap 'rm -f "$log" "$flog"' EXIT

# ---------------------------------------------------------------------------
# the green protocol run
# ---------------------------------------------------------------------------
set +e
timeout 60 "$binary" > "$log" 2>&1
code=$?
set -e

case_line run_exit "$([ "$code" = 0 ] && echo 1 || echo 0)"

# every anchor this suite registers, in the order the run prints them
anchors="S2.LINK_ATTACH S2.SCAN_START S2.TX_PROBE_REQ S1.BYTES_TX S3.TX_AUTH \
S3.TX_ASSOC S3.RX_PROBE_RESP S3.VEC_PROBE_RESP S3.RX_AUTH S3.VEC_AUTH_RESP \
S3.RX_ASSOC S3.VEC_ASSOC_RESP S3.EVENT_SCAN_DONE S3.EVENT_ASSOC S3.NODE_TABLE \
S3.RUN_STATE S3.ENCAP S3.DECAP S1.BYTES_RX S4.EAPOL_CLASSIFY S3.DEFRAG \
S3.DEFRAG_NEG S3.NEG_TRUNC S3.NEG_IE S3.CCMP S3.HWCCMP S3.WATCHDOG M.PAIR_SKB \
M.NAMED_ZERO M.PAIR_MBUF M.NAMED_MISUSE"

for id in $anchors; do
    if grep -qF "vwifi-smoke: $id ok=1" "$log"; then
        case_line "$id" 1
    else
        case_line "$id" 0
    fi
done

if grep -qF "vwifi-smoke: PASS" "$log"; then
    case_line pass_anchor 1
else
    case_line pass_anchor 0
fi

# the transmit-before-receive order the anchors promise, plus the association
# event last among the management events
order_ok=0
order=$(grep -n -F \
    -e "vwifi-smoke: S2.TX_PROBE_REQ" -e "vwifi-smoke: S3.RX_PROBE_RESP" \
    -e "vwifi-smoke: S3.TX_AUTH" -e "vwifi-smoke: S3.RX_AUTH" \
    -e "vwifi-smoke: S3.TX_ASSOC" -e "vwifi-smoke: S3.RX_ASSOC" \
    -e "vwifi-smoke: S3.EVENT_ASSOC" "$log" | cut -d: -f1)
if [ "$(printf '%s\n' "$order" | grep -c . || true)" = 7 ] \
   && printf '%s\n' "$order" | awk 'NR > 1 && $1 <= prev { bad = 1 } { prev = $1 } END { exit bad }'; then
    order_ok=1
fi
case_line anchor_order "$order_ok"

if [ "$(grep -ci skip "$log" || true)" = 0 ]; then
    case_line no_skip 1
else
    case_line no_skip 0
fi

# ---------------------------------------------------------------------------
# the library's own two host tests, re-anchored onto this port
# ---------------------------------------------------------------------------
set +e
timeout 60 "$mbuf_test" > "$flog" 2>&1
mbuf_code=$?
set -e
if [ "$mbuf_code" = 0 ] && grep -q "mbuf trim: PASS" "$flog" \
   && grep -q "mbuf prepend: PASS" "$flog" \
   && grep -q "mbuf pullup: PASS" "$flog"; then
    case_line upstream_mbuf_trim 1
else
    case_line upstream_mbuf_trim 0
fi

set +e
timeout 60 "$ccm_test" > "$flog" 2>&1
ccm_code=$?
set -e
if [ "$ccm_code" = 0 ] && grep -q "RFC3610 vectors and mbuf MIC rejection: PASS" "$flog"; then
    case_line upstream_ccm 1
else
    case_line upstream_ccm 0
fi

# ---------------------------------------------------------------------------
# the fault group: the polluted authentication response
#
# The failing anchor set is registered here; the run may not create other
# failures and may not crash (an exit code, never a signal).
# ---------------------------------------------------------------------------
expected_red="S3.TX_ASSOC S3.RX_AUTH S3.VEC_AUTH_RESP S3.RX_ASSOC \
S3.VEC_ASSOC_RESP S3.EVENT_ASSOC S3.NODE_TABLE S3.RUN_STATE"

set +e
timeout 60 "$binary" --fault trunc-auth > "$flog" 2>&1
fcode=$?
set -e

case_line fault_exit_code "$([ "$fcode" = 1 ] && echo 1 || echo 0)"

got_red=$(sed -n 's/.*vwifi-smoke: \([A-Z0-9_.]*\) ok=0.*/\1/p' "$flog" | sort | tr '\n' ' ')
want_red=$(printf '%s\n' $expected_red | sort | tr '\n' ' ')
case_line fault_failure_set "$([ "$got_red" = "$want_red" ] && echo 1 || echo 0)"

# the named rejection of the polluted input is what the fault run asserts
case_line fault_named_rejection \
    "$(grep -qF "vwifi-smoke: S3.NEG_TRUNC ok=1" "$flog" && echo 1 || echo 0)"

echo "$suite: exit=$code mbuf=$mbuf_code ccm=$ccm_code fault=$fcode red=[$got_red] failures=$failures"
[ "$failures" = 0 ]

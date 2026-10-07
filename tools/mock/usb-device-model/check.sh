#!/bin/sh
# @file    check.sh
# @brief   Wrap the USB device model into container cases.
# @author  zhugengyu
# @date    07.10.2026
# @design adapter - ref net80211:tests/run-host-tests.sh:1
#
# The suite binary is not duplicated here: this wrapper runs the binary its own Makefile
# built and turns the anchor lines into one CASE line each, so the container and its
# aggregated log speak the same language as every other suite.
#
# Four groups are judged here: the green run (the frozen anchor list, in order, with the
# vector-driven assertions the shared factory supplies), the vector-tamper fault, and the
# scripted negatives whose red controls are part of the run. Every group is a witness for
# its own failure mode, so a green run says what it claims.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
suite=usb-device-model
binary=$root/tests/host/linuxkpi-usb-mock/build/usb-device-model
vec_root=$root/tools/mock/vectors

failures=0

case_line() {
    if [ "$2" = "1" ]; then
        printf 'CASE %s/%s PASS\n' "$suite" "$1"
    else
        printf 'CASE %s/%s FAIL\n' "$suite" "$1"
        failures=$((failures + 1))
    fi
}

if [ ! -x "$binary" ]; then
    echo "MOCK-BUILD $suite: $binary was not built" >&2
    exit 2
fi

log=$(mktemp)
poslog=$(mktemp)
trap 'rm -f "$log" "$poslog"' EXIT

# ---------------------------------------------------------------------------
# the green run: every anchor of the frozen list, in the declared order
# ---------------------------------------------------------------------------
set +e
timeout 60 "$binary" --vec-root "$vec_root" > "$log" 2>&1
code=$?
set -e

case_line run_exit "$([ "$code" = 0 ] && echo 1 || echo 0)"

# The frozen anchor list. The order is part of the contract: the enumeration chain runs
# before the class claim, the driver probe follows it, and the smoke verdict is last.
anchors="usb-smoke: hcd registered bus=0
usb-smoke: port connect n=1 speed=HS
usb-smoke: enum descriptor prefix len=8 ok=1
usb-smoke: enum set-addr n=1
usb-smoke: enum dev-desc vid=0x1234 pid=0x5678
usb-smoke: enum config tree bytes=32 ok=1
usb-smoke: enum set-config n=1
usb-smoke: class claimed intf=0 cls=08/06/50
usb-smoke: driver probed intf=0
usb-smoke: ctrl ms-reset ok
usb-smoke: ctrl get-max-lun stall=1
usb-smoke: tur csw status=0 tag=1
usb-smoke: inquiry len=36 vendor=VMOCK prod=UMASS1
usb-smoke: capacity blocks=2048 size=512
usb-smoke: csw tag-paired n=5 mismatch=0
usb-smoke: paired urb ok=1
usb-smoke: paired coherent ok=1
usb-smoke: named-fail all-zero ok=1
usb-smoke: queue drain in-order n=2 ok=1
usb-smoke: kill cancelled urb status=-ENOENT ok=1
usb-smoke: PASS"

# The CASE name of a frozen anchor line: the text after the prefix, with the spaces and
# dots turned into dashes (so the line names one case unambiguously).
anchor_case_name() {
    printf '%s\n' "$1" | sed 's/^usb-smoke: //; s/[ .]/-/g'
}

# Position of each frozen line: a line that is absent breaks the group, and the order
# check below uses only the ones that were found.
: > "$poslog"
n=0
while IFS= read -r pat; do
    [ -n "$pat" ] || continue
    n=$((n + 1))
    pos=$(grep -n -F -e "$pat" "$log" | head -1 | cut -d: -f1)
    if [ -n "$pos" ]; then
        printf '%s\n' "$pos" >> "$poslog"
        case_line "$(anchor_case_name "$pat")" 1
    else
        case_line "$(anchor_case_name "$pat")" 0
        echo "  $suite: missing anchor line: $pat" >&2
    fi
done <<EOF
$anchors
EOF

# Every frozen line present, in the declared order.
order_ok=0
if [ "$(grep -c . "$poslog" || true)" = "$n" ] \
   && awk 'NR > 1 && $1 <= prev { bad = 1 } { prev = $1 } END { exit bad }' "$poslog"; then
    order_ok=1
fi
case_line anchor_order "$order_ok"

# The smoke verdict must be the last usb-smoke: line, and nothing may be skipped.
if [ "$(grep '^usb-smoke:' "$log" | tail -1)" = "usb-smoke: PASS" ]; then
    case_line pass_anchor 1
else
    case_line pass_anchor 0
fi
if [ "$(grep -ci skip "$log" || true)" = 0 ]; then
    case_line no_skip 1
else
    case_line no_skip 0
fi

# The vector-driven assertions: each one says which vector it loaded, and the shared
# factory validates those files at container build time (check-vectors.sh).
for vec in S1.EP0_DESC_DEV.in.001 S1.EP0_ENUM_SETUP.exp.001 S1.CTRL_MS_RESET.exp.001 \
           S1.CTRL_GET_MAX_LUN.exp.001 S1.BOT_CBW.exp.001; do
    name=$(printf '%s\n' "$vec" | tr '.' '_' | tr 'A-Z' 'a-z')
    if grep -qF "usb-smoke: vec=$vec ok=1" "$log"; then
        case_line "$name" 1
    else
        case_line "$name" 0
        echo "  $suite: the vector assertion for $vec is missing" >&2
    fi
done

# ---------------------------------------------------------------------------
# the vector tamper: a must-red control
#
# The run is given the vector root explicitly: the fault perturbs a loaded vector, so
# without the root there is nothing to perturb and the fault does not engage (observed:
# the bare invocation exits 0 and passes).
# ---------------------------------------------------------------------------
set +e
timeout 60 "$binary" --fault vec-tamper --vec-root "$vec_root" > "$log" 2>&1
tamper_code=$?
set -e

if [ "$tamper_code" != 0 ] \
   && grep -qE '^usb-smoke: vec-mismatch vec=.* offset=[0-9]+' "$log"; then
    case_line vec-tamper 1
else
    case_line vec-tamper 0
    echo "  $suite: the tampered vector did not go red (exit=$tamper_code)" >&2
    grep -n 'vec-mismatch\|usb-smoke: FAIL' "$log" | sed 's/^/    /' >&2 || true
fi

# ---------------------------------------------------------------------------
# the scripted negatives: expected green, and the battery's own red control
#
# The negative battery is deliberately broken in the second run: it must go red, which is
# what proves the battery is able to fail rather than merely print ok=1.
# ---------------------------------------------------------------------------
set +e
timeout 60 "$binary" --fault negatives > "$log" 2>&1
neg_code=$?
set -e

if [ "$neg_code" = 0 ] && grep -qF "usb-smoke: ep0-battery neg=7 ok=1" "$log"; then
    case_line ep0-battery 1
else
    case_line ep0-battery 0
    echo "  $suite: the negative battery did not report its subset (exit=$neg_code)" >&2
fi

if [ "$neg_code" = 0 ] \
   && grep -qF "usb-smoke: seam xfail double-free ok=1" "$log" \
   && grep -qF "usb-smoke: seam xfail uaf ok=1" "$log"; then
    case_line seam-xfail 1
else
    case_line seam-xfail 0
    echo "  $suite: the seam negative controls did not report (exit=$neg_code)" >&2
fi

set +e
timeout 60 "$binary" --fault battery-tamper > "$log" 2>&1
bat_code=$?
set -e

if [ "$bat_code" != 0 ] && grep -qF "usb-smoke: ep0-battery ok=0" "$log"; then
    case_line battery-tamper 1
else
    case_line battery-tamper 0
    echo "  $suite: the lying battery did not go red (exit=$bat_code)" >&2
fi

echo "$suite: exit=$code tamper=$tamper_code negatives=$neg_code battery=$bat_code failures=$failures"
[ "$failures" = 0 ]

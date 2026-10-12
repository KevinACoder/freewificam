#!/bin/sh
# @file    check.sh
# @brief   Wrap the linuxkpi PCI engine's host suite into container cases.
# @design  adapter - ref tools/mock/linuxkpi-usb-test/check.sh:1 (the sibling
#          suite's shape: a frozen anchor list judged in order, plus the run's own
#          red controls).
#
# The suite binary is not duplicated here: this wrapper runs the binary its own
# Makefile built and turns the anchor lines into one CASE line each.
#
# Three groups are judged: the green run (the frozen anchor list, in order, with
# the smoke verdict last), and the two scripted faults whose red controls are part
# of the run - a perturbed config byte the identity assertion reads, and a host
# that raises the vector but delivers nothing. Every group is a witness for its own
# failure mode, so a green run says what it claims. A fourth group judges the AC
# 7260 synthetic chain (the driver line's stage i), which is its own invocation
# because the engine's driver registry is bounded and the default run already holds
# three drivers.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
suite=linuxkpi-pcie-test
binary=$root/tests/host/linuxkpi-pcie-test/build/linuxkpi-pcie-test

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
timeout 60 "$binary" > "$log" 2>&1
code=$?
set -e

case_line run_exit "$([ "$code" = 0 ] && echo 1 || echo 0)"

# The frozen anchor list. The order is part of the contract: enumeration and the
# identity records come first, then config space, the command bits, the capability
# walk and the BAR window, then the two interrupt paths, then DMA, then the driver
# registry, and the smoke verdict last.
anchors="pcie-smoke: hcd registered devs=3
pcie-smoke: dev 01:00.0 id=1234:5678 class=0x020000
pcie-smoke: dev 02:00.0 id=1234:5679 class=0x028000
pcie-smoke: cfg dword vendor-device=0x56781234
pcie-smoke: cfg sizes byte=0x34 word=0x1234 dword=0x56781234
pcie-smoke: cfg unimplemented=0xffffffff
pcie-smoke: cfg error rc=-5 val=0xff
pcie-smoke: enable command=0x0002
pcie-smoke: master command=0x0006
pcie-smoke: cap msi=0x50 msix=0x70
pcie-smoke: bar0 len=4096 write-read ok=1
pcie-smoke: msi enable irq=8192 ok=1
pcie-smoke: msi deliver n=1 ok=1
pcie-smoke: msi re-enable busy ok=1
pcie-smoke: msi release ok=1
pcie-smoke: msix vector irq=8193 ok=1
pcie-smoke: msix deliver n=1 ok=1
pcie-smoke: msix no-vector rc=-103 ok=1
pcie-smoke: dma coherent conserve ok=1
pcie-smoke: dma map dirs flush=1 invalidate=1 ok=1
pcie-smoke: dma mask ok=1
pcie-smoke: driver late-claim dev=03:00.0 ok=1
pcie-smoke: probe stats ok=3 errors=0
pcie-smoke: PASS"

anchor_case_name() {
    printf '%s\n' "$1" | sed 's/^pcie-smoke: //; s/[ .]/-/g'
}

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

# The smoke verdict must be the last pcie-smoke: line, and nothing may be skipped.
if [ "$(grep '^pcie-smoke:' "$log" | tail -1)" = "pcie-smoke: PASS" ]; then
    case_line pass_anchor 1
else
    case_line pass_anchor 0
fi
if [ "$(grep -ci skip "$log" || true)" = 0 ]; then
    case_line no_skip 1
else
    case_line no_skip 0
fi

# ---------------------------------------------------------------------------
# the config tamper: a must-red control
#
# The fault perturbs a config byte the identity assertion reads, so the anchor
# that carries the value cannot match and the run must go red.
# ---------------------------------------------------------------------------
set +e
timeout 60 "$binary" --fault cfg-tamper > "$log" 2>&1
tamper_code=$?
set -e

if [ "$tamper_code" != 0 ] && grep -qF "pcie-smoke: FAIL" "$log"; then
    case_line cfg-tamper 1
else
    case_line cfg-tamper 0
    echo "  $suite: the tampered config byte did not go red (exit=$tamper_code)" >&2
fi

# ---------------------------------------------------------------------------
# the lying host: a must-red control
#
# The fault makes the scripted doorbell deliver nothing, so the delivery anchors
# report n=0 and the run must go red.
# ---------------------------------------------------------------------------
set +e
timeout 60 "$binary" --fault lying-host > "$log" 2>&1
lying_code=$?
set -e

if [ "$lying_code" != 0 ] && grep -qF "pcie-smoke: msi deliver n=0 ok=0" "$log"; then
    case_line lying-host 1
else
    case_line lying-host 0
    echo "  $suite: the lying host did not go red (exit=$lying_code)" >&2
fi

# ---------------------------------------------------------------------------
# the AC 7260 synthetic chain, stage i: the driver line's host-side proof
#
# The synthetic host presents the Intel AC 7260 (8086:08b1, class 0280), the
# engine enumerates it, the driver's id table claims it and the probe entry runs
# once with both records. Its own invocation, for the registry-bound reason above.
# ---------------------------------------------------------------------------
set +e
timeout 60 "$binary" --scenario ac7260 > "$log" 2>&1
chain_code=$?
set -e

case_line chain_exit "$([ "$chain_code" = 0 ] && echo 1 || echo 0)"

chain_anchors="pcie-smoke: chain enumerated id=8086:08b1 found=1
pcie-smoke: chain class=0x028000
pcie-smoke: chain bdf=21:00.0
pcie-smoke: chain probe n=1 ok=1
pcie-smoke: chain id=8086:08b1 driver_data=1
pcie-smoke: chain negative probe n=0 ok=1
pcie-smoke: chain cfg vendor-device=0x08b18086
pcie-smoke: chain bar0 len=4096 map=1 ok=1
pcie-smoke: chain stats ok=1 errors=0
pcie-smoke: PASS"

while IFS= read -r pat; do
    [ -n "$pat" ] || continue
    if grep -qF -e "$pat" "$log"; then
        case_line "$(anchor_case_name "$pat")" 1
    else
        case_line "$(anchor_case_name "$pat")" 0
        echo "  $suite: missing chain anchor line: $pat" >&2
    fi
done <<EOF
$chain_anchors
EOF

echo "$suite: exit=$code tamper=$tamper_code lying=$lying_code chain=$chain_code failures=$failures"
[ "$failures" = 0 ]

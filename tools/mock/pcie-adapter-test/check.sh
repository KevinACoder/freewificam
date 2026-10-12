#!/bin/sh
# @file    check.sh
# @brief   Wrap the PCI adapter suite into container cases.
# @design  adapter - ref tools/mock/linuxkpi-pcie-test/check.sh:1 (the sibling
#          suite's shape: a frozen anchor list judged in order, plus the run's own
#          red control).
#
# The suite binary is not duplicated here: this wrapper runs the binary its own
# Makefile built and turns the anchor lines into one CASE line each.
#
# Two groups are judged: the green run (the frozen anchor list, in order, with the
# smoke verdict last) and the scripted config-error fault whose red control is part
# of the run.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
suite=pcie-adapter-test
binary=$root/tests/host/pcie-adapter-test/build/pcie-adapter-test

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

set +e
timeout 60 "$binary" > "$log" 2>&1
code=$?
set -e

case_line run_exit "$([ "$code" = 0 ] && echo 1 || echo 0)"

# The frozen anchor list, in the order the run produces it: the adapter's
# enumeration and identity records, the config path and its error mapping, the BAR
# window, the interrupt arm / delivery / release and the MSI-X no-vector mapping,
# the DMA maintenance, then the domain's own contract.
anchors="pcie-adapter: bus up devs=2 init=1
pcie-adapter: desc dev0 id=1234:5678 class=0x028000
pcie-adapter: desc dev1 id=1234:5679 class=0x020000
pcie-adapter: driver probed dev0 ok=1
pcie-adapter: cfg passthrough dword=0x56781234
pcie-adapter: cfg error rc=-12 val=0xff
pcie-adapter: bar map identity ok=1
pcie-adapter: msi arm intid=8192 allocs=1 enables=1
pcie-adapter: irq dispatch handler n=1 ok=1
pcie-adapter: msi release releases=1 disables=1
pcie-adapter: msix no-vector rc=-103
pcie-adapter: dma map tx flush=1 ok=1
pcie-adapter: dma map rx invalidate=2 ok=1
pcie-adapter: dma coherent ok=1
pcie-adapter: domain present ok=1
pcie-adapter: domain alloc n=1 intid=8192 data=0 addr=0xfd450040
pcie-adapter: domain rid attach=1 map=1
pcie-adapter: domain second alloc refused ok=1
pcie-adapter: domain free unmaps=1 ok=1
pcie-adapter: domain fail unwinds n=2 ok=1
pcie-adapter: PASS"

anchor_case_name() {
    printf '%s\n' "$1" | sed 's/^pcie-adapter: //; s/[ .]/-/g'
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

order_ok=0
if [ "$(grep -c . "$poslog" || true)" = "$n" ] \
   && awk 'NR > 1 && $1 <= prev { bad = 1 } { prev = $1 } END { exit bad }' "$poslog"; then
    order_ok=1
fi
case_line anchor_order "$order_ok"

if [ "$(grep '^pcie-adapter:' "$log" | tail -1)" = "pcie-adapter: PASS" ]; then
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
# the config-error fault: a must-red control
#
# The fault makes the scripted config read fail, so the pass-through anchor cannot
# carry the value it wrote and the run must go red.
# ---------------------------------------------------------------------------
set +e
timeout 60 "$binary" --fault cfg-error > "$log" 2>&1
fault_code=$?
set -e

if [ "$fault_code" != 0 ] && grep -qF "pcie-adapter: FAIL" "$log"; then
    case_line cfg-error 1
else
    case_line cfg-error 0
    echo "  $suite: the config-error fault did not go red (exit=$fault_code)" >&2
fi

echo "$suite: exit=$code fault=$fault_code failures=$failures"
[ "$failures" = 0 ]

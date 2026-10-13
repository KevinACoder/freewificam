#!/bin/sh
# @file    check.sh
# @brief   Wrapper for the ehci-iso-schedule-test suite: run the cases and the
#          red control, and map each into container case lines.
#
# The harness owns the mechanism (tests/host/ehci-iso-schedule-test): eleven
# cases driving the port's real translation units against a controller in RAM.
# This wrapper runs it and adds two controls that prove the cases can fail: the
# same harness built against a mutated copy of the engine whose
# every-second-micro-frame period mask claims slots it does not feed must turn
# the period-mask case red, and one built against a mutated port whose
# control-IN invalidate keeps the old endpoint-address test must turn the
# control-in-cache case red.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
log_dir=$root/tools/mock/build/ehci-iso-schedule-test
mkdir -p "$log_dir"

bin=$root/tests/host/ehci-iso-schedule-test/build/ehci-iso-schedule-test
if [ ! -x "$bin" ]; then
    echo "MOCK-BUILD ehci-iso-schedule-test: $bin was not built" >&2
    exit 2
fi

cases="period-mask tscl-encode frame-link link-unlink slot-exhaustion completion-map sync-wait control-in-cache reject-matrix direction-out schedule-tiling"

failures=0
case_line() {
    printf 'CASE ehci-iso-schedule-test/%s %s\n' "$1" "$2"
    [ "$2" = "PASS" ] || failures=$((failures + 1))
}

# ---- the cases ----------------------------------------------------------------

if "$bin" > "$log_dir/cases.log" 2>&1; then
    for c in $cases; do
        if grep -q "^EHCIISO: $c PASS\$" "$log_dir/cases.log"; then
            case_line "$c" PASS
        else
            case_line "$c" FAIL
            grep "^EHCIISO: $c" "$log_dir/cases.log" >&2 || true
        fi
    done
    if ! grep -q '^EHCIISO: summary cases=11 failed=0$' "$log_dir/cases.log"; then
        echo "ehci-iso-schedule-test: summary unexpected" >&2
        tail -8 "$log_dir/cases.log" >&2 || true
    fi
else
    for c in $cases; do
        case_line "$c" FAIL
    done
    echo "ehci-iso-schedule-test: run failed" >&2
    tail -12 "$log_dir/cases.log" >&2 || true
fi

# ---- the red control ----------------------------------------------------------

# The period mask is what keeps the per-frame packing honest: each selected
# micro-frame carries exactly one packet, and no slot past the descriptor's
# packet count is armed.  The control builds the harness against a copy whose
# every-second-micro-frame mask selects slots the packing does not feed; the
# period-mask case must go red.
CU=$(sh "$root/tools/patches/replay.sh" cherryusb)
mut=$log_dir/mutated-iso
rm -rf "$mut"
mkdir -p "$mut"
sed -e 's/#define EHCI_ISO_MF_MASK_EVERY_2UF 0x55/#define EHCI_ISO_MF_MASK_EVERY_2UF 0x77/' \
    "$CU/port/ehci/usb_hc_ehci_iso.c" > "$mut/usb_hc_ehci_iso.c"
if grep -q 'EVERY_2UF 0x77' "$mut/usb_hc_ehci_iso.c" &&
    make -C "$root/tests/host/ehci-iso-schedule-test" \
        ISO_SRC="$mut/usb_hc_ehci_iso.c" BUILD_DIR="$mut/build" > "$log_dir/mutated-build.log" 2>&1; then
    if "$mut/build/ehci-iso-schedule-test" > "$log_dir/mutated-run.log" 2>&1; then
        case_line period-mask-red-control FAIL
        echo "ehci-iso-schedule-test: the lying period mask passed (it must fail)" >&2
    else
        if grep -q '^EHCIISO: period-mask FAIL' "$log_dir/mutated-run.log"; then
            case_line period-mask-red-control PASS
        else
            case_line period-mask-red-control FAIL
            echo "ehci-iso-schedule-test: the red control failed elsewhere" >&2
            tail -4 "$log_dir/mutated-run.log" >&2 || true
        fi
    fi
else
    case_line period-mask-red-control FAIL
    echo "ehci-iso-schedule-test: the mutated engine did not build" >&2
    tail -5 "$log_dir/mutated-build.log" >&2 || true
fi

# The completion path must invalidate a control-IN buffer with the direction
# taken from the setup packet (endpoint zero's address is 0x00).  The control
# builds the harness against a copy whose condition keeps only the old
# endpoint-address test; the control-in-cache case must go red.
mut=$log_dir/mutated-port
rm -rf "$mut"
mkdir -p "$mut"
sed -e 's/(urb->setup \&\& (urb->setup->bmRequestType & 0x80)) ||/0 ||/' \
    "$CU/port/ehci/usb_hc_ehci.c" > "$mut/usb_hc_ehci.c"
if grep -q '0 ||' "$mut/usb_hc_ehci.c" &&
    make -C "$root/tests/host/ehci-iso-schedule-test" \
        PORT_SRC="$mut/usb_hc_ehci.c" BUILD_DIR="$mut/build" > "$log_dir/mutated-port-build.log" 2>&1; then
    if "$mut/build/ehci-iso-schedule-test" > "$log_dir/mutated-port-run.log" 2>&1; then
        case_line control-in-cache-red-control FAIL
        echo "ehci-iso-schedule-test: the endpoint-address-only invalidate passed (it must fail)" >&2
    else
        if grep -q '^EHCIISO: control-in-cache FAIL' "$log_dir/mutated-port-run.log"; then
            case_line control-in-cache-red-control PASS
        else
            case_line control-in-cache-red-control FAIL
            echo "ehci-iso-schedule-test: the red control failed elsewhere" >&2
            tail -4 "$log_dir/mutated-port-run.log" >&2 || true
        fi
    fi
else
    case_line control-in-cache-red-control FAIL
    echo "ehci-iso-schedule-test: the mutated port did not build" >&2
    tail -5 "$log_dir/mutated-port-build.log" >&2 || true
fi

exit "$failures"

#!/bin/sh
# @file    check.sh
# @brief   Wrapper for the uvc-stream-test suite: run the cases and the red
#          control, and map each into container case lines.
#
# The harness owns the mechanism (tests/host/uvc-stream-test): eight cases
# driving the real stream engine against a scripted device. This wrapper adds
# two controls that prove the cases can fail: the same harness built against a
# mutated copy of the engine whose alternate-setting comparison drops the
# "equal" case must turn the alt-select case red, and one whose probe ladder
# skips the interface re-arm must turn the probe-rearm case red.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
log_dir=$root/tools/mock/build/uvc-stream-test
mkdir -p "$log_dir"

bin=$root/tests/host/uvc-stream-test/build/uvc-stream-test
if [ ! -x "$bin" ]; then
    echo "MOCK-BUILD uvc-stream-test: $bin was not built" >&2
    exit 2
fi

cases="alt-select probe-rearm frame-assembly eof-boundary flip-no-eof error-drop overflow-drop pool-drain"

failures=0
case_line() {
    printf 'CASE uvc-stream-test/%s %s\n' "$1" "$2"
    [ "$2" = "PASS" ] || failures=$((failures + 1))
}

# ---- the cases ----------------------------------------------------------------

if "$bin" > "$log_dir/cases.log" 2>&1; then
    for c in $cases; do
        if grep -q "^UVCSTREAM: $c PASS\$" "$log_dir/cases.log"; then
            case_line "$c" PASS
        else
            case_line "$c" FAIL
            grep "^UVCSTREAM: $c" "$log_dir/cases.log" >&2 || true
        fi
    done
    if ! grep -q '^UVCSTREAM: summary cases=8 failed=0$' "$log_dir/cases.log"; then
        echo "uvc-stream-test: summary unexpected" >&2
        tail -8 "$log_dir/cases.log" >&2 || true
    fi
else
    for c in $cases; do
        case_line "$c" FAIL
    done
    echo "uvc-stream-test: run failed" >&2
    tail -12 "$log_dir/cases.log" >&2 || true
fi

# ---- the red control ----------------------------------------------------------

# The "smallest sufficient setting" rule accepts equality: an endpoint that
# carries exactly the requested bytes is sufficient. The control builds the
# harness against a copy whose comparison demands strictly more; the
# alt-select case (want=192 must choose the 192-byte setting) must go red.
CU=$(sh "$root/tools/patches/replay.sh" cherryusb)
mut=$log_dir/mutated-stream
rm -rf "$mut"
mkdir -p "$mut"
sed -e 's/(psize >= want)/(psize > want)/' \
    "$CU/class/video/usbh_uvc_stream.c" > "$mut/usbh_uvc_stream.c"
if grep -q '(psize > want)' "$mut/usbh_uvc_stream.c" &&
    make -C "$root/tests/host/uvc-stream-test" \
        STREAM_SRC="$mut/usbh_uvc_stream.c" BUILD_DIR="$mut/build" > "$log_dir/mutated-build.log" 2>&1; then
    if "$mut/build/uvc-stream-test" > "$log_dir/mutated-run.log" 2>&1; then
        case_line alt-select-red-control FAIL
        echo "uvc-stream-test: the strict comparison passed (it must fail)" >&2
    else
        if grep -q '^UVCSTREAM: alt-select FAIL' "$log_dir/mutated-run.log"; then
            case_line alt-select-red-control PASS
        else
            case_line alt-select-red-control FAIL
            echo "uvc-stream-test: the red control failed elsewhere" >&2
            tail -4 "$log_dir/mutated-run.log" >&2 || true
        fi
    fi
else
    case_line alt-select-red-control FAIL
    echo "uvc-stream-test: the mutated engine did not build" >&2
    tail -5 "$log_dir/mutated-build.log" >&2 || true
fi

# The probe ladder's cure is the interface re-arm cycle; a build that skips it
# must fail the probe-rearm case (the refused camera never answers again).
mut=$log_dir/mutated-norescue
rm -rf "$mut"
mkdir -p "$mut"
sed -e 's/^            stream_interface_rearm(video);$/            \/* mutated: no re-arm *\//' \
    "$CU/class/video/usbh_uvc_stream.c" > "$mut/usbh_uvc_stream.c"
if grep -q 'mutated: no re-arm' "$mut/usbh_uvc_stream.c" &&
    make -C "$root/tests/host/uvc-stream-test" \
        STREAM_SRC="$mut/usbh_uvc_stream.c" BUILD_DIR="$mut/build" > "$log_dir/mutated-norescue-build.log" 2>&1; then
    if "$mut/build/uvc-stream-test" > "$log_dir/mutated-norescue-run.log" 2>&1; then
        case_line probe-rearm-red-control FAIL
        echo "uvc-stream-test: without the re-arm the probe case passed (it must fail)" >&2
    else
        if grep -q '^UVCSTREAM: probe-rearm FAIL' "$log_dir/mutated-norescue-run.log"; then
            case_line probe-rearm-red-control PASS
        else
            case_line probe-rearm-red-control FAIL
            echo "uvc-stream-test: the red control failed elsewhere" >&2
            tail -4 "$log_dir/mutated-norescue-run.log" >&2 || true
        fi
    fi
else
    case_line probe-rearm-red-control FAIL
    echo "uvc-stream-test: the mutated engine did not build" >&2
    tail -5 "$log_dir/mutated-norescue-build.log" >&2 || true
fi

exit "$failures"

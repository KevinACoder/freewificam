#!/bin/sh
# @file    check.sh
# @brief   Wrapper for the net80211 port contract suite: turn the port test
#          directory's own gates into container case lines.
# @author  zhugengyu
# @date    07.10.2026
#
# The heavy lifting lives in tests/host/net80211-port-test's Makefile (one build
# language, one place to read it).  This wrapper runs its four gates and maps
# each into one case: the imported stack translation units compile clean, the
# include discipline holds mechanically, the port smoke links and runs on the
# ThreadX host runtime, and the kernel-isolation gates pass.
#
# The full clause inventory of the port contract (per-clause cases) belongs to
# a later round; this wrapper is the executable first slice.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
log_dir=$root/tools/mock/build/net80211-port-contract
mkdir -p "$log_dir"

failures=0

case_line() {
    printf 'CASE net80211-port-test/%s %s\n' "$1" "$2"
    [ "$2" = "PASS" ] || failures=$((failures + 1))
}

# ---- the port test directory's own gates -------------------------------------

for gate in stack-compile headers link-threadx gates; do
    if make -C "$root/tests/host/net80211-port-test" "$gate" \
            > "$log_dir/$gate.log" 2>&1; then
        case_line "$gate" PASS
    else
        case_line "$gate" FAIL
        printf 'net80211-port-contract: %s failed, see %s\n' \
            "$gate" "$log_dir/$gate.log" >&2
        tail -5 "$log_dir/$gate.log" >&2 || true
    fi
done

# ---- the wireless route declaration -----------------------------------------

# The suite consumes 802.11 frames and IE encodings; its truth routing is the
# same one the frame mock declares.  The annotation is fixed text here because
# the suite's contents are fixed; a new frame-level expectation in this suite
# must extend the annotation in the same change.
route=$root/tools/mock/build/net80211-port-contract/route.txt
cat > "$route" <<'EOF'
ieee80211-route: IEEE 802.11-2020 clause 9.3.3.1 (frame control fields), clause 9.4.2.1 (beacon/probe response body), clause 12.7.2 (CCMP, RFC 3610)
EOF

exit "$failures"

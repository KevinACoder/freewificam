#!/bin/sh
# @file    check.sh
# @brief   Wrapper for the linuxkpi-80211-shape-test suite: run the dispatch
#          cases and the red control, and map each into container case lines.
#
# The harness owns the mechanism (tests/host/linuxkpi-80211-shape-test): the
# substitute body driven by scripted drivers of each op family.  This wrapper
# runs it, groups the cases into two container case lines, and adds the controls
# that prove they can fail: the same harness built against a substitute that
# never calls vif_cfg_changed must turn the 7.3 association case red, and one
# that leaves the default link's address zero must turn the link-address case
# red.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
log_dir=$root/tools/mock/build/linuxkpi-80211-shape-test
mkdir -p "$log_dir"

bin=$root/tests/host/linuxkpi-80211-shape-test/build/linuxkpi-80211-shape-test
if [ ! -x "$bin" ]; then
    echo "MOCK-BUILD linuxkpi-80211-shape-test: $bin was not built" >&2
    exit 2
fi

failures=0

case_line() {
    printf 'CASE linuxkpi-80211-shape-test/%s %s\n' "$1" "$2"
    [ "$2" = "PASS" ] || failures=$((failures + 1))
}

# a group is one case line, backed by the named anchor lines all being PASS
group() {
    name=$1
    shift
    if grep -q '^SHAPE: summary cases=9 failed=0$' "$log_dir/run.log"; then
        ok=1
        for c in "$@"; do
            grep -q "^SHAPE: $c PASS" "$log_dir/run.log" || ok=0
        done
        if [ "$ok" = "1" ]; then
            case_line "$name" PASS
        else
            case_line "$name" FAIL
            echo "linuxkpi-80211-shape-test: $name: an anchor line is missing or failed" >&2
            grep '^SHAPE: ' "$log_dir/run.log" >&2 || true
        fi
    else
        case_line "$name" FAIL
        echo "linuxkpi-80211-shape-test: summary unexpected" >&2
        tail -8 "$log_dir/run.log" >&2 || true
    fi
}

# ---- the cases ----------------------------------------------------------------

if "$bin" > "$log_dir/run.log" 2> "$log_dir/run.err"; then
    group 73-family-dispatch \
        73-association 73-leave link-bookkeeping default-link-address \
        zero-change-is-silent station-default-link
    group 6x-family-dispatch \
        6x-association 6x-leave both-families
else
    case_line 73-family-dispatch FAIL
    case_line 6x-family-dispatch FAIL
    echo "linuxkpi-80211-shape-test: run failed" >&2
    tail -8 "$log_dir/run.log" >&2 || true
fi

# ---- the red control ----------------------------------------------------------

# The vif-wide half of a BSS change goes to vif_cfg_changed; a driver that only
# ever hears link_info_changed would never be told it is associated.  The
# control builds the same harness against a copy of the substitute whose
# dispatch no longer makes that call; the 7.3 association case must then fail.
mut=$log_dir/mutated-engine
rm -rf "$mut"
mkdir -p "$mut"
sed 's/if (cfg != 0 && ctx->ops->vif_cfg_changed != NULL)/if (0)/' \
    "$root/components/linuxkpi/linuxkpi_80211.c" > "$mut/linuxkpi_80211.c"
if grep -q 'if (0)' "$mut/linuxkpi_80211.c" &&
    make -C "$root/tests/host/linuxkpi-80211-shape-test" \
        ENGINE_EXTRA_SRC="$mut/linuxkpi_80211.c" \
        BUILD_DIR="$mut/build" > "$log_dir/mutated-build.log" 2>&1; then
    if timeout -k 5 120 "$mut/build/linuxkpi-80211-shape-test" > "$log_dir/mutated-run.log" 2>&1; then
        case_line vif-cfg-red-control FAIL
        echo "linuxkpi-80211-shape-test: the cfg-silent substitute passed (it must fail)" >&2
    else
        if grep -q '^SHAPE: 73-association FAIL' "$log_dir/mutated-run.log"; then
            case_line vif-cfg-red-control PASS
        else
            case_line vif-cfg-red-control FAIL
            echo "linuxkpi-80211-shape-test: the red control failed elsewhere" >&2
            grep '^SHAPE: ' "$log_dir/mutated-run.log" >&2 || true
        fi
    fi
else
    case_line vif-cfg-red-control FAIL
    echo "linuxkpi-80211-shape-test: the mutated substitute did not build" >&2
    tail -5 "$log_dir/mutated-build.log" >&2 || true
fi

# The default link's address is what a driver reads while taking the interface
# to program the chip's own address filter (rtw89: mac80211.c:137-138).  The
# control builds the harness against a substitute that leaves that field zero;
# the default-link-address case must then fail.
mut2=$log_dir/mutated-linkaddr
rm -rf "$mut2"
mkdir -p "$mut2"
sed 's/memcpy(vif->bss_conf.addr, vif->addr, 6);/;/' \
    "$root/components/linuxkpi/linuxkpi_80211.c" > "$mut2/linuxkpi_80211.c"
if grep -q 'memcpy(vif->bss_conf.addr' "$mut2/linuxkpi_80211.c"; then
    case_line link-addr-red-control FAIL
    echo "linuxkpi-80211-shape-test: the link-address mutation did not apply" >&2
elif make -C "$root/tests/host/linuxkpi-80211-shape-test" \
        ENGINE_EXTRA_SRC="$mut2/linuxkpi_80211.c" \
        BUILD_DIR="$mut2/build" > "$log_dir/mutated-linkaddr-build.log" 2>&1; then
    if timeout -k 5 120 "$mut2/build/linuxkpi-80211-shape-test" > "$log_dir/mutated-linkaddr-run.log" 2>&1; then
        case_line link-addr-red-control FAIL
        echo "linuxkpi-80211-shape-test: the zero-link-address substitute passed (it must fail)" >&2
    else
        if grep -q '^SHAPE: default-link-address FAIL' "$log_dir/mutated-linkaddr-run.log"; then
            case_line link-addr-red-control PASS
        else
            case_line link-addr-red-control FAIL
            echo "linuxkpi-80211-shape-test: the link-address red control failed elsewhere" >&2
            grep '^SHAPE: ' "$log_dir/mutated-linkaddr-run.log" >&2 || true
        fi
    fi
else
    case_line link-addr-red-control FAIL
    echo "linuxkpi-80211-shape-test: the mutated substitute did not build" >&2
    tail -5 "$log_dir/mutated-linkaddr-build.log" >&2 || true
fi

# The station's default link is what a driver reads off sta->link[link_id]
# (rtw89's association handler builds its CMAC table from it).  The control
# builds the harness against a substitute that never wires sta->link[0]; the
# station-default-link case must then fail.
mut3=$log_dir/mutated-stalink
rm -rf "$mut3"
mkdir -p "$mut3"
sed 's/sta->link\[0\] = &sta->deflink;/;/' \
    "$root/components/linuxkpi/linuxkpi_80211.c" > "$mut3/linuxkpi_80211.c"
if grep -q 'sta->link\[0\] = &sta->deflink;' "$mut3/linuxkpi_80211.c"; then
    case_line sta-link-red-control FAIL
    echo "linuxkpi-80211-shape-test: the sta-link mutation did not apply" >&2
elif make -C "$root/tests/host/linuxkpi-80211-shape-test" \
        ENGINE_EXTRA_SRC="$mut3/linuxkpi_80211.c" \
        BUILD_DIR="$mut3/build" > "$log_dir/mutated-stalink-build.log" 2>&1; then
    if timeout -k 5 120 "$mut3/build/linuxkpi-80211-shape-test" > "$log_dir/mutated-stalink-run.log" 2>&1; then
        case_line sta-link-red-control FAIL
        echo "linuxkpi-80211-shape-test: the unwired-sta-link substitute passed (it must fail)" >&2
    else
        if grep -q '^SHAPE: station-default-link FAIL' "$log_dir/mutated-stalink-run.log"; then
            case_line sta-link-red-control PASS
        else
            case_line sta-link-red-control FAIL
            echo "linuxkpi-80211-shape-test: the sta-link red control failed elsewhere" >&2
            grep '^SHAPE: ' "$log_dir/mutated-stalink-run.log" >&2 || true
        fi
    fi
else
    case_line sta-link-red-control FAIL
    echo "linuxkpi-80211-shape-test: the mutated substitute did not build" >&2
    tail -5 "$log_dir/mutated-stalink-build.log" >&2 || true
fi

exit "$failures"

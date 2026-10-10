#!/bin/sh
# Gate: the build shape of every registered host suite.
#
# `make mock` compiles each suite the registry names, one Makefile per suite.
# Two shapes in those Makefiles are load-bearing and are checked here from the
# source side, because a violation is invisible at run time until it lies:
#
#   - header dependency tracking: a compile command must pass -MMD -MP and the
#     Makefile must -include the resulting .d files.  Without it a header edit
#     leaves the objects that included it stale, and a suite then runs a binary
#     whose translation units disagree about the very layouts the header
#     states - the container reports green on a tree that does not build (this
#     happened: a struct usb_device layout change left the harness objects from
#     an earlier day linked against a freshly built engine);
#   - `clean` must remove exactly the build directory.  A clean that appends a
#     path suffix removes nothing in the default configuration, so "clean and
#     rebuild" silently reuses every object.
#
# Usage:
#   tools/gates/check-host-makefiles.sh               # scan the registered suites
#   tools/gates/check-host-makefiles.sh --selftest    # mutate copies; every case must go red
#   tools/gates/check-host-makefiles.sh --registry <suites.mk>
# Exit status: 0 clean, 1 findings.

set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
registry=$root/tools/mock/suites.mk

case "${1:-}" in
    --selftest)
        selftest=1
        ;;
    --registry)
        registry=${2:?usage: check-host-makefiles.sh [--registry <suites.mk>]}
        selftest=0
        ;;
    "")
        selftest=0
        ;;
    *)
        echo "usage: check-host-makefiles.sh [--selftest] [--registry <suites.mk>]" >&2
        exit 2
        ;;
esac

# The findings for one Makefile; empty means clean.
check_makefile() {
    mk=$1
    bad=
    if ! grep -q -- '-MMD' "$mk"; then
        bad="$bad no-dependency-tracking"
    fi
    if ! grep -q '^-include' "$mk"; then
        bad="$bad no-dependency-include"
    fi
    if ! grep -q '^clean:' "$mk"; then
        bad="$bad no-clean-target"
    elif grep -A2 '^clean:' "$mk" | grep -q 'rm -rf .*\$(if '; then
        bad="$bad clean-removes-a-suffixed-path"
    fi
    [ -z "$bad" ]
}

scan() {
    status=0
    list=$(sed -n 's/^[[:space:]]*SUITES[[:space:]]*:=[[:space:]]*//p' "$registry")
    if [ -z "$list" ]; then
        echo "check-host-makefiles: FAIL (registry $registry names no suites)"
        return 1
    fi
    repo=$(cd "$(dirname "$registry")/../.." && pwd)
    host=$repo/tests/host
    for suite in $list; do
        if [ ! -f "$host/$suite/Makefile" ]; then
            # A suite that does not build through a Makefile (a script suite) is
            # out of this gate's scope; one that does must have its Makefile here.
            decl=$repo/tools/mock/$suite/suite.mk
            if [ -f "$decl" ] && grep -qE '^SUITE_BUILD[[:space:]]*:=[[:space:]]*make' "$decl"; then
                echo "FAIL: $suite: builds through a Makefile that is not under $host/$suite"
                status=1
            else
                echo "SKIP: $suite (not a Makefile-built suite)"
            fi
            continue
        fi
        if check_makefile "$host/$suite/Makefile"; then
            echo "PASS: $suite"
        else
            echo "FAIL: $suite:$bad"
            status=1
        fi
    done
    return $status
}

if [ "$selftest" = 0 ]; then
    if scan; then
        echo "check-host-makefiles: PASS (build shape of every registered host suite)"
        exit 0
    fi
    echo "check-host-makefiles: FAIL (build shape)"
    exit 1
fi

# The selftest mutates copies of the real Makefiles: the pristine copies must
# pass, and each mutation must turn the scan red - a gate that cannot go red is
# itself broken.
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

suites='freertos-cmsis2-smoke threadx-cmsis2-smoke'
mkdir -p "$tmp/tools/mock" "$tmp/tests/host"
printf 'SUITES := %s\n' "$suites" > "$tmp/tools/mock/suites.mk"
for suite in $suites; do
    mkdir -p "$tmp/tests/host/$suite"
    cp "$root/tests/host/$suite/Makefile" "$tmp/tests/host/$suite/Makefile"
done

self=$0
selftest_registry=$tmp/tools/mock/suites.mk
failed=

expect() {
    want=$1
    label=$2
    set +e
    out=$("$self" --registry "$selftest_registry" 2>&1)
    rc=$?
    set -e
    if [ "$rc" -eq "$want" ]; then
        echo "PASS: selftest $label (exit $rc)"
    else
        echo "FAIL: selftest $label (exit $rc, want $want)"
        printf '%s\n' "$out" | sed 's/^/    /'
        failed=1
    fi
}

expect 0 "pristine copies"
sed 's/-MMD -MP //' "$tmp/tests/host/freertos-cmsis2-smoke/Makefile" > "$tmp/mk.tmp"
mv "$tmp/mk.tmp" "$tmp/tests/host/freertos-cmsis2-smoke/Makefile"
expect 1 "dependency tracking dropped"
cp "$root/tests/host/freertos-cmsis2-smoke/Makefile" "$tmp/tests/host/freertos-cmsis2-smoke/Makefile"
sed '/^-include/d' "$tmp/tests/host/threadx-cmsis2-smoke/Makefile" > "$tmp/mk.tmp"
mv "$tmp/mk.tmp" "$tmp/tests/host/threadx-cmsis2-smoke/Makefile"
expect 1 "dependency include dropped"
cp "$root/tests/host/threadx-cmsis2-smoke/Makefile" "$tmp/tests/host/threadx-cmsis2-smoke/Makefile"
sed 's|rm -rf \$(BUILD_DIR)$|rm -rf $(BUILD_DIR)$(if $(filter off,$(PATCHES)),,/pinned)|' \
    "$tmp/tests/host/threadx-cmsis2-smoke/Makefile" > "$tmp/mk.tmp"
mv "$tmp/mk.tmp" "$tmp/tests/host/threadx-cmsis2-smoke/Makefile"
expect 1 "clean re-suffixed"

if [ -n "$failed" ]; then
    echo "check-host-makefiles: FAIL (selftest)"
    exit 1
fi
echo "check-host-makefiles: PASS (selftest: pristine green, every mutation red)"

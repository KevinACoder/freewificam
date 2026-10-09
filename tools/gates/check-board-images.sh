#!/bin/sh
# Gate: the build shape of every registered board image.
#
# `make board-images` compiles each suite the registry names, one Makefile per
# image - but the aggregate stops at the first failure, and a stale object in a
# build directory can hide a compile-time defect entirely. Two shapes in those
# Makefiles are load-bearing and are checked here from the source side:
#
#   - the per-file flag argument must actually reach the compile command: the
#     COMPILE_RULE body has to apply $(2), and no later rule may redefine the
#     same object without it (the last definition wins in make, which is how
#     the formatter's disable switches silently stopped being applied);
#   - an image that links the vendored formatter (components/printf) must link
#     with section GC (-ffunction-sections -fdata-sections plus
#     -Wl,--gc-sections): the engine's printf_/vprintf_ entry points reference
#     an unbound _putchar sink and are not called by these images, so dropping
#     them is what keeps the link resolvable (components/printf/PROVENANCE.md);
#   - an image whose banner carries the build tag must rebuild when the tag
#     changes (a stamp the objects depend on), or a stale banner from an
#     earlier tree is certified as the committed build.
#
# Usage:
#   tools/gates/check-board-images.sh               # scan the registered suites
#   tools/gates/check-board-images.sh --selftest    # mutate copies; every case must go red
#   tools/gates/check-board-images.sh --registry <suites.mk>
# Exit status: 0 clean, 1 findings.

set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
registry=$root/tests/board/suites.mk

case "${1:-}" in
    --selftest)
        selftest=1
        ;;
    --registry)
        registry=${2:?usage: check-board-images.sh [--registry <suites.mk>]}
        selftest=0
        ;;
    "")
        selftest=0
        ;;
    *)
        echo "usage: check-board-images.sh [--selftest] [--registry <suites.mk>]" >&2
        exit 2
        ;;
esac

# The findings for one Makefile; empty means clean.
bad=

check_makefile() {
    mk=$1
    bad=
    rule=$(awk '/^define[[:space:]]+COMPILE_RULE/{f=1} f{print} f&&/^endef/{exit}' "$mk")
    if [ -z "$rule" ]; then
        bad="$bad no-COMPILE_RULE"
    elif ! printf '%s\n' "$rule" | grep -qF '$(2)'; then
        bad="$bad per-file-flags-argument-not-applied"
    fi
    if grep -qF 'PRINTF_SRCS' "$mk"; then
        grep -qF -- '-ffunction-sections' "$mk" || bad="$bad no--ffunction-sections"
        grep -qF -- '-fdata-sections' "$mk" || bad="$bad no--fdata-sections"
        grep -qF -- '--gc-sections' "$mk" || bad="$bad no--gc-sections"
        grep -qF -- 'PRINTF_DISABLE_SUPPORT_FLOAT' "$mk" || bad="$bad no-float-switch"
        grep -qF -- 'PRINTF_DISABLE_SUPPORT_EXPONENTIAL' "$mk" || bad="$bad no-exponential-switch"
    fi
    if grep -qF 'FW_BUILD_TAG' "$mk"; then
        grep -qF 'TAG_STAMP :=' "$mk" || bad="$bad no-build-tag-stamp"
        grep -qF '$(TAG_STAMP)' "$mk" || bad="$bad build-tag-stamp-unused"
    fi
    [ -z "$bad" ]
}

scan() {
    status=0
    list=$(sed -n 's/^[[:space:]]*SUITES_BOARD[[:space:]]*:=[[:space:]]*//p' "$registry")
    if [ -z "$list" ]; then
        echo "check-board-images: FAIL (registry $registry names no suites)"
        return 1
    fi
    board=$(dirname "$registry")
    for suite in $list; do
        if [ ! -f "$board/$suite/Makefile" ]; then
            echo "FAIL: $suite: no Makefile under $board/$suite"
            status=1
            continue
        fi
        if check_makefile "$board/$suite/Makefile"; then
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
        echo "check-board-images: PASS (build shape of every registered image)"
        exit 0
    fi
    echo "check-board-images: FAIL (build shape)"
    exit 1
fi

# The selftest mutates copies of the real Makefiles: the pristine copies must
# pass, and each mutation must turn the scan red - a gate that cannot go red is
# itself broken.
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

suites='freertos-rk3568-smoke threadx-rk3568-smoke'
mkdir -p "$tmp/tests/board"
printf 'SUITES_BOARD := %s\n' "$suites" > "$tmp/tests/board/suites.mk"
for suite in $suites; do
    mkdir -p "$tmp/tests/board/$suite"
    cp "$root/tests/board/$suite/Makefile" "$tmp/tests/board/$suite/Makefile"
done

self=$0
selftest_registry=$tmp/tests/board/suites.mk
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
sed 's/\$(2) //' "$tmp/tests/board/freertos-rk3568-smoke/Makefile" > "$tmp/mk.tmp"
mv "$tmp/mk.tmp" "$tmp/tests/board/freertos-rk3568-smoke/Makefile"
expect 1 "per-file flags dropped"
cp "$root/tests/board/freertos-rk3568-smoke/Makefile" "$tmp/tests/board/freertos-rk3568-smoke/Makefile"
sed 's/--gc-sections//' "$tmp/tests/board/threadx-rk3568-smoke/Makefile" > "$tmp/mk.tmp"
mv "$tmp/mk.tmp" "$tmp/tests/board/threadx-rk3568-smoke/Makefile"
expect 1 "section GC dropped"
cp "$root/tests/board/threadx-rk3568-smoke/Makefile" "$tmp/tests/board/threadx-rk3568-smoke/Makefile"
sed '/TAG_STAMP :=/d' "$tmp/tests/board/threadx-rk3568-smoke/Makefile" > "$tmp/mk.tmp"
mv "$tmp/mk.tmp" "$tmp/tests/board/threadx-rk3568-smoke/Makefile"
expect 1 "build-tag stamp dropped"

if [ -n "$failed" ]; then
    echo "check-board-images: FAIL (selftest)"
    exit 1
fi
echo "check-board-images: PASS (selftest: pristine green, every mutation red)"

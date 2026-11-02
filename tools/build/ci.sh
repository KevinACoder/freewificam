#!/bin/sh
# tools/build/ci.sh - build every defconfig that belongs to one board.
#
# Exposed as `./make.rtos rk3568_ci|firefly_v1_ci|firefly_v2_ci` and as
# `make ...` from the repository root.  The board is read from the defconfig:
# rk3568-itx is the default board and is therefore implicit (no
# CONFIG_BOARD_RK3568_ITX line); the firefly boards are explicit choices.
#
# One clean build per profile, in defconfig-name order.  Failures are
# collected, all profiles are still attempted, and the script exits nonzero if
# any failed.  Per-profile logs go to build/ci/<target>/ (git-ignored).

set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
target=${1:-}
case "$target" in
    rk3568_ci)     board=rk3568-itx ;;
    firefly_v1_ci) board=firefly-v1 ;;
    firefly_v2_ci) board=firefly-v2 ;;
    *)
        echo "ci: usage: tools/build/ci.sh rk3568_ci|firefly_v1_ci|firefly_v2_ci" >&2
        exit 2
        ;;
esac

configs="$root/tests/board/rtos/configs"
logs="$root/build/ci/$target"
mkdir -p "$logs"

echo "ci: $target board=$board"
"$root/make.rtos" check >"$logs/check.log" 2>&1 || {
    echo "ci: FAIL check (log: $logs/check.log)" >&2
    exit 1
}

profiles=""
for f in "$configs"/*.defconfig; do
    [ -e "$f" ] || continue
    case "$board" in
        rk3568-itx)
            grep -q '^CONFIG_BOARD_FIREFLY_' "$f" && continue
            ;;
        firefly-v1)
            grep -q '^CONFIG_BOARD_FIREFLY_V1=y' "$f" || continue
            ;;
        firefly-v2)
            grep -q '^CONFIG_BOARD_FIREFLY_V2=y' "$f" || continue
            ;;
    esac
    profiles="$profiles $(basename "$f" .defconfig)"
done

[ -n "$profiles" ] || {
    echo "ci: no profiles for board $board" >&2
    exit 1
}

n=0
failed=0
for p in $profiles; do
    n=$((n + 1))
    echo "ci: [$n] $p"
    if ! "$root/make.rtos" clean >"$logs/$p.clean.log" 2>&1; then
        echo "ci: FAIL clean $p (log: $logs/$p.clean.log)" >&2
        failed=$((failed + 1))
        continue
    fi
    if ! PROFILE="$p" "$root/make.rtos" image >"$logs/$p.log" 2>&1; then
        echo "ci: FAIL build $p (log: $logs/$p.log)" >&2
        failed=$((failed + 1))
    fi
done

echo "ci: $target: $n profiles, $failed failures"
[ "$failed" -eq 0 ]

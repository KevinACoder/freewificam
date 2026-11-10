#!/bin/sh
# tools/build/ci.sh - build every defconfig of the one board this line carries.
#
# Exposed as `./make.rtos rk3568_ci`.  Every defconfig under
# tests/board/rtos/configs/ is an rk3568-itx profile (the board symbol is the
# Kconfig default), so no board filter is needed.
#
# One clean build per profile, in defconfig-name order.  Failures are
# collected, all profiles are still attempted, and the script exits nonzero if
# any failed.  Per-profile logs go to build/ci/<target>/ (git-ignored).

set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
target=${1:-}
case "$target" in
    rk3568_ci) board=rk3568-itx ;;
    *)
        echo "ci: usage: tools/build/ci.sh rk3568_ci" >&2
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
    # A profile that explicitly selects another board is not this target's.
    grep -q '^CONFIG_BOARD_FIREFLY_' "$f" && continue
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

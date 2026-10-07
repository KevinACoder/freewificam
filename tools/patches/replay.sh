#!/bin/sh
# @file    replay.sh
# @brief   Materialise the tree to compile from: the vendored pin, or a copy of it with
#          the declared patches applied (build-time replay, never an edit in place).
# @author  zhugengyu
# @date    07.10.2026
# @design patch series replay - ref linux:scripts/patch-kernel:149
#
# Usage:
#   tools/patches/replay.sh <repo>       print the source root for third-party/<repo>
#
# With no patch files under patches/<repo>/ the vendored tree is printed unchanged, so a
# repository without patches builds exactly as before. With patches, the tree is copied
# under tools/mock/build/patched/<repo>/ and every patch is applied in name order; the
# caller compiles from the printed path. Failures are loud: a patch that does not apply,
# or a vendored tree that carries local modifications, stops the build (exit 2) instead of
# silently testing something other than the recorded pin.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)

repo=${1:-}
if [ -z "$repo" ]; then
    echo "replay: usage: replay.sh <repo>" >&2
    exit 2
fi

vendor=$root/third-party/$repo
patchdir=$root/patches/$repo
dest=$root/tools/mock/build/patched/$repo

if [ ! -d "$vendor" ]; then
    echo "replay: $vendor is missing" >&2
    exit 2
fi

# No patches: the pin itself is the build input.
set -- "$patchdir"/*.patch
if [ ! -e "$1" ]; then
    printf '%s\n' "$vendor"
    exit 0
fi

# A modified vendored tree would make the compiled bytes match no recorded state; the
# patch channel is the only way in.
if [ -n "$(git -C "$vendor" status --porcelain 2>/dev/null)" ]; then
    echo "replay: third-party/$repo has local modifications - patches are the only channel" >&2
    exit 2
fi

pin=$(git -C "$vendor" rev-parse HEAD 2>/dev/null || echo unknown)
stamp=$( { printf 'pin=%s\n' "$pin"; for p in "$@"; do sha256sum "$p"; done; } \
         | sha256sum | cut -d' ' -f1 )

if [ "$(cat "$dest/.replay-stamp" 2>/dev/null || true)" != "$stamp" ]; then
    rm -rf "$dest"
    mkdir -p "$dest"
    # Copy without the submodule's .git pointer: the copy is a build input, not a repo.
    tar -C "$vendor" --exclude=.git -cf - . | tar -C "$dest" -xf -
    # patch(1), not git apply: the copy lives inside this repository, and git apply
    # silently "skips" patches whose target files are untracked there.  --fuzz=0 keeps
    # the application exact: a drifted pin must fail, not apply at an offset.
    for p in "$@"; do
        # patch(1) reports on stdout; keep stdout for the path this script prints.
        if ! patch -p1 -d "$dest" --fuzz=0 --no-backup-if-mismatch < "$p" >&2; then
            echo "replay: $(basename "$p") does not apply to $repo at $pin" >&2
            exit 2
        fi
    done
    printf '%s\n' "$stamp" > "$dest/.replay-stamp"
fi

printf '%s\n' "$dest"

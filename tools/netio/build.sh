#!/usr/bin/env bash
# Build the pinned NETIO peer on a Linux host (the Pi in the bench).
#
# The source tree is vendored unmodified under third-party/netio.  Building it
# out of tree keeps that tree pristine: the peer must stay byte-identical to
# the upstream pin because it is the one component shared by every arm.
#
#   tools/netio/build.sh
#   tools/netio/build.sh --stage pi@192.168.0.17:/tmp/netio
set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
out=$root/build/netio
stage=

while [ $# -gt 0 ]; do
    case $1 in
    --out) out=$2; shift 2 ;;
    --stage) stage=$2; shift 2 ;;
    -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
    *) echo "netio/build: unknown option $1" >&2; exit 2 ;;
    esac
done

rm -rf "$out"
mkdir -p "$out"
cp -a "$root/third-party/netio/." "$out/"
make -C "$out" linux >/dev/null

bin=$out/netio
[ -x "$bin" ] || { echo "netio/build: no binary at $bin" >&2; exit 1; }

echo "netio: $(sha256sum "$bin" | cut -d' ' -f1)  $bin"
echo "usage: netio -u -b 64 <board-ip>    # UDP peer; -t for TCP"

if [ -n "$stage" ]; then
    host=${stage%%:*}
    path=${stage#*:}
    [ "$path" = "$stage" ] && path=/tmp/netio
    scp -q "$bin" "$host:$path"
    echo "netio: staged to $host:$path"
fi

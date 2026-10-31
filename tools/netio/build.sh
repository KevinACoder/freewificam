#!/usr/bin/env bash
# Build the pinned NETIO peer and the pps sender on a Linux host.
#
# The NETIO tree is vendored unmodified under third-party/netio.  Building it
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
    -h|--help) sed -n '2,11p' "$0"; exit 0 ;;
    *) echo "netio/build: unknown option $1" >&2; exit 2 ;;
    esac
done

rm -rf "$out"
mkdir -p "$out"

cp -a "$root/third-party/netio/." "$out/"
make -C "$out" linux >/dev/null
netio=$out/netio
[ -x "$netio" ] || { echo "netio/build: no binary at $netio" >&2; exit 1; }

gcc -O2 -pthread -o "$out/ppsgen" "$root/tools/netio/ppsgen.c"
ppsgen=$out/ppsgen
[ -x "$ppsgen" ] || { echo "netio/build: no binary at $ppsgen" >&2; exit 1; }

echo "netio:  $(sha256sum "$netio" | cut -d' ' -f1)  $netio"
echo "ppsgen: $(sha256sum "$ppsgen" | cut -d' ' -f1)  $ppsgen"
echo "usage: netio -u -b 64 <board-ip>             # UDP peer; -t for TCP"
echo "       ppsgen -d <board-ip> -p 18768 ...     # 64 B pps sender"

if [ -n "$stage" ]; then
    host=${stage%%:*}
    path=${stage#*:}
    [ "$path" = "$stage" ] && path=/tmp/netio
    scp -q "$netio" "$ppsgen" "$host:$path"
    echo "netio: staged netio and ppsgen to $host:$path"
fi

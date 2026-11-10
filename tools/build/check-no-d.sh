#!/bin/sh
# Fail if a compile or link command for the listed profiles carries a -D
# option.  The generated config header and the fixed build_defs.h are the only
# places a configuration macro may come from; this gate is what makes that a
# rule instead of a convention.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
build="$root/tests/board/rtos/build"
python=${PYTHON:-python3}
cross=${AARCH64_CROSS_PATH:-}
profiles=${*:-}
if [ -z "$profiles" ]; then
	profiles="rk3568-tick rk3568-net rk3568-pcie-nvme rk3568-emmc rk3568-usb-msc"
fi
status=0
for p in $profiles; do
	"$python" "$root/tools/build/kconfig-gen.py" --profile "$p" >/dev/null
	out=$(make -C "$build" -Bn image PROFILE="$p" \
		AARCH64_CROSS_PATH="$cross" 2>/dev/null || true)
	bad=$(printf '%s\n' "$out" | grep -E 'aarch64-none-elf-(gcc|ld|ar)' | grep -- ' -D' || true)
	if [ -n "$bad" ]; then
		echo "FAIL: $p: a compile/link command carries -D" >&2
		printf '%s\n' "$bad" | head -3 >&2
		status=1
	else
		echo "ok: $p"
	fi
done
exit "$status"

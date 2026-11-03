#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
#
# L1 consumer check for the published SDK.
#
# What it proves, in order:
#   1. every public header is self-contained: it compiles alone with the host
#      compiler against ONLY dist/<profile>/include;
#   2. the generated configuration reached the consumer (compile-time asserts
#      in consumer.c);
#   3. a tiny consumer compiles with the pkg-config cflags (no -D, no internal
#      include path) and links with the pkg-config libs (no internal -L) to a
#      fully resolved AArch64 static executable;
#   4. the linked executable really contains the public log symbol.
#
# It does NOT run the executable: the archives are bare-metal AArch64 and the
# board is not part of a build checkpoint.  "Link successfully" is the claim.
#
# Usage: tests/host/sdk-consumer/check.sh [profile]
#        (run `./make.rtos sdk PROFILE=<profile>` first, or use
#         `./make.rtos sdk-check PROFILE=<profile>`)

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)

if [ $# -ge 1 ] && [ -n "${1:-}" ]; then
	profile=$1
else
	profile=$(sed -n 's/^[[:space:]]*CONFIG[[:space:]]*:=[[:space:]]*//p' \
		"$root/tests/board/rtos/config.mk" | head -n1)
fi

dist="$root/dist/$profile"
src="$here/consumer.c"
cc=${CC:-aarch64-none-elf-gcc}
hostcc=${HOSTCC:-cc}
pkg_config=${PKG_CONFIG:-pkg-config}
nm=${NM:-aarch64-none-elf-nm}
readelf=${READELF:-aarch64-none-elf-readelf}
work="$root/build/sdk-consumer/$profile"

fail() {
	echo "FAIL: $*" >&2
	exit 1
}

[ -d "$dist" ] || fail "no $dist; run ./make.rtos sdk PROFILE=$profile first"
[ -f "$dist/freewificam.pc" ] || fail "$dist/freewificam.pc is missing"
[ -f "$dist/manifest.json" ] || fail "$dist/manifest.json is missing"
[ -f "$src" ] || fail "$src is missing"

mkdir -p "$work"

echo "== sdk-consumer: profile=$profile"
echo "   dist=$dist"

# --- 1. public headers are self-contained ---------------------------------
for h in fw_config.h fw_osal.h fw_bus.h fw_netif.h fw_wifi.h fw_log.h \
	 fw_core.h; do
	[ -f "$dist/include/freewificam/$h" ] || fail "missing public header $h"
	printf '#include <freewificam/%s>\n' "$h" | \
		"$hostcc" -std=c11 -fsyntax-only -I "$dist/include" -x c - || \
		fail "$h does not compile alone"
done
echo "   headers: 7/7 self-contained (host syntax check, dist include only)"

# The forwarded component headers are data, not a second public surface: make
# sure the published tree did not leak a developer path.
if grep -R -n -E '/home/|/tmp/|/workspace/' "$dist/include" >/dev/null 2>&1; then
	grep -R -n -E '/home/|/tmp/|/workspace/' "$dist/include" >&2
	fail "the published include tree contains a local path"
fi

# --- 2. pkg-config contract ------------------------------------------------
cflags=$(PKG_CONFIG_PATH="$dist" PKG_CONFIG_LIBDIR="$dist" \
	"$pkg_config" --cflags freewificam)
libs=$(PKG_CONFIG_PATH="$dist" PKG_CONFIG_LIBDIR="$dist" \
	"$pkg_config" --static --libs freewificam)

case " $cflags " in
	*" -I$dist/include "*) ;;
	*) fail "pkg-config cflags do not point at $dist/include: $cflags" ;;
esac
case " $cflags " in
	*" -D"*) fail "pkg-config cflags carry a -D: $cflags" ;;
esac
case " $libs " in
	*" -L$dist/lib "*) ;;
	*) fail "pkg-config libs do not point at $dist/lib: $libs" ;;
esac
case " $libs " in
	*"/components/"*|*"/third-party/"*|*"/tests/"*)
		fail "pkg-config libs leak an internal path: $libs" ;;
esac
echo "   pkg-config: cflags=$cflags"
echo "                libs=$libs"

# --- 3. compile and link ---------------------------------------------------
case "$profile" in
	*[!A-Za-z0-9_.-]*) fail "unsafe profile name: $profile" ;;
esac
# shellcheck disable=SC2086
"$cc" -std=c11 -O2 -Wall -Wextra $cflags -c "$src" -o "$work/consumer.o"
# shellcheck disable=SC2086
"$cc" -nostdlib -static -Wl,-e,main "$work/consumer.o" $libs \
	-o "$work/consumer.elf"

echo "   link: $cc -nostdlib -static -Wl,-e,main consumer.o <pkg-config libs>"

# --- 4. the link result is what we claim -----------------------------------
"$readelf" -h "$work/consumer.elf" | grep -q 'AArch64' || \
	fail "the consumer is not an AArch64 executable"

"$readelf" -sW "$work/consumer.elf" | grep -q ' log_level_get$' || \
	fail "log_level_get is not in the linked image"

if "$nm" -u "$work/consumer.elf" | grep -q .; then
	"$nm" -u "$work/consumer.elf" >&2
	fail "the linked consumer still has undefined symbols"
fi

libs_count=$(ls "$dist/lib"/*.a 2>/dev/null | wc -l)
manifest_profile=$(sed -n 's/.*"profile": "\([^"]*\)".*/\1/p' "$dist/manifest.json" | head -n1)
[ "$manifest_profile" = "$profile" ] || \
	fail "manifest profile is '$manifest_profile', expected '$profile'"
echo "   manifest: profile=$manifest_profile libraries=$libs_count"

echo "PASS: sdk-consumer $profile (configured, compiled, linked, resolved)"

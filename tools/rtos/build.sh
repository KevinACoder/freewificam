#!/bin/sh
# @file    build.sh
# @brief   Compatibility entry for the RTOS build; new work uses ./make.rtos.
#
# The image is a function of one defconfig under tests/board/rtos/configs/, and
# ./make.rtos is the entry that selects it, regenerates the fragment and runs
# the platform Makefile.  This script remains so the commands in older notes,
# RELEASE.md and board automation keep working; it forwards and says so once on
# stderr.  It does not edit tests/board/rtos/config.mk.
#
#   tools/rtos/build.sh                 one image of the default profile
#   tools/rtos/build.sh images          both backends of it
#   PROFILE=<name> tools/rtos/build.sh  one image of <name>
#   tools/rtos/build.sh host|host-test|digest
#
# Jobs come from JOBS; the toolchain from AARCH64_CROSS_PATH, as before.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)

target=${1:-all}
case "$target" in
	all|"")
		set -- image
		;;
	image|images|host|host-test|digest|check|clean|distclean|list|config)
		;;
	*)
		echo "build.sh: unknown target '$target'; use ./make.rtos help" >&2
		exit 2
		;;
esac

echo "build.sh: deprecated; forwarding to ./make.rtos $*" >&2
exec "$root/make.rtos" "$@"

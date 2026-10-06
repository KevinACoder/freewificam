#!/bin/sh
# @file    build.sh
# @brief   Configure and build the upstream ThreadX regression suite in an external
#          build directory.
# @author  zhugengyu
# @date    07.10.2026
# @design adapter - ref threadx:scripts/cmake_bootstrap.sh:35
#
# The build directory lives under this suite's own tree, never inside the vendored
# ThreadX checkout: the upstream CMake entry derives every path from its own location,
# so an external -B directory leaves the source tree byte-identical.
#
# -DBUILD_SHARED_LIBS=ON is part of the upstream invocation (scripts/cmake_bootstrap.sh
# passes it) and is required here: the coverage configurations add the profiling flags as
# PRIVATE link options of the kernel library target, which a static archive cannot carry
# to the test executables - the link would fail on the profiling runtime symbols.
#
# THREADX_REGRESSION_CONFIG selects one of the five upstream configurations; the default
# is the one upstream's CI uses for its first pass.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
threadx=$root/third-party/threadx

config=${THREADX_REGRESSION_CONFIG:-default_build_coverage}
build=$here/build/$config

cmake -S "$threadx/test/tx/cmake" -B "$build" -GNinja \
      -DBUILD_SHARED_LIBS=ON \
      -DCMAKE_TOOLCHAIN_FILE="$threadx/cmake/linux.cmake" \
      -DCMAKE_BUILD_TYPE="$config"

cmake --build "$build"

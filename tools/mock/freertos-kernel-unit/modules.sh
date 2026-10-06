#!/bin/sh
# @file    modules.sh
# @brief   Shared declarations of the freertos-kernel-unit Gate1 suite: which upstream
#          modules R1 covers, where the vendored tree and the kernel pin live, and the
#          make overrides that adapt the upstream build without editing one byte.
# @author  zhugengyu
# @date    07.10.2026
# @design adapter - ref threadx:test/tx/regression/CMakeLists.txt:1
#
# Sourced by build.sh and run-tests.sh, so the module table and the override set exist
# once. All paths are relative to the repository root; the callers cd there first.

# R1 covers the five modules whose pipeline needs no unifdef. timers, tasks and smp
# follow in R2: they expand FreeRTOSConfig.def through unifdef.
FKU_MODULES="queue event_groups list stream_buffer message_buffer"

# Per-binary wall-clock limit. The whole R1 suite is expected to run in seconds; the
# limit only exists so that a hung binary fails the case instead of the container.
FKU_BIN_TIMEOUT=120

FKU_VENDORED=third-party/FreeRTOS-Kernel-tests
FKU_KERNEL=third-party/FreeRTOS-Kernel
FKU_OUT=tools/mock/build/freertos-kernel-unit/out
FKU_LOGS=tools/mock/build/freertos-kernel-unit/logs

# The kernel commit these test bytes were collected against (V11.3.1). A kernel bump
# that does not re-collect the tests must fail here rather than quietly exercise a
# different kernel; see third-party/FreeRTOS-Kernel-tests.provenance.md for the rule.
FKU_KERNEL_PIN=3a22924e0a9ddbbc8b0758881c33b3422a5cc20d

# fku_make DIR [target...] - run the upstream make with this repository's overrides.
# KERNEL_DIR, CC and BUILD_DIR are command-line variables: GNU make lets them override
# the file-internal assignments and passes them down to every sub-make. That is the
# whole adaptation - no upstream Makefile is edited.
fku_make() {
    _dir=$1
    shift
    make -C "$_dir" \
        KERNEL_DIR="$PWD/$FKU_KERNEL" \
        CC=gcc \
        BUILD_DIR="$PWD/$FKU_OUT" "$@"
}

# fku_suites MODULE - the suite directories of a module. A module whose Makefile
# includes testdir.mk is its own single suite and prints ".".
fku_suites() {
    if grep -qE '^SUITES' "$FKU_VENDORED/$1/Makefile"; then
        grep -E '^SUITES' "$FKU_VENDORED/$1/Makefile" | awk '{print $3}'
    else
        echo .
    fi
}

# fku_suite_dir MODULE SUITE - the make directory of one suite.
fku_suite_dir() {
    case $2 in
    .) printf '%s/%s\n' "$FKU_VENDORED" "$1" ;;
    *) printf '%s/%s/%s\n' "$FKU_VENDORED" "$1" "$2" ;;
    esac
}

# fku_bins - the built R1 test binaries, one name per line, in a stable order.
fku_bins() {
    ls "$FKU_OUT/bin" 2>/dev/null \
        | grep -E '^(queue|event_groups|list|stream_buffer|message_buffer)_' \
        | grep '_utest$' | sort
}

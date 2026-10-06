#!/bin/sh
# @file    build.sh
# @brief   Gate1 suite freertos-kernel-unit: build the R1 test binaries out of tree.
# @author  zhugengyu
# @date    07.10.2026
# @design adapter - ref threadx:test/tx/regression/CMakeLists.txt:1
#
# The vendored upstream tree builds with per-suite "bin" targets; the kernel under test
# is this repository's FreeRTOS-Kernel submodule, injected through KERNEL_DIR. Nothing
# is written inside either upstream tree: BUILD_DIR points at the mock container's
# ignored build area.
#
# Exit codes: 0 built; 2 a precondition or the build failed (the container reports that
# as a configuration/build error, never as a silent pass).

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
cd "$root"
. "$here/modules.sh"

# Preconditions: ruby generates both the CMock mocks and the Unity test runners, so its
# absence is a blocked prerequisite, not a test failure.
if ! command -v ruby > /dev/null 2>&1; then
    printf 'FREERTOS-KERNEL-UNIT blocked: ruby not found (mock and runner generation need it)\n' >&2
    exit 2
fi

kernel=$(git ls-tree HEAD -- "$FKU_KERNEL" | awk '{print $3}')
if [ "$kernel" != "$FKU_KERNEL_PIN" ]; then
    printf 'FREERTOS-KERNEL-UNIT blocked: kernel gitlink is %s, the vendored tests were collected for %s\n' \
        "$kernel" "$FKU_KERNEL_PIN" >&2
    printf 'Re-collect the test tree for the new kernel pin before running this suite.\n' >&2
    exit 2
fi

printf 'FREERTOS-KERNEL-UNIT kernel-pin %s ok\n' "$FKU_KERNEL_PIN"
printf 'FREERTOS-KERNEL-UNIT ruby %s\n' "$(ruby --version)"
printf 'FREERTOS-KERNEL-UNIT cmock %s\n' \
    "$(git -C "$FKU_VENDORED/CMock" rev-parse HEAD 2>/dev/null || echo unknown)"

mkdir -p "$FKU_OUT" "$FKU_LOGS"

for module in $FKU_MODULES; do
    for suite in $(fku_suites "$module"); do
        printf 'FREERTOS-KERNEL-UNIT build %s/%s\n' "$module" "$suite"
        fku_make "$(fku_suite_dir "$module" "$suite")" bin
    done
done

built=$(fku_bins | wc -l | tr -d ' ')
printf 'FREERTOS-KERNEL-UNIT built %s test binaries\n' "$built"

# Suite declaration: FreeRTOS host runtime contract - the 64-bit handle cases.
#
# The cases run against the pinned CMSIS-FreeRTOS adapter with the declared patches
# replayed (tools/patches/replay.sh); the suite also carries the red control that runs the
# same cases against the pinned bytes (README.md).

SUITE_NAME := freertos-host-smoke
SUITE_TIMEOUT := 120
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/freertos-cmsis2-smoke
SUITE_RUN := sh tools/mock/freertos-host-smoke/check.sh
SUITE_TRUTH_SOURCE := The CMSIS-RTOS2 API contract on a 64-bit host: an object handle must survive the upstream adapter's internal flag-bit encoding, and every case must prove its own precondition (a handle above 4 GiB) instead of passing on a low-address build
SUITE_SEGMENTS := -
SUITE_WIRELESS := 0

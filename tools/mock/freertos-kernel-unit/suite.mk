# Gate1 suite: the official FreeRTOS kernel unit tests (CMock/Unity), R1 slice.
SUITE_NAME := freertos-kernel-unit
SUITE_TIMEOUT := 900
SUITE_EXPECT := self
SUITE_BUILD := sh tools/mock/freertos-kernel-unit/build.sh
SUITE_RUN := sh tools/mock/freertos-kernel-unit/run-tests.sh
SUITE_TRUTH_SOURCE := Upstream FreeRTOS official CMock/Unity kernel unit tests, vendored verbatim from FreeRTOS/FreeRTOS @ f4fcc3b2 (kernel pin V11.3.1 @ 3a22924e): the expectations are upstream's own, not this repository's
SUITE_SEGMENTS := -
SUITE_WIRELESS := 0

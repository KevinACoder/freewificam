# Suite declaration: the ThreadX host smoke test, wrapped into container cases.
#
# The executable is built by its own Makefile under tests/host/ - the container must not
# grow a second copy of it. This suite covers the runtime face (scheduler alive, queue,
# delay, tick); the kernel API face is covered by the regression suite next to it.

SUITE_NAME := threadx-cmsis2-smoke
SUITE_TIMEOUT := 120
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/threadx-cmsis2-smoke
SUITE_RUN := sh tools/mock/threadx-cmsis2-smoke/check.sh
SUITE_TRUTH_SOURCE := The smoke test's own anchors, which encode the CMSIS-RTOS2 semantics the backend must provide (scheduler running, message queue order, tick progress)
SUITE_SEGMENTS := -
SUITE_WIRELESS := 0

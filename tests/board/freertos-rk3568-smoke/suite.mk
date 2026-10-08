# Declaration of the FreeRTOS RK3568 bring-up smoke suite. Read by the host
# runner (tools/gates) and by the repository build aggregate (make
# board-images); KEY := value, the same make-assignment syntax the host-side
# suites use.
#
# The image carries no judgment channel of its own: it prints a fixed set of
# anchor lines, and judge.py walks them in the order they appear. The ladder is
# the only counting anchor (see judge.py for the tolerances).
SUITE_NAME            := freertos-rk3568-smoke
SUITE_TRUTH_SOURCE    := the image's fixed anchor lines plus the reporter's three beat lines, each cross-checked against the emitting sites recorded in judge.py
SUITE_IMAGE           := tests/board/freertos-rk3568-smoke/build/freertos.bin
SUITE_BUILD           := make -C tests/board/freertos-rk3568-smoke
SUITE_PROFILE         := freertos
SUITE_DEPLOY          := freertos.bin
SUITE_SUCCESS_PATTERN := freertos-rk3568 bring-up
SUITE_END_PATTERN     := freertos: smoke PASS
SUITE_SUMMARY_PATTERN := freertos: tick=([0-9]+) switches=([0-9]+) cntv_ms=([0-9]+)
SUITE_CASE_NAMES      := bring-up kernel gic tick task-hi task-lo ladder pass
SUITE_JUDGE           := python3 tests/board/freertos-rk3568-smoke/judge.py
SUITE_TIMEOUT         := 90
SUITE_SEGMENTS        := -
SUITE_WIRELESS        := 0

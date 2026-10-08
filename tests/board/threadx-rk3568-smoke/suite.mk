# Declaration of the ThreadX RK3568 bring-up smoke suite. Read by the host
# runner (tools/gates) and by the repository build aggregate (make
# board-images); KEY := value, the same make-assignment syntax the host-side
# suites use.
#
# The image carries no judgment channel of its own: it prints a fixed set of
# anchor lines, and judge.py walks them in the order they appear. The ladder is
# the only counting anchor (see judge.py for the tolerances).
SUITE_NAME            := threadx-rk3568-smoke
SUITE_TRUTH_SOURCE    := the image's fixed anchor lines plus the ladder beats and the second thread's liveness count, each cross-checked against the emitting sites recorded in judge.py
SUITE_IMAGE           := tests/board/threadx-rk3568-smoke/build/threadx-rk3568.bin
SUITE_BUILD           := make -C tests/board/threadx-rk3568-smoke
SUITE_PROFILE         := rtos
SUITE_DEPLOY          := rtos.bin
SUITE_SUCCESS_PATTERN := tx: kernel entered
SUITE_END_PATTERN     := threadx: smoke PASS
SUITE_SUMMARY_PATTERN := tick: t=([0-9]+)
SUITE_CASE_NAMES      := kernel unused-mem gic tick stamp ladder thread-lo pass
SUITE_JUDGE           := python3 tests/board/threadx-rk3568-smoke/judge.py
SUITE_TIMEOUT         := 90
SUITE_SEGMENTS        := -
SUITE_WIRELESS        := 0

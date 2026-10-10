# Declaration of the ThreadX board WLAN suite. Read by the host runner
# (tools/gates) and by the repository build aggregate (make board-images);
# KEY := value, the same make-assignment syntax the host-side suites use.
#
# This image exists separately from the regression image because its cases are
# the only ones that need the dongle: a missing card reds this suite and leaves
# the regression image's green untouched.
SUITE_NAME            := threadx-rk3568-wlan
SUITE_TRUTH_SOURCE    := the suite's own BOARD-SUITE/CASE channel plus the fixed anchor lines of the WLAN chain (driver probe log, firmware revision line, scan table rows), cross-checked against the Linux-lane same-card ground truth captured on this board (the QiQiJia sweep of the ground-truth round)
SUITE_IMAGE           := tests/board/threadx-rk3568-wlan/build/threadx-rk3568-wlan.bin
SUITE_BUILD           := make -C tests/board/threadx-rk3568-wlan
SUITE_PROFILE         := rtos
SUITE_DEPLOY          := rtos.bin
SUITE_SUCCESS_PATTERN := BOARD-SUITE threadx-rk3568-wlan start
SUITE_SELECT_PATTERN  := BOARD-SUITE threadx-rk3568-wlan select
SUITE_END_PATTERN     := BOARD-SUITE threadx-rk3568-wlan result=
SUITE_SUMMARY_PATTERN := BOARD-SUITE threadx-rk3568-wlan result=pass cases=([0-9]+) ok=([0-9]+) exit=([0-9]+)
SUITE_CASE_NAMES      := shell-up wlan-probe wlan-scan wlan-join-raw wlan-join wlan-dataplane
SUITE_STEPS_FILE      := steps.tsv
SUITE_JUDGE           := python3 tests/board/threadx-rk3568-wlan/judge.py
SUITE_TIMEOUT         := 480
SUITE_SEGMENTS        := -
SUITE_WIRELESS        := 0

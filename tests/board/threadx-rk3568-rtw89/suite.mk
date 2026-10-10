# Declaration of the ThreadX board rtw89 suite. Read by the host runner
# (tools/gates) and by the repository build aggregate (make board-images);
# KEY := value, the same make-assignment syntax the host-side suites use.
#
# One image, one driver: this suite carries the rtw89 driver (RTL8851BU) and the
# WLAN suite carries rtl8xxxu, so a failure in one leaves the other's board
# rounds and its green untouched.  Both dongles can be on the board at once -
# they sit on different controllers.
SUITE_NAME            := threadx-rk3568-rtw89
SUITE_TRUTH_SOURCE    := the suite's own BOARD-SUITE/CASE channel plus the fixed anchor lines of the rtw89 bring-up chain (the driver's probe log, its firmware version line, its chip-information line, the scan table rows), cross-checked against the Linux-lane ground truth for the same chip on this board
SUITE_IMAGE           := tests/board/threadx-rk3568-rtw89/build/threadx-rk3568-rtw89.bin
SUITE_BUILD           := make -C tests/board/threadx-rk3568-rtw89
SUITE_PROFILE         := rtos
SUITE_DEPLOY          := rtos.bin
SUITE_SUCCESS_PATTERN := BOARD-SUITE threadx-rk3568-rtw89 start
SUITE_SELECT_PATTERN  := BOARD-SUITE threadx-rk3568-rtw89 select
SUITE_END_PATTERN     := BOARD-SUITE threadx-rk3568-rtw89 result=
SUITE_SUMMARY_PATTERN := BOARD-SUITE threadx-rk3568-rtw89 result=pass cases=([0-9]+) ok=([0-9]+) exit=([0-9]+)
SUITE_CASE_NAMES      := shell-up rtw89-probe rtw89-scan rtw89-join rtw89-dataplane
SUITE_STEPS_FILE      := steps.tsv
SUITE_JUDGE           := python3 tests/board/threadx-rk3568-rtw89/judge.py
SUITE_TIMEOUT         := 480
SUITE_SEGMENTS        := -
SUITE_WIRELESS        := 0

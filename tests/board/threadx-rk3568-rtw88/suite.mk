# Declaration of the ThreadX board rtw88 suite. Read by the host runner
# (tools/gates) and by the repository build aggregate (make board-images);
# KEY := value, the same make-assignment syntax the host-side suites use.
#
# One image, one driver: this suite carries the rtw88 driver (RTL8821CU) and the
# WLAN suite carries rtl8xxxu, so a failure in one leaves the other's board
# rounds and its green untouched.  Both dongles can be on the board at once -
# they sit on different controllers.
SUITE_NAME            := threadx-rk3568-rtw88
SUITE_TRUTH_SOURCE    := the suite's own BOARD-SUITE/CASE channel plus the fixed anchor lines of the rtw88 bring-up chain (the CD-ROM modeswitch flip, the class hook's claim of the network personality, the driver's firmware version line, the scan table rows), cross-checked against the Linux-lane ground truth for the same chip on this board
SUITE_IMAGE           := tests/board/threadx-rk3568-rtw88/build/threadx-rk3568-rtw88.bin
SUITE_BUILD           := make -C tests/board/threadx-rk3568-rtw88
SUITE_PROFILE         := rtos
SUITE_DEPLOY          := rtos.bin
SUITE_SUCCESS_PATTERN := BOARD-SUITE threadx-rk3568-rtw88 start
SUITE_SELECT_PATTERN  := BOARD-SUITE threadx-rk3568-rtw88 select
SUITE_END_PATTERN     := BOARD-SUITE threadx-rk3568-rtw88 result=
SUITE_SUMMARY_PATTERN := BOARD-SUITE threadx-rk3568-rtw88 result=pass cases=([0-9]+) ok=([0-9]+) exit=([0-9]+)
SUITE_CASE_NAMES      := shell-up rtw88-probe rtw88-scan rtw88-join-raw rtw88-join rtw88-dataplane rtw88-floodping
SUITE_STEPS_FILE      := steps.tsv
# The case that consumes the round's own steps (the credential): the runner
# holds --steps-extra until this line appears, so the connect lands after the
# sweep has finished and while the join case is waiting, not at a guessed
# moment during the scan (see tools/gates/board_runner.py).
SUITE_EXTRA_AFTER_PATTERN := wlan: join waiting for the supplicant
SUITE_JUDGE           := python3 tests/board/threadx-rk3568-rtw88/judge.py
SUITE_TIMEOUT         := 480
SUITE_SEGMENTS        := -
SUITE_WIRELESS        := 0

# Declaration of the FreeRTOS board regression suite. Read by the host runner
# (tools/gates) and by the repository build aggregate (make board-images);
# KEY := value, the same make-assignment syntax the host-side suites use.
SUITE_NAME            := freertos-rk3568-regression
SUITE_TRUTH_SOURCE    := the suite's own BOARD-SUITE/CASE channel, cross-checked against the fixed anchor lines of the FreeRTOS board glue, the kernel identity through the facade, the board heap, the shell port and the wired line's PHY/link/address chain
SUITE_IMAGE           := tests/board/freertos-rk3568-regression/build/freertos-rk3568-regression.bin
SUITE_BUILD           := make -C tests/board/freertos-rk3568-regression
SUITE_PROFILE         := freertos
SUITE_DEPLOY          := freertos.bin
SUITE_SUCCESS_PATTERN := BOARD-SUITE freertos-rk3568-regression start
SUITE_SELECT_PATTERN  := BOARD-SUITE freertos-rk3568-regression select
SUITE_END_PATTERN     := BOARD-SUITE freertos-rk3568-regression result=
SUITE_SUMMARY_PATTERN := BOARD-SUITE freertos-rk3568-regression result=pass cases=([0-9]+) ok=([0-9]+) exit=([0-9]+)
SUITE_CASE_NAMES      := shell-up kernel-up tick-ladder thread-sched cmsis2-prio freertos-sched os-timer usb-xhci usb-ehci lwip-loopback gmac-up
SUITE_STEPS_FILE      := ../common/shell-steps.tsv
SUITE_JUDGE           := python3 tests/board/freertos-rk3568-regression/judge.py
SUITE_TIMEOUT         := 180
SUITE_SEGMENTS        := -
SUITE_WIRELESS        := 0

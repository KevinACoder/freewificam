# Declaration of the ThreadX board regression suite. Read by the host runner
# (tools/gates) and by the repository build aggregate (make board-images);
# KEY := value, the same make-assignment syntax the host-side suites use.
#
# SUITE_SELECT_PATTERN is the early anchor the runner waits for when it has a
# selection directive to send (the image's select window opens right after that
# line); SUITE_SUCCESS_PATTERN is the start line the steps are timed against.
SUITE_NAME            := threadx-rk3568-regression
SUITE_TRUTH_SOURCE    := the suite's own BOARD-SUITE/CASE channel, cross-checked against the fixed anchor lines of the kernel glue, the board heap, the CMSIS2 backend and the shell port
SUITE_IMAGE           := tests/board/threadx-rk3568-regression/build/threadx-rk3568-regression.bin
SUITE_BUILD           := make -C tests/board/threadx-rk3568-regression
SUITE_PROFILE         := rtos
SUITE_DEPLOY          := rtos.bin
SUITE_SUCCESS_PATTERN := BOARD-SUITE threadx-rk3568-regression start
SUITE_SELECT_PATTERN  := BOARD-SUITE threadx-rk3568-regression select
SUITE_END_PATTERN     := BOARD-SUITE threadx-rk3568-regression result=
SUITE_SUMMARY_PATTERN := BOARD-SUITE threadx-rk3568-regression result=pass cases=([0-9]+) ok=([0-9]+) exit=([0-9]+)
SUITE_CASE_NAMES      := shell-up kernel-up tick-ladder thread-sched cmsis2-prio tlsf-heap os-timer usb-xhci usb-ehci lwip-loopback gmac-up
SUITE_STEPS_FILE      := steps.tsv
SUITE_JUDGE           := python3 tests/board/threadx-rk3568-regression/judge.py
SUITE_TIMEOUT         := 180
SUITE_SEGMENTS        := -
SUITE_WIRELESS        := 0

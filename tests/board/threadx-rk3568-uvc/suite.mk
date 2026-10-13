# Declaration of the ThreadX board UVC suite. Read by the host runner
# (tools/gates) and by the repository build aggregate (make board-images);
# KEY := value, the same make-assignment syntax the host-side suites use.
#
# SUITE_SELECT_PATTERN is the early anchor the runner waits for when it has a
# selection directive to send (the image's select window opens right after that
# line); SUITE_SUCCESS_PATTERN is the start line the steps are timed against.
SUITE_NAME            := threadx-rk3568-uvc
SUITE_TRUTH_SOURCE    := the suite's own BOARD-SUITE/CASE channel, cross-checked against the camera's fixed anchors (bus-0 controller init, the video class connect line, the negotiated open line) and against the Linux ground-truth capture of the same camera on this board (the board wiki's webcam round: 640x480 MJPEG, 30 fps nominal, 11,233 B/frame)
SUITE_IMAGE           := tests/board/threadx-rk3568-uvc/build/threadx-rk3568-uvc.bin
SUITE_BUILD           := make -C tests/board/threadx-rk3568-uvc
SUITE_PROFILE         := rtos
SUITE_DEPLOY          := rtos.bin
SUITE_SUCCESS_PATTERN := BOARD-SUITE threadx-rk3568-uvc start
SUITE_SELECT_PATTERN  := BOARD-SUITE threadx-rk3568-uvc select
SUITE_END_PATTERN     := BOARD-SUITE threadx-rk3568-uvc result=
SUITE_SUMMARY_PATTERN := BOARD-SUITE threadx-rk3568-uvc result=pass cases=([0-9]+) ok=([0-9]+) exit=([0-9]+)
SUITE_CASE_NAMES      := shell-up usb-host-up uvc-open uvc-iso-smoke uvc-stream
SUITE_STEPS_FILE      := steps.tsv
SUITE_JUDGE           := python3 tests/board/threadx-rk3568-uvc/judge.py
SUITE_TIMEOUT         := 240
SUITE_SEGMENTS        := -
SUITE_WIRELESS        := 0

# Declaration of the CherrySH RK3568 shell suite. Read by the host runner
# (tools/gates) and by the repository build aggregate (make board-images);
# KEY := value, the same make-assignment syntax the host-side suites use.
#
# The image deploys under the fixed name of the profile it boots from (the
# shell image is served to the board as freertos.bin).
#
# This suite is step-only: there is no summary line to capture, so no
# SUITE_SUMMARY_PATTERN is declared - judge.py judges the shell's responses to
# the shared interaction steps (SUITE_STEPS_FILE, resolved relative to this
# directory).
SUITE_NAME            := cherrysh-rk3568-smoke
SUITE_TRUTH_SOURCE    := the shell's own responses to the declared interaction steps plus the image's bring-up anchor lines, each cross-checked against the emitting sites recorded in judge.py
SUITE_IMAGE           := tests/board/cherrysh-rk3568-smoke/build/cherrysh.bin
SUITE_BUILD           := make -C tests/board/cherrysh-rk3568-smoke
SUITE_PROFILE         := freertos
SUITE_DEPLOY          := freertos.bin
SUITE_SUCCESS_PATTERN := cherrysh-rk3568 bring-up
SUITE_END_PATTERN     := \$ exit
SUITE_CASE_NAMES      := prompt help-echo tab-complete clear-run exit-restore
SUITE_STEPS_FILE      := ../common/shell-steps.tsv
SUITE_JUDGE           := python3 tests/board/cherrysh-rk3568-smoke/judge.py
SUITE_TIMEOUT         := 90
SUITE_SEGMENTS        := -
SUITE_WIRELESS        := 0

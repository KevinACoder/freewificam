# Negative fixture: the declaration carries no SUITE_END_PATTERN.
#
# The end sentinel is what makes "silence" distinguishable from "passed":
# without the pattern the runner can never tell an incomplete run from a
# finished one, so the declaration is a configuration error and --validate
# must reject it (exit 2). One field removed, nothing else broken.
#
# @design declarative registry entry, negative instance - ref
#          tests/board/threadx-rk3568-regression/suite.mk (one field broken).
SUITE_NAME            := neg-no-end-pattern
SUITE_TRUTH_SOURCE    := the synthetic BOARD-SUITE/CASE judgment channel of the reference fixtures, reconciled against the independent BOARD-SUMMARY anchor and the end sentinel
SUITE_IMAGE           := tests/board/fixtures/neg-no-end-pattern/build/neg-no-end-pattern.bin
SUITE_PROFILE         := rtos
SUITE_DEPLOY          := rtos.bin
SUITE_SUCCESS_PATTERN := BOARD-SUITE neg-no-end-pattern start
SUITE_SUMMARY_PATTERN := BOARD-SUMMARY neg-no-end-pattern cases=([0-9]+) ok=([0-9]+) exit=([0-9]+)
SUITE_JUDGE           := python3 tests/board/fixtures/ref-judge/judge.py
SUITE_TIMEOUT         := 90
SUITE_SEGMENTS        := -
SUITE_WIRELESS        := 0

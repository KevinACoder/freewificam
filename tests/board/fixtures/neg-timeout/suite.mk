# Negative fixture: SUITE_TIMEOUT is not a number.
#
# The timeout is the serial_wait bound; a value that cannot be turned into
# seconds leaves the collection stage undefined, so --validate must reject it
# (exit 2). Only this one field is broken.
#
# @design declarative registry entry, negative instance - ref
#          tests/board/threadx-rk3568-regression/suite.mk (one field broken).
SUITE_NAME            := neg-timeout
SUITE_TRUTH_SOURCE    := the synthetic BOARD-SUITE/CASE judgment channel of the reference fixtures, reconciled against the independent BOARD-SUMMARY anchor and the end sentinel
SUITE_IMAGE           := tests/board/fixtures/neg-timeout/build/neg-timeout.bin
SUITE_PROFILE         := rtos
SUITE_DEPLOY          := rtos.bin
SUITE_SUCCESS_PATTERN := BOARD-SUITE neg-timeout start
SUITE_END_PATTERN     := BOARD-SUITE neg-timeout result=
SUITE_SUMMARY_PATTERN := BOARD-SUMMARY neg-timeout cases=([0-9]+) ok=([0-9]+) exit=([0-9]+)
SUITE_JUDGE           := python3 tests/board/fixtures/ref-judge/judge.py
SUITE_TIMEOUT         := eventually
SUITE_SEGMENTS        := -
SUITE_WIRELESS        := 0

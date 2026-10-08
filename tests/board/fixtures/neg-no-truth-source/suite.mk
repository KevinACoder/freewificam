# Negative fixture: the declaration carries no SUITE_TRUTH_SOURCE.
#
# Every expected value must name its independent source, so a declaration
# without one is a configuration error and --validate must reject it (exit 2).
# This is the complete fixture declaration with exactly that one field removed.
#
# @design declarative registry entry, negative instance - ref
#          tests/board/threadx-rk3568-regression/suite.mk (one field broken).
SUITE_NAME            := neg-no-truth-source
SUITE_IMAGE           := tests/board/fixtures/neg-no-truth-source/build/neg-no-truth-source.bin
SUITE_PROFILE         := rtos
SUITE_DEPLOY          := rtos.bin
SUITE_SUCCESS_PATTERN := BOARD-SUITE neg-no-truth-source start
SUITE_END_PATTERN     := BOARD-SUITE neg-no-truth-source result=
SUITE_SUMMARY_PATTERN := BOARD-SUMMARY neg-no-truth-source cases=([0-9]+) ok=([0-9]+) exit=([0-9]+)
SUITE_JUDGE           := python3 tests/board/fixtures/ref-judge/judge.py
SUITE_TIMEOUT         := 90
SUITE_SEGMENTS        := -
SUITE_WIRELESS        := 0

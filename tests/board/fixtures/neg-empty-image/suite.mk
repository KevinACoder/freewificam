# Negative fixture: SUITE_IMAGE is empty.
#
# The image path is the first link of the fingerprint chain (host crc32 =
# tftp_stage return = on-board check), so an empty path would deploy nothing
# and prove nothing; --validate must reject it (exit 2). Only this one field
# is empty; every other field is the complete fixture's.
#
# @design declarative registry entry, negative instance - ref
#          tests/board/threadx-rk3568-regression/suite.mk (one field broken).
SUITE_NAME            := neg-empty-image
SUITE_TRUTH_SOURCE    := the synthetic BOARD-SUITE/CASE judgment channel of the reference fixtures, reconciled against the independent BOARD-SUMMARY anchor and the end sentinel
SUITE_IMAGE           :=
SUITE_PROFILE         := rtos
SUITE_DEPLOY          := rtos.bin
SUITE_SUCCESS_PATTERN := BOARD-SUITE neg-empty-image start
SUITE_END_PATTERN     := BOARD-SUITE neg-empty-image result=
SUITE_SUMMARY_PATTERN := BOARD-SUMMARY neg-empty-image cases=([0-9]+) ok=([0-9]+) exit=([0-9]+)
SUITE_JUDGE           := python3 tests/board/fixtures/ref-judge/judge.py
SUITE_TIMEOUT         := 90
SUITE_SEGMENTS        := -
SUITE_WIRELESS        := 0

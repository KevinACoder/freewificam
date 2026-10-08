# Negative fixture: SUITE_NAME does not match the directory name.
#
# The name is the key the registry, the deploy name and the CASE lines all
# agree on; a declaration copied without renaming would run under one name and
# report under another, so --validate must reject it (exit 2). The declared
# name here belongs to the complete fixture - exactly that one field differs.
#
# @design declarative registry entry, negative instance - ref
#          tests/board/threadx-rk3568-regression/suite.mk (one field broken).
SUITE_NAME            := fixture-accept
SUITE_TRUTH_SOURCE    := the synthetic BOARD-SUITE/CASE judgment channel of the reference fixtures, reconciled against the independent BOARD-SUMMARY anchor and the end sentinel
SUITE_IMAGE           := tests/board/fixtures/neg-name-mismatch/build/neg-name-mismatch.bin
SUITE_PROFILE         := rtos
SUITE_DEPLOY          := rtos.bin
SUITE_SUCCESS_PATTERN := BOARD-SUITE fixture-accept start
SUITE_END_PATTERN     := BOARD-SUITE fixture-accept result=
SUITE_SUMMARY_PATTERN := BOARD-SUMMARY fixture-accept cases=([0-9]+) ok=([0-9]+) exit=([0-9]+)
SUITE_JUDGE           := python3 tests/board/fixtures/ref-judge/judge.py
SUITE_TIMEOUT         := 90
SUITE_SEGMENTS        := -
SUITE_WIRELESS        := 0

# Declaration of the fixture suite: a complete, valid declaration. The host
# runner's --validate must accept it, and the fixture registry lists it.
#
# Everything here is synthetic. The image path need not exist, the deploy name
# is the platform's standard one, and the judge is the reference judge of this
# fixture set (tests/board/fixtures/ref-judge/judge.py), which speaks the same
# synthetic line protocol as the static transcripts it is exercised with.
# KEY := value, the same make-assignment syntax the board suites use.
#
# @design declarative registry entry - ref
#          tests/board/threadx-rk3568-regression/suite.mk (the same field set,
#          every field synthetic).
SUITE_NAME            := fixture-accept
SUITE_TRUTH_SOURCE    := the synthetic BOARD-SUITE/CASE judgment channel of the reference fixtures, reconciled against the independent BOARD-SUMMARY anchor and the end sentinel
SUITE_IMAGE           := tests/board/fixtures/fixture-accept/build/fixture-accept.bin
SUITE_PROFILE         := rtos
SUITE_DEPLOY          := rtos.bin
SUITE_SUCCESS_PATTERN := BOARD-SUITE fixture-accept start
SUITE_END_PATTERN     := BOARD-SUITE fixture-accept result=
SUITE_SUMMARY_PATTERN := BOARD-SUMMARY fixture-accept cases=([0-9]+) ok=([0-9]+) exit=([0-9]+)
SUITE_JUDGE           := python3 tests/board/fixtures/ref-judge/judge.py
SUITE_TIMEOUT         := 90
SUITE_SEGMENTS        := -
SUITE_WIRELESS        := 0

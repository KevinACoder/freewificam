# Suite declaration: every board suite's judge self test.
#
# The judges are what turn a board transcript into a pass or a fail, so a judge
# that cannot go red - or that stops noticing a piece of evidence - silently
# changes the meaning of every board round. Each judge carries its own self
# test: a legal transcript it must pass, plus one mutation per piece of evidence
# it must fail. This suite runs them all offline (no board, no radio), which is
# where a judge change is caught instead of on a boot.

SUITE_NAME := board-judge-selftest
SUITE_TIMEOUT := 300
SUITE_EXPECT := self
SUITE_RUN := sh tools/mock/board-judge-selftest/run.sh
SUITE_TRUTH_SOURCE := Each board suite's judge declares its own legal transcript and its own mutations (tests/board/*/judge.py: BASE_TRANSCRIPT and MUTATIONS), and its --selftest must judge the base pass and every mutation on its declared exit code
SUITE_SEGMENTS := -
SUITE_WIRELESS := 0

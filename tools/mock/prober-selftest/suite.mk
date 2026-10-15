# Suite declaration: the measurement peer's own self test.
#
# The peer (tools/prober) is the instrument the air and throughput questions of
# the wireless rounds are answered with, so its parsers and its verdict rules
# are covered here rather than by the board round that first exercises them: a
# phase line read wrongly, or an unparsable summary read as a number, sends the
# next round in the wrong direction. There is nothing to build - the self test
# is the case set, and the wrapper translates its per-check lines into the
# container's CASE grammar and proves the suite can go red.

SUITE_NAME := prober-selftest
SUITE_TIMEOUT := 120
SUITE_EXPECT := self
SUITE_RUN := sh tools/mock/prober-selftest/run.sh
SUITE_TRUTH_SOURCE := The peer's own declared semantics - the phase-line and uplink-line grammars it parses, the "a missing value is never zero" and "an empty capture is a different verdict" rules, the per-phase action grammar - each carried with its negative in the same self test
SUITE_SEGMENTS := -
SUITE_WIRELESS := 0

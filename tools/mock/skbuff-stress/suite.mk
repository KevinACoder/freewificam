# Suite declaration: the skbuff seam under stress.
#
# The harness binary is built by its own Makefile under tests/host/ - the container
# must not grow a second copy of it. It drives the skbuff seam's contract face with
# a scenario matrix, seeded operation streams and a constrained profile, and reports
# every cell, stream and control as an anchor line (`SKBST: <...>`).

SUITE_NAME := skbuff-stress
SUITE_TIMEOUT := 300
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/skbuff-stress-mock
SUITE_RUN := sh tools/mock/skbuff-stress/check.sh
SUITE_TRUTH_SOURCE := Linux skbuff semantics for the geometry and queue operations (the in-repo seam contract headers), plus the harness's own invariants: pairing conservation, named misuse counters and queue integrity
SUITE_SEGMENTS := M.GEOM,M.QUEUE,M.FUZZ,M.PAIR,M.NAMED
SUITE_WIRELESS := 0
SUITE_ROUTE := n/a

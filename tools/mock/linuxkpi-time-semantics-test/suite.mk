# Suite declaration: the polling budget and the completion layout.
#
# The harness binary is built by its own Makefile under tests/host/ - the
# container must not grow a second copy of it.  It drives the contract header's
# polling macros and the completion object against the real linuxkpi body, and
# reports every case as an anchor line (`TIMESEM: <case> PASS|FAIL`).

SUITE_NAME := linuxkpi-time-semantics-test
SUITE_TIMEOUT := 300
SUITE_EXPECT := self
SUITE_BUILD := make -C tests/host/linuxkpi-time-semantics-test
SUITE_RUN := sh tools/mock/linuxkpi-time-semantics-test/check.sh
SUITE_TRUTH_SOURCE := upstream include/linux/iopoll.h (poll_timeout_us_atomic: the budget charges the delay and one nanosecond per round, never the operation's own time), the port's tick-based clock (lkpi_ktime_get is osKernelGetTickCount * 1e6 ns), and the completion layout the port pins in linuxkpi_impl.h (a condvar shell larger than its slot aliases the sticky marker)
SUITE_SEGMENTS := S1.TIMESEM,M.TIME
SUITE_WIRELESS := 0
SUITE_ROUTE := n/a

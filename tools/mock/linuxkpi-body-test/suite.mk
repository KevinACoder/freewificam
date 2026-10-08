# Suite declaration: the linuxkpi body linked and driven.
#
# The suite binary is built by its own Makefile under tests/host/ - the container
# must not grow a second copy of it. It links the real service bodies, the real
# skbuff pool, the real firmware registry and blob, and the real USB engine into
# one program, plus a driver registered through the module-section mechanism, and
# reports every case as an anchor line (`LKPI: <ID> ...`).

SUITE_NAME := linuxkpi-body-test
SUITE_TIMEOUT := 300
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/linuxkpi-body-test
SUITE_RUN := sh tools/mock/linuxkpi-body-test/check.sh
SUITE_TRUTH_SOURCE := the in-repo linuxkpi contract headers and the bodies under test (service semantics, the pool's named counters, the firmware registry's name resolution), USB device identifier matching per the USB 2.0 chapter 9 descriptor fields, and the module-registration mechanism the linuxkpi contract header defines
SUITE_SEGMENTS := S1.EP0_DESC_DEV,S1.EP0_SETUP,S1.TOGGLE,M.PAIR,M.NAMED
SUITE_WIRELESS := 0
SUITE_ROUTE := n/a

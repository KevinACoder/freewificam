# Suite declaration: the console level mechanism.
#
# The harness binary is built by its own Makefile under tests/host/ - the
# container must not grow a second copy of it.  It links the vendored engine and
# the shell unchanged, so the cases judge the engine's level semantics and the
# shell's line assembly, stamp, repeat gate and never-filtered promotion exactly
# as the boards run them.  The wrapper adds the compile-gate check and the two
# red controls.

SUITE_NAME := ulog-level-test
SUITE_TIMEOUT := 300
SUITE_EXPECT := self
SUITE_BUILD := make -C tests/host/ulog-level-test
SUITE_RUN := sh tools/mock/ulog-level-test/check.sh
SUITE_TRUTH_SOURCE := the vendored engine's own semantics (components/ulog/src/ulog.h: the seven-level vocabulary, the per-subscriber threshold, ALWAYS passing every threshold, ULOG_MAX_SUBSCRIBERS 6) plus the shell's outlet discipline and the never-filtered family table the judges' anchor tables generate (os/baremetal/log.c, os/baremetal/log_anchors.h)
SUITE_SEGMENTS := S1.ULOGLEVEL
SUITE_WIRELESS := 0
SUITE_ROUTE := n/a

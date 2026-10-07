# Suite declaration: the linuxkpi USB urb engine under injection stress.
#
# The harness binary is built by its own Makefile under tests/host/ - the container
# must not grow a second copy of it.  It drives the engine through the same
# synthetic controller the usb-device-model suite uses, with the injection knobs of
# this round, and reports every cell, stream and control as an anchor line
# (`URBSTRESS: <...>`).

SUITE_NAME := linuxkpi-usb-urb-stress
SUITE_TIMEOUT := 300
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/linuxkpi-usb-urb-stress
SUITE_RUN := sh tools/mock/linuxkpi-usb-urb-stress/check.sh
SUITE_TRUTH_SOURCE := Linux urb submission/completion/cancellation semantics and the USB error-code contract, the in-repo linuxkpi USB seam contract, and the harness invariants (exactly-once completion, error-code correctness, synchronous kill drain, named use-after-free, allocation balance, reentrancy prohibition)
SUITE_SEGMENTS := S1.URBSTRESS,S2.URBSTRESS,M.PAIR,M.NAMED
SUITE_WIRELESS := 0
SUITE_ROUTE := n/a

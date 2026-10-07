# Suite declaration: the USB device model and the linuxkpi USB request face.
#
# The suite binary is built by its own Makefile under tests/host/ - the container must
# not grow a second copy of it. It carries a scripted device model (enumeration,
# descriptors, class requests, the mass-storage command/data/status flow) behind the
# repository's linuxkpi USB API, and it reports every step as a segment anchor line
# (`usb-smoke: <ID> ...`).

SUITE_NAME := linuxkpi-usb-test
SUITE_TIMEOUT := 300
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/linuxkpi-usb-test
SUITE_RUN := sh tools/mock/linuxkpi-usb-test/check.sh
SUITE_TRUTH_SOURCE := USB 2.0 chapter 9 descriptors and chapter 8 toggle semantics, USB MSC BOT (bulk-only transport) rev 1.0 chapter 5/6, and the in-repo linuxkpi USB API face (components/linuxkpi/usb)
SUITE_SEGMENTS := S1.EP0_DESC_DEV,S1.EP0_ENUM_SETUP,S1.CTRL_MS_RESET,S1.CTRL_GET_MAX_LUN,S1.BOT_CBW,M.PAIR,M.NAMED
SUITE_WIRELESS := 0
SUITE_ROUTE := n/a

# Suite declaration: the EHCI isochronous engine against a controller in RAM.
#
# The suite binary is built by its own Makefile under tests/host/ - the
# container must not grow a second copy of it. It compiles the vendored EHCI
# port (port/ehci/usb_hc_ehci.c) and the engine the iso patch adds
# (port/ehci/usb_hc_ehci_iso.c, replayed from patches/cherryusb/0003) as one
# 32-bit program, with the controller's register window living in RAM, and
# asserts the iTD encoding, the frame list placement, the slot discipline and
# the completion mapping; the wrapper adds the red control.

SUITE_NAME := ehci-iso-schedule-test
SUITE_TIMEOUT := 120
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/ehci-iso-schedule-test
SUITE_RUN := sh tools/mock/ehci-iso-schedule-test/check.sh
SUITE_TRUTH_SOURCE := the EHCI specification's iTD encoding (the TSCL transaction fields and the buffer pointer page window, paragraph 3.3), the USB 2.0 high-speed period encoding (chapter 9.6.6: period = 2^(bInterval-1) micro-frames), and the port's declared urb contract (common/usb_hc.h, the iso_packet fields)
SUITE_SEGMENTS := -
SUITE_WIRELESS := 0
SUITE_ROUTE := n/a

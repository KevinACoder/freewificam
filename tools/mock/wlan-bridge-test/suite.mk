# Suite declaration: the WLAN bridge chain over a scripted driver.
#
# The suite binary is built by its own Makefile under tests/host/ - the
# container must not grow a second copy of it. It links the real net80211
# stack, the real bridge (both halves), the real mac80211 substitute, the real
# driver units and the real presentation layer into one program, drives the
# whole chain over a scripted driver and a fake AP, and reports every case as
# an anchor line (`WLANBRIDGE: <ID> PASS|FAIL ...`).

SUITE_NAME := wlan-bridge-test
SUITE_TIMEOUT := 300
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/wlan-bridge-test
SUITE_RUN := sh tools/mock/wlan-bridge-test/check.sh
SUITE_TRUTH_SOURCE := the frame-test suite's proven responder shapes (management frame layouts, the 802.11-2020 clause 9.3.1.4 subtypes and clause 9.4.2 element ids cited in the suite), the port's seam contract (filter-before-sweep ordering, the EAPOL payload offset behind the 14-byte ethernet header), the CCMP exchange the frame-test suite already vector-anchors, and the skbuff pool's named counters
SUITE_SEGMENTS := B3.PROBE_REQ,B4.AUTH,B7.CCMP
SUITE_WIRELESS := 0
SUITE_ROUTE := n/a

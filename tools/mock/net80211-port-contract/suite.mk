# Gate1 suite: the net80211 port contract (first slice).
#
# The suite runs the port test directory's own four gates over the imported
# stack and the project's port layer: the seventeen translation units compile
# clean, the include discipline is checked mechanically, the port smoke links
# and runs on the ThreadX host runtime, and the kernel-isolation gates pass.
# Per-clause contract cases are a later round; this slice is what exists and is
# executable today.

SUITE_NAME := net80211-port-contract
SUITE_TIMEOUT := 900
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/net80211-port stack-compile headers
SUITE_RUN := sh tools/mock/net80211-port-contract/check.sh
SUITE_TRUTH_SOURCE := IEEE 802.11-2020 frame and IE encodings (routed per clause), RFC 3610 (CCMP), and the imported stack's compile/link contract on the CMSIS-RTOS2 port layer
SUITE_SEGMENTS := -
SUITE_WIRELESS := 1
SUITE_ROUTE := ieee80211-route: IEEE 802.11-2020 clause 9.3.3.1 (frame control fields), clause 9.4.2.1 (beacon/probe response body), clause 12.7.2 (CCMP, RFC 3610)

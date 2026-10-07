# Suite declaration: the net80211 protocol-path mock (the wireless suite).
#
# The suite binary is built by its own Makefile under tests/host/ - the container
# must not grow a second copy of it. The suite drives the imported net80211 stack
# through its protocol paths (scan, open-system authentication, association,
# encapsulation and decapsulation, fragments, AES-CCM, the inactivity timeout)
# with a driver substitute and a scripted responder, and it carries the library's
# own two host tests re-anchored onto this port.

SUITE_NAME := net80211-frame-test
SUITE_TIMEOUT := 120
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/net80211-frame-test
SUITE_RUN := sh tools/mock/net80211-frame-test/check.sh
SUITE_TRUTH_SOURCE := 802.11 frame formats and element encoding (the suite's hand-derived vector tables), the RFC 3610 vectors and MIC rejection the library's own test carries, and the component's declared contracts
SUITE_SEGMENTS := S1.BYTES_TX S1.BYTES_RX S2.LINK_ATTACH S2.SCAN_START S2.TX_PROBE_REQ S3.ENCAP S3.DECAP S3.DEFRAG S3.CCMP S3.WATCHDOG S3.NODE_TABLE S4.EAPOL_CLASSIFY M.PAIR_SKB M.PAIR_MBUF M.NAMED_ZERO M.NAMED_MISUSE
SUITE_WIRELESS := 1
SUITE_ROUTE := ieee80211-route: IEEE 802.11-2020 frame formats (management frame layout and element encoding, the open-system authentication sequence), per-anchor clauses registered by the suite

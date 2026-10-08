# Gate1 suite: the extracted driver's static build gates.
#
# The suite is the execution vehicle for the mainline driver's build-side checks: the
# RTL8188EU translation units compile clean for aarch64 against the first-party
# mac80211/cfg80211 header face, the face compiles standalone at a stricter warning
# level, the license split is scanned mechanically, and the kernel-isolation gates pass
# with their canaries. The heavy lifting lives in tests/host/rtl8xxxu-port-build's
# Makefile (one build language, one place to read it).

SUITE_NAME := rtl8xxxu-port-build
SUITE_TIMEOUT := 900
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/rtl8xxxu-port-build compile
SUITE_RUN := sh tools/mock/rtl8xxxu-port-build/check.sh
SUITE_TRUTH_SOURCE := the driver's own bytes as the interface compiles them (the two RTL8188EU translation units against the first-party face), the interface's layout and constant definitions, and the two-source license ledger (the extracted files' SPDX against the registry)
SUITE_SEGMENTS := -
SUITE_WIRELESS := 1
SUITE_ROUTE := ieee80211-route: IEEE 802.11-2020 clause 9.3 (MAC header layout: frame control fields, address fields, sequence control and the QoS control field that follows them) and clause 9.4.2 element 45 (HT capabilities bit values), per the skill's protocol-basics reference sections 3.1-3.3

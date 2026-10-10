# Gate1 suite: the vendored rtw88 driver's static build gates.
#
# The suite is the execution vehicle for the mainline driver's build-side checks: the
# 8821CU translation units compile for aarch64 against the first-party mac80211/cfg80211
# header face, the face compiles standalone at a stricter warning level, the license
# split is scanned mechanically, and the kernel-isolation gates pass with their canaries.
# The heavy lifting lives in tests/host/rtw88-port-build's Makefile (one build language,
# one place to read it).

SUITE_NAME := rtw88-port-build
SUITE_TIMEOUT := 900
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/rtw88-port-build compile
SUITE_RUN := sh tools/mock/rtw88-port-build/check.sh
SUITE_TRUTH_SOURCE := the driver's own bytes as its own build compiles them (the twenty 8821CU translation units against the first-party face, at the kernel's default warning level for a driver directory), the interface's layout and constant definitions as transcribed into the face, and the two-source license ledger (the extracted files' dual SPDX against the registry)
SUITE_SEGMENTS := -
SUITE_WIRELESS := 1
SUITE_ROUTE := ieee80211-route: IEEE 802.11-2020 clause 9.3 (MAC header layout) and clause 9.4.2.14/9.4.2.15 (the VHT capability and operation elements) and clause 10.3.9 (the HT/VHT MCS sets) for the descriptor records the 8821CU transmit path builds, and the radiotap field definitions it fills (the interface's include/net/ieee80211_radiotap.h)

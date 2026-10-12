# Gate1 suite: the vendored rtl8189fs driver's static build gates.
#
# The suite is the execution vehicle for the SDIO vendor driver's build-side checks: the
# 131 RTL8188F translation units compile for aarch64 against the first-party
# cfg80211/netdev/WEXT/osdep header face and the linuxkpi SDIO engine, the face compiles
# standalone at a stricter warning level, the driver's undefined-symbol set matches its
# recorded ledger, the license split is scanned mechanically (the vendored tree is
# GPL-2.0-only with no SPDX line, so the scan is the see-bottom scan), and the
# kernel-isolation gates pass with their canaries. The heavy lifting lives in
# tests/host/rtl8189fs-port-build's Makefile (one build language, one place to read it).

SUITE_NAME := rtl8189fs-port-build
SUITE_TIMEOUT := 1800
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/rtl8189fs-port-build compile
SUITE_RUN := sh tools/mock/rtl8189fs-port-build/check.sh
SUITE_TRUTH_SOURCE := the driver's own bytes as its own build compiles them (the 131 RTL8188F translation units of the lab build's module link table, against the first-party face, at the kernel's default warning level for a driver directory, with the vendored bytes' own diagnostics recorded by file and line), the interface's layout and constant definitions as transcribed into the face, and the two-source license ledger (the vendored files' Realtek GPL-2.0 see-bottom block against the registry)
SUITE_SEGMENTS := -
SUITE_WIRELESS := 1
SUITE_ROUTE := ieee80211-route: IEEE 802.11-2020 clause 9.3 (MAC header layout) and clause 9.4.2.14/9.4.2.15 (the HT capability and operation elements) and clause 10.3.9 (the HT MCS set) for the descriptor records the RTL8188F transmit path builds, the radiotap field definitions it fills (the interface's include/net/ieee80211_radiotap.h), and the wireless-extension request records its configuration path casts (the interface's include/uapi/linux/wireless.h)

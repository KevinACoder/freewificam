# Gate1 suite: the iwlwifi port line's static build gates.
#
# The suite is the execution vehicle for the AC 7260 driver line's build-side
# checks: the 63 translation units of the 7260 (mvm) path compile clean for aarch64
# against the first-party linuxkpi PCI/DMA/interrupt and mac80211 face; the face
# compiles standalone; the license split is scanned mechanically; and the
# kernel-isolation gates pass with their canaries. The heavy lifting lives in
# tests/host/iwlwifi-port-build's Makefile (one build language, one place to read
# it).

SUITE_NAME := iwlwifi-port-build
SUITE_TIMEOUT := 900
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/iwlwifi-port-build compile
SUITE_RUN := sh tools/mock/iwlwifi-port-build/check.sh
SUITE_TRUTH_SOURCE := the vendored driver's bytes as the port compiles them, the driver's own Makefiles as the translation-unit authority, and the first-party linuxkpi PCI/DMA/interrupt and mac80211 header face they bridge to
SUITE_SEGMENTS := -
SUITE_WIRELESS := 0
SUITE_ROUTE := n/a

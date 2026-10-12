# Gate1 suite: the PCIe backend line's static build gates.
#
# The suite is the execution vehicle for the PCIe backend line's build-side
# checks: the GICv3 ITS + message-interrupt domain, the DesignWare root-complex
# core, MSI-X table programming and the NVMe driver, together with the board port
# TUs they bind to, compile clean for aarch64; the line's headers and the port
# headers compile standalone; the license split is scanned mechanically; and the
# kernel-isolation gates pass with their canaries. The heavy lifting lives in
# tests/host/pcie-backend-build's Makefile (one build language, one place to read
# it).

SUITE_NAME := pcie-backend-build
SUITE_TIMEOUT := 900
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/pcie-backend-build compile
SUITE_RUN := sh tools/mock/pcie-backend-build/check.sh
SUITE_TRUTH_SOURCE := the board drivers' bytes as the port compiles them, the new first-party headers and the arch/board port faces they bridge to, and the driver register and table geometry
SUITE_SEGMENTS := -
SUITE_WIRELESS := 0
SUITE_ROUTE := n/a

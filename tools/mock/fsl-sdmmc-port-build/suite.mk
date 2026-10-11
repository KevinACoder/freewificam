# Gate1 suite: the SDIO line's static build gates.
#
# The suite is the execution vehicle for the imported SDIO stack's build-side
# checks: the imported protocol layer, this project's port files and the board
# controller driver compile clean for aarch64, the shadows and the seam compile
# standalone at a stricter warning level, the license split is scanned
# mechanically, and the kernel-isolation gates pass with their canaries. The
# heavy lifting lives in tests/host/fsl-sdmmc-port-build's Makefile (one build
# language, one place to read it).

SUITE_NAME := fsl-sdmmc-port-build
SUITE_TIMEOUT := 900
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/fsl-sdmmc-port-build compile
SUITE_RUN := sh tools/mock/fsl-sdmmc-port-build/check.sh
SUITE_TRUTH_SOURCE := the imported stack's bytes as the port compiles them, the shadow headers' and the seam's own declarations, the board controller's register map, and the two-source license ledger (the imported files' SPDX against the registry)
SUITE_SEGMENTS := -
SUITE_WIRELESS := 0
SUITE_ROUTE := n/a

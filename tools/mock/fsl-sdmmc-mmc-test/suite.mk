# Gate1 suite: the eMMC line over a scripted controller.
#
# The suite binary is built by its own Makefile under tests/host/ - the container
# must not grow a second copy of it. It links the imported MMC card layer and
# this project's port over it against a controller stand-in that records every
# call and models just enough of a card, and reports each case as an anchor line
# (`mmc-test: <id> ok=<0|1> ...`), so the ladder's shape, the width and clock
# programming and the block transfers are asserted call by call rather than
# inferred from a card's behaviour on a board.

SUITE_NAME := fsl-sdmmc-mmc-test
SUITE_TIMEOUT := 300
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/fsl-sdmmc-mmc-test
SUITE_RUN := sh tools/mock/fsl-sdmmc-mmc-test/check.sh
SUITE_TRUTH_SOURCE := the imported MMC card layer's own initialization order (components/fsl_sdmmc/mmc/fsl_mmc.c), the SD/MMC specification's command semantics as the card layer encodes them (components/fsl_sdmmc/common/fsl_sdmmc_spec.h), the port's capability and clock declarations (ports/fsl_sdmmc/sdmmc_host_dwcmshc.c, board/rk3568-itx/drivers/dwc_mshc.c) and the SDMA boundary arithmetic (board/rk3568-itx/drivers/dwc_mshc_dma.h)
SUITE_SEGMENTS := -
SUITE_WIRELESS := 0
SUITE_ROUTE := n/a

# Gate1 suite: the SDIO adapter against a scripted stack.
#
# The suite binary is built by its own Makefile under tests/host/ - the container
# must not grow a second copy of it. It links the adapter and the seam's own
# dispatchers against a stub that records every call into the imported stack, and
# reports each case as an anchor line (`adapter-test: <id> ok=<0|1> ...`), so the
# mapping and the host's length discipline are asserted call by call rather than
# inferred from a card's behaviour.

SUITE_NAME := fsl-sdmmc-adapter-test
SUITE_TIMEOUT := 300
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/fsl-sdmmc-adapter-test
SUITE_RUN := sh tools/mock/fsl-sdmmc-adapter-test/check.sh
SUITE_TRUTH_SOURCE := the seam's declared operation set and error family (components/linuxkpi/sdio/lkpi_sdio_backend.h), the imported stack's extended-command field widths (components/fsl_sdmmc/common/fsl_sdmmc_spec.h), and the host's word-length requirement as the adapter states it
SUITE_SEGMENTS := -
SUITE_WIRELESS := 0
SUITE_ROUTE := n/a

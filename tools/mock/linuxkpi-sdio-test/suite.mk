# Gate1 suite: the SDIO engine against a synthetic card.
#
# The suite binary is built by its own Makefile under tests/host/ - the container
# must not grow a second copy of it. It links the engine against a scripted card
# behind the backend seam, and reports each case as an anchor line
# (`sdio-test: <id> ok=<0|1> ...`), so the interface's semantics are asserted
# call by call rather than inferred from a card's behaviour.

SUITE_NAME := linuxkpi-sdio-test
SUITE_TIMEOUT := 300
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/linuxkpi-sdio-test
SUITE_RUN := sh tools/mock/linuxkpi-sdio-test/check.sh
SUITE_TRUTH_SOURCE := the SDIO specification's register map and command numbers as the face declares them (components/linuxkpi/include/linux/mmc/sdio.h), the interface's I/O semantics (the SDIO core's claim, function-enable, block-size and transfer-splitting rules), and the seam's declared operation set and error family (components/linuxkpi/sdio/lkpi_sdio_backend.h)
SUITE_SEGMENTS := -
SUITE_WIRELESS := 0
SUITE_ROUTE := n/a

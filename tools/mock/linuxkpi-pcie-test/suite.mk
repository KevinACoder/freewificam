# Suite declaration: the linuxkpi PCI engine over its backend seam.
#
# The suite binary is built by its own Makefile under tests/host/ - the container
# must not grow a second copy of it. It carries a synthetic PCI host and a scripted
# device model (config space, a BAR window, a scripted doorbell, DMA recording)
# behind the repository's linuxkpi PCI face, and it reports every step as an anchor
# line (`pcie-smoke: <ID> ...`).

SUITE_NAME := linuxkpi-pcie-test
SUITE_TIMEOUT := 300
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/linuxkpi-pcie-test
SUITE_RUN := sh tools/mock/linuxkpi-pcie-test/check.sh
SUITE_TRUTH_SOURCE := the PCI specification's config-space header, capability-list and BAR layouts, the MSI / MSI-X vector semantics, and the in-repo linuxkpi PCI face (components/linuxkpi/pci)
SUITE_SEGMENTS := -
SUITE_WIRELESS := 0
SUITE_ROUTE := n/a

# Suite declaration: the PCI adapter and the message-interrupt domain.
#
# The suite binary is built by its own Makefile under tests/host/ - the container
# must not grow a second copy of it. It drives the board adapter
# (ports/pcie/lkpi_pcie_port.c) end to end through the engine against a scripted
# root complex and MSI programming, and runs the real message-interrupt domain
# (arch/aarch64/gicv3_msi.c) against scripted ITS primitives. Every step is
# reported as an anchor line (`pcie-adapter: <ID> ...`).

SUITE_NAME := pcie-adapter-test
SUITE_TIMEOUT := 300
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/pcie-adapter-test
SUITE_RUN := sh tools/mock/pcie-adapter-test/check.sh
SUITE_TRUTH_SOURCE := the backend seam's mapping contract (components/linuxkpi/pci/lkpi_pcie_backend.h), the PCIe Requester-ID / ITS DeviceID identity and the event-id payload rule (arch/aarch64/gicv3_msi.c), and the RK3568 DMA coherence model (non-cacheable coherent window, direction maintenance for streaming buffers)
SUITE_SEGMENTS := -
SUITE_WIRELESS := 0
SUITE_ROUTE := n/a

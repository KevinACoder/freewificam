# Gate1 suite: the storage stack against a scripted NVMe controller.
#
# The suite binary is built by its own Makefile under tests/host/ - the container
# must not grow a second copy of it. It links the real ports/fatfs adapter and the
# real vendored FatFs against a scripted NVMe controller and a minimal host
# CMSIS-RTOS2 backend, and reports each case as an anchor line
# (`storage-test: <id> ok=<0|1> ...`), so the exFAT round trip, the 4096-byte
# sector mapping, the fault-injection bounds and the completion conservation are
# asserted call by call rather than inferred from a controller's behaviour.

SUITE_NAME := pcie-storage-test
SUITE_TIMEOUT := 300
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/pcie-storage-test
SUITE_RUN := sh tools/mock/pcie-storage-test/check.sh
SUITE_TRUTH_SOURCE := the exFAT format and the fixed 4096-byte filesystem sector (ports/fatfs/ffconf.h), the NVMe completion and interrupt contract (board/rk3568-itx/drivers/nvme/nvme.h), and the block-device seam's units as diskio.c maps a FatFs sector onto device blocks (ports/fatfs/diskio.c, ports/fatfs/blkdev.h)
SUITE_SEGMENTS := -
SUITE_WIRELESS := 0
SUITE_ROUTE := n/a

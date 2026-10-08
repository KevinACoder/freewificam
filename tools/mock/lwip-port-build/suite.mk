# Gate1 suite: the lwIP port build gates.
#
# This suite is the execution vehicle for the lwIP port's *static* gates (the
# component deliberately has no host runtime suite): the stack's compile set
# and the port layer build clean for both CMSIS-RTOS2 backends, the sys_arch
# function face is present and its undefined symbols stay inside the CMSIS
# service prefix, and the kernel-isolation gates pass with their canaries.
# The heavy lifting lives in tests/host/lwip-port-build's Makefile.

SUITE_NAME := lwip-port-build
SUITE_TIMEOUT := 900
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/lwip-port-build compile-threadx
SUITE_RUN := sh tools/mock/lwip-port-build/check.sh
SUITE_TRUTH_SOURCE := the vendored lwIP 2.2.1 source tree's own contract (lwip/sys.h function face, the Filelists.mk compile groups) compiled against the CMSIS-RTOS2 API header; the canary proofs are the isolation gate script's own
SUITE_SEGMENTS := -

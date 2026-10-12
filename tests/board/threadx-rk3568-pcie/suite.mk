# SPDX-License-Identifier: BSD-3-Clause
# Declaration of the ThreadX board PCIe/NVMe suite. Read by the host runner
# (tools/gates) and by the repository build aggregate (make board-images);
# KEY := value, the same make-assignment syntax the host-side suites use.
#
# One image, one bus: this suite carries the PCIe + NVMe line - the DesignWare
# root complex, the GICv3 ITS message-interrupt domain and the NVMe block
# driver - and no wireless driver.  That is the point of the line: a storage
# endpoint over PCIe with message interrupts, so a wireless result can be read
# against a bus whose completions arrive as MSIs through the ITS rather than as
# URB or SDIO callbacks.
SUITE_NAME            := threadx-rk3568-pcie
SUITE_TRUTH_SOURCE    := the suite's own BOARD-SUITE/CASE channel plus the fixed anchor lines of the PCIe/NVMe bring-up chain (the root complex's link-up and downstream-function claims, the NVMe controller's identity and namespace geometry, the message-interrupt delivery line), cross-checked against the PCIe/NVMe register and queue contracts and the board evidence recorded for this endpoint
SUITE_IMAGE           := tests/board/threadx-rk3568-pcie/build/threadx-rk3568-pcie.bin
SUITE_BUILD           := make -C tests/board/threadx-rk3568-pcie
SUITE_PROFILE         := rtos
SUITE_DEPLOY          := rtos.bin
SUITE_SUCCESS_PATTERN := BOARD-SUITE threadx-rk3568-pcie start
SUITE_SELECT_PATTERN  := BOARD-SUITE threadx-rk3568-pcie select
SUITE_END_PATTERN     := BOARD-SUITE threadx-rk3568-pcie result=
SUITE_SUMMARY_PATTERN := BOARD-SUITE threadx-rk3568-pcie result=pass cases=([0-9]+) ok=([0-9]+) exit=([0-9]+)
SUITE_CASE_NAMES      := shell-up pcie-nvme
SUITE_STEPS_FILE      := steps.tsv
SUITE_JUDGE           := python3 tests/board/threadx-rk3568-pcie/judge.py
SUITE_TIMEOUT         := 240
SUITE_SEGMENTS        := -
SUITE_WIRELESS        := 0

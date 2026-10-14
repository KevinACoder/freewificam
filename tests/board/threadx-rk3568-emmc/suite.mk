# SPDX-License-Identifier: BSD-3-Clause
# Declaration of the ThreadX board eMMC suite. Read by the host runner
# (tools/gates) and by the repository build aggregate (make board-images);
# KEY := value, the same make-assignment syntax the host-side suites use.
#
# One image, one storage line: this suite carries the soldered eMMC - the SD/MMC
# stack's MMC card protocol layer, this project's port and host glue, the board's
# eMMC controller driver, the block-device seam and FatFs - and no wireless
# driver and no other bus. That is the point of the line: the board's second
# storage path, on its own controller, so a storage result can be read against
# the PCIe/NVMe one and a wireless result against both.
SUITE_NAME            := threadx-rk3568-emmc
SUITE_TRUTH_SOURCE    := the suite's own BOARD-SUITE/CASE channel plus the fixed anchor lines of the eMMC bring-up and data path (the card's identity and the bus mode it came up in, the controller's clock chain, the block round trip's checksum, the FatFs volume's format/mount/verify lines and the bounded-window throughput), cross-checked against the card's own CSD/EXT_CSD readback and the board evidence recorded for this card
SUITE_IMAGE           := tests/board/threadx-rk3568-emmc/build/threadx-rk3568-emmc.bin
SUITE_BUILD           := make -C tests/board/threadx-rk3568-emmc
SUITE_PROFILE         := rtos
SUITE_DEPLOY          := rtos.bin
SUITE_SUCCESS_PATTERN := BOARD-SUITE threadx-rk3568-emmc start
SUITE_SELECT_PATTERN  := BOARD-SUITE threadx-rk3568-emmc select
SUITE_END_PATTERN     := BOARD-SUITE threadx-rk3568-emmc result=
SUITE_SUMMARY_PATTERN := BOARD-SUITE threadx-rk3568-emmc result=pass cases=([0-9]+) ok=([0-9]+) exit=([0-9]+)
SUITE_CASE_NAMES      := shell-up emmc-probe emmc-block emmc-fs emmc-fs-verify emmc-bw
SUITE_STEPS_FILE      := steps.tsv
SUITE_JUDGE           := python3 tests/board/threadx-rk3568-emmc/judge.py
SUITE_TIMEOUT         := 1800
SUITE_SEGMENTS        := -
SUITE_WIRELESS        := 0

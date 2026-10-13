# SPDX-License-Identifier: BSD-3-Clause
# Declaration of the ThreadX board iwlwifi (Intel AC 7260) suite. Read by the
# host runner (tools/gates) and by the repository build aggregate (make
# board-images); KEY := value, the same make-assignment syntax the host-side
# suites use.
#
# One image, one driver, one bus: this suite carries the vendored iwlwifi driver
# (Intel Dual Band Wireless AC 7260, the mvm generation) over the PCIe line - the
# DesignWare root complex, the GICv3 ITS message-interrupt domain and the
# linuxkpi PCI engine over them - and no other driver.  It exists on its own
# because the driver is a SoftMAC mac80211 stack that reaches the net80211
# bridge through the mac80211 substitute, and because it rides PCIe with message
# interrupts rather than a USB host or an SDIO controller: a failure here must
# leave every other image's board rounds untouched.
SUITE_NAME            := threadx-rk3568-iwlwifi
SUITE_TRUTH_SOURCE    := the suite's own BOARD-SUITE/CASE channel plus the fixed anchor lines of the driver's bring-up chain (the PCI engine's enumeration and claim of 8086:08b1, the driver's own chip-identification and firmware-load lines routed through the linuxkpi sink, the mac80211 substitute's hw-registration line, the bridge's attach, the scan table rows) cross-checked against the Linux-lane ground truth for the same card on this board
SUITE_IMAGE           := tests/board/threadx-rk3568-iwlwifi/build/threadx-rk3568-iwlwifi.bin
SUITE_BUILD           := make -C tests/board/threadx-rk3568-iwlwifi
SUITE_PROFILE         := rtos
SUITE_DEPLOY          := rtos.bin
SUITE_SUCCESS_PATTERN := BOARD-SUITE threadx-rk3568-iwlwifi start
SUITE_SELECT_PATTERN  := BOARD-SUITE threadx-rk3568-iwlwifi select
SUITE_END_PATTERN     := BOARD-SUITE threadx-rk3568-iwlwifi result=
SUITE_SUMMARY_PATTERN := BOARD-SUITE threadx-rk3568-iwlwifi result=pass cases=([0-9]+) ok=([0-9]+) exit=([0-9]+)
SUITE_CASE_NAMES      := shell-up iwlwifi-probe iwlwifi-scan
SUITE_STEPS_FILE      := steps.tsv
SUITE_JUDGE           := python3 tests/board/threadx-rk3568-iwlwifi/judge.py
SUITE_TIMEOUT         := 900
SUITE_SEGMENTS        := -
SUITE_WIRELESS        := 0

# Declaration of the ThreadX board SDIO suite. Read by the host runner
# (tools/gates) and by the repository build aggregate (make board-images);
# KEY := value, the same make-assignment syntax the host-side suites use.
#
# One image, one bus: this suite carries the SDIO line - the imported SDIO
# protocol layer over this board's DesignWare MMC controller - and no wireless
# driver.  That is the point of the line: a different bus under the same kernel
# and the same shell, so a wireless result can be read against a bus that has no
# URB lifecycle, no completion callbacks and no resubmission.
SUITE_NAME            := threadx-rk3568-sdio
SUITE_TRUTH_SOURCE    := the suite's own BOARD-SUITE/CASE channel plus the fixed anchor lines of the SDIO bring-up chain (the card identity the CIS reports, the function's block-size register written and read back, two block reads of the card's CIS, the quiet pending-interrupt word), cross-checked against the SDIO Simplified Specification's register and tuple layouts and the board evidence recorded for this card
SUITE_IMAGE           := tests/board/threadx-rk3568-sdio/build/threadx-rk3568-sdio.bin
SUITE_BUILD           := make -C tests/board/threadx-rk3568-sdio
SUITE_PROFILE         := rtos
SUITE_DEPLOY          := rtos.bin
SUITE_SUCCESS_PATTERN := BOARD-SUITE threadx-rk3568-sdio start
SUITE_SELECT_PATTERN  := BOARD-SUITE threadx-rk3568-sdio select
SUITE_END_PATTERN     := BOARD-SUITE threadx-rk3568-sdio result=
SUITE_SUMMARY_PATTERN := BOARD-SUITE threadx-rk3568-sdio result=pass cases=([0-9]+) ok=([0-9]+) exit=([0-9]+)
SUITE_CASE_NAMES      := shell-up sdio-enum sdio-card-enable sdio-engine sdio-datapath sdio-pump
SUITE_STEPS_FILE      := steps.tsv
SUITE_JUDGE           := python3 tests/board/threadx-rk3568-sdio/judge.py
SUITE_TIMEOUT         := 240
SUITE_SEGMENTS        := -
SUITE_WIRELESS        := 0

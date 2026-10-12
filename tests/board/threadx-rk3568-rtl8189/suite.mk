# Declaration of the ThreadX board RTL8189FTV suite. Read by the host runner
# (tools/gates) and by the repository build aggregate (make board-images);
# KEY := value, the same make-assignment syntax the host-side suites use.
#
# One image, one driver: this suite carries the vendored Realtek SDIO vendor
# driver (RTL8189FTV) over the SDIO bus line, and no other driver.  The image
# exists on its own because the driver is a FullMAC vendor stack that reaches
# cfg80211 directly and rides the SDIO controller rather than a USB host - a
# failure here must leave every other image's board rounds untouched.
SUITE_NAME            := threadx-rk3568-rtl8189
SUITE_TRUTH_SOURCE    := the suite's own BOARD-SUITE/CASE channel plus the fixed anchor lines of the driver's bring-up chain (the SDIO identity the card reports, the driver's own probe and firmware-download lines, the hardware address it reads from eFuse, the scan table rows) cross-checked against the Linux-lane ground truth for the same card on this board
SUITE_IMAGE           := tests/board/threadx-rk3568-rtl8189/build/threadx-rk3568-rtl8189.bin
SUITE_BUILD           := make -C tests/board/threadx-rk3568-rtl8189
SUITE_PROFILE         := rtos
SUITE_DEPLOY          := rtos.bin
SUITE_SUCCESS_PATTERN := BOARD-SUITE threadx-rk3568-rtl8189 start
SUITE_SELECT_PATTERN  := BOARD-SUITE threadx-rk3568-rtl8189 select
SUITE_END_PATTERN     := BOARD-SUITE threadx-rk3568-rtl8189 result=
SUITE_SUMMARY_PATTERN := BOARD-SUITE threadx-rk3568-rtl8189 result=pass cases=([0-9]+) ok=([0-9]+) exit=([0-9]+)
SUITE_CASE_NAMES      := shell-up wlan8189-probe wlan8189-scan wlan8189-join wlan8189-dataplane
SUITE_STEPS_FILE      := steps.tsv
# The case that consumes the round's own steps (the credential): the runner holds
# --steps-extra until this line appears, so the connect lands after the scan has
# finished and while the join case is waiting, not at a guessed moment during the
# sweep (see tools/gates/board_runner.py).
SUITE_EXTRA_AFTER_PATTERN := wlan8189: join waiting for the supplicant
SUITE_JUDGE           := python3 tests/board/threadx-rk3568-rtl8189/judge.py
SUITE_TIMEOUT         := 480
SUITE_SEGMENTS        := -
SUITE_WIRELESS        := 0

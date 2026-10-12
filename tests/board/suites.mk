# Board-side suite registry: the suites the Gate2 runner drives and the images
# the build aggregate produces. Same make-assignment syntax as the host-side
# registry (tools/mock/suites.mk) so one reader understands both, and both the
# runner and this repository's Makefile read this file.
#
# Each name is a directory under tests/board/ holding a suite.mk declaration
# (see tests/board/README.md for the field table and the line protocol).
SUITES_BOARD := freertos-rk3568-smoke threadx-rk3568-smoke cherrysh-rk3568-smoke threadx-rk3568-regression freertos-rk3568-regression threadx-rk3568-wlan threadx-rk3568-rtw89 threadx-rk3568-rtw88 threadx-rk3568-sdio threadx-rk3568-rtl8189 threadx-rk3568-pcie

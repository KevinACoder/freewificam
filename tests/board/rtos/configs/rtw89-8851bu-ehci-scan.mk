# B target path, stage 2: bring-up + scan.  rtw89 runs first (USB/firmware/
# attach/start); rtw89_scan then pumps the bridge's dwell/walk against the bench
# AP on ch5.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick rtw89 rtw89_scan
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u
CONFIG_RTW89_USB_BUS := 1

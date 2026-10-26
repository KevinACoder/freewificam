# B target path, stage 3: bring-up + scan + open join.  The join is the
# bridge's own station MLME against the bench AP (open, ch5); no key exchange.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick rtw89 rtw89_scan rtw89_join
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u
CONFIG_RTW89_USB_BUS := 1

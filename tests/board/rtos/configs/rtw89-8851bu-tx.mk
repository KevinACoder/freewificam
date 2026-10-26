# B on the other kernel: same sources, same driver bytes; only the backend
# source set, the backend defines and the link window differ.
CONFIG_BACKEND := threadx
CONFIG_CASES := tick rtw89 rtw89_scan rtw89_join rtw89_dataplane
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_RTW89_USB_BUS := 3

# B (target path): the 8851BU on the panel's USB2 root.  Same image as
# rtw89-8851bu.mk except the bus list; the EHCI >=16 KiB bulk question is the
# one this arm answers.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick rtw89 rtw89_scan rtw89_join rtw89_dataplane
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u
CONFIG_RTW89_USB_BUS := 1

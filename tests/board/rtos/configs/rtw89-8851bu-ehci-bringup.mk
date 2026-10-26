# B target path, stage 1: the 8851BU on the panel's USB2 root (EHCI bus 1),
# bring-up only.  This is the arm that answers the EHCI >=16 KiB bulk question:
# the driver's receive buffers are 20 KiB, above the recorded 16 KiB timeout.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick rtw89
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u
CONFIG_RTW89_USB_BUS := 1

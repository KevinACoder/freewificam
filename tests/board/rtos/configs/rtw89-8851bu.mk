# B: RTL8851BU (0bda:b851) on an xHCI root.  The ladder runs in this order:
# bring-up (USB + firmware + attach + start), scan, open join, raw-L2 data.
# 16 MiB heap: sixteen 20 KiB receive buffers live for the driver's lifetime
# and the firmware download allocates a packet per transfer.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick rtw89 rtw89_scan rtw89_join rtw89_dataplane
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u
CONFIG_RTW89_USB_BUS := 3

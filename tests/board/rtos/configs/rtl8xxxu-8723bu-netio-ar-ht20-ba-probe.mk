# RTL8723BU NETIO on the AR9271 bench AP, HT20 with short GI, downlink RX BA on,
# with the RTOS-03 receive-path counters (P0 observation arm).
#
# Same as rtl8xxxu-8723bu-netio-ar-ht20-ba.mk plus the two observation levers:
#   CONFIG_NETIO_RXPATH     the netio: rxpath / netio: usbeng anchors at the
#                           cell's begin and udp-c2s-end, and the cell-end
#                           bridge snapshot the WLAN backend never took
#   CONFIG_LKPI_USB_OBS     the bulk-IN completion length histogram and the
#                           last error code in the CherryUSB adapter
#
# This image is an instrument, not a throughput baseline: the levers add work
# per completion (a few adds) and the anchors only print.  Numbers from it go
# into the ledger as attribution, never as a baseline or a denominator.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick rtl8xxxu rtl8xxxu_scan rtl8xxxu_join rtl8xxxu_dataplane rtl8xxxu_netio
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u
CONFIG_RTL8XXXU_CHIP := 8723bu
# 1 = both USB2 roots (EHCI); the 8723BU is on the hub behind one of them.
CONFIG_RTL8XXXU_USB_BUS := 1
CONFIG_RTL8XXXU_EXPECT_MAC := 88:e6:28:53:10:9c
CONFIG_RTL8XXXU_AP_SSID := labwifi-ar-ht20s
CONFIG_RTL8XXXU_AP_FREQ := 2432
# the downlink block-ack receive side (same module as the 8188 arm)
CONFIG_WLAN_RX_BA := 1
CONFIG_LEVERS += -DCONFIG_WLAN_RX_BA=1 -DCONFIG_NETIO_CPU_KHZ=1992000
CONFIG_LEVERS += -DCONFIG_NETIO_RXPATH=1 -DCONFIG_LKPI_USB_OBS=1

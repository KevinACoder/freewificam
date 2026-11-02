# RTL8723BU NETIO on the AR9271 bench AP, HT20 with short GI, downlink RX BA on.
#
# The 8723BU (0bda:b720, MAC 88:e6:28:53:10:9c) sits behind the 4-port hub on an
# EHCI root (the 8188EU is on xHCI), so the controller list is 1 = both USB2
# roots.  The chip is the same 1x1 11n part the L3-10 ladder brought up; this
# arm is its netio instrument so the module's numbers are taken with the same
# client, block size and AP as the 8188EUS line.
#
# The profile mirrors rtl8xxxu-8188eu-netio-ar-ht20-ba.mk: same ladder, same
# netio server, same RX-BA lever, only the chip identity, the bus roots and the
# expected MAC change.  A boot that attaches another chip's address is an arm
# error, not a slow cell.
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

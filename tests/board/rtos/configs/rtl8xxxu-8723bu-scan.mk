# L3-10 stage 2: bring-up + scan.  The 8723B has no hardware scan op either:
# the net80211 bridge walks the 2.4 GHz channels through the driver's
# .sw_scan_start, so the case pumps the dwell entry and the stack's 1 Hz tick.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick rtl8xxxu rtl8xxxu_scan
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u
CONFIG_RTL8XXXU_CHIP := 8723bu
CONFIG_RTL8XXXU_USB_BUS := 1
CONFIG_RTL8XXXU_EXPECT_MAC := 88:e6:28:53:10:9c
CONFIG_RTL8XXXU_AP_SSID := labwifi-mtk24
CONFIG_RTL8XXXU_AP_FREQ := 2432
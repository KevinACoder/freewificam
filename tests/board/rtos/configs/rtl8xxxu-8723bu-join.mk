# L3-10 stage 3: bring-up + scan + open join against the bench AP (MTK,
# labwifi-mtk24, ch5, open).  The join is the bridge's own station MLME; no
# key exchange.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick rtl8xxxu rtl8xxxu_scan rtl8xxxu_join
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u
CONFIG_RTL8XXXU_CHIP := 8723bu
CONFIG_RTL8XXXU_USB_BUS := 1
CONFIG_RTL8XXXU_EXPECT_MAC := 88:e6:28:53:10:9c
CONFIG_RTL8XXXU_AP_SSID := labwifi-mtk24
CONFIG_RTL8XXXU_AP_FREQ := 2432
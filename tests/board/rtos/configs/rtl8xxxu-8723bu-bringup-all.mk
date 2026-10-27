# 8723BU discovery arm: one boot that starts all four controllers (xHCI 3/2 and
# EHCI 1/0) and reports which one claims the dongle.  The Linux reference build
# already says the
# 8723BU is behind the 4-port hub on an EHCI root, but the bus id moves with
# the probe order, so this arm measures instead of trusting the note.
# Not a delivery profile.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick rtl8xxxu
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u
CONFIG_RTL8XXXU_CHIP := 8723bu
CONFIG_RTL8XXXU_USB_BUS := 4
CONFIG_RTL8XXXU_EXPECT_MAC := 88:e6:28:53:10:9c
CONFIG_RTL8XXXU_AP_SSID := labwifi-mtk24
CONFIG_RTL8XXXU_AP_FREQ := 2432
# 8188EUS discovery arm: one boot that starts all four controllers (xHCI 3/2 and
# EHCI 1/0) and reports which one claims the dongle.  The answer pins the bus
# in the other profiles; this file is not a delivery profile.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick rtl8xxxu
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u
CONFIG_RTL8XXXU_USB_BUS := 4
CONFIG_RTL8XXXU_EXPECT_MAC := 20:f4:1b:52:8f:01
CONFIG_RTL8XXXU_AP_SSID := labwifi-brmc
CONFIG_RTL8XXXU_AP_FREQ := 2432

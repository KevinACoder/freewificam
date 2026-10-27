# L3-10 stage 1: the RTL8723BU (0bda:b720) on the bench's USB2 roots
# (EHCI bus 1/0), bring-up only.  The dongle is a three-interface part: two
# Bluetooth interfaces (class e0/01/01, no host stack here) and the
# vendor-specific WiFi one the driver's id table claims.
# The one discovery boot uses `-bringup-all` instead and pins whichever bus
# claims it.  Backend: FreeRTOS first (the ladder's reference arm).
CONFIG_BACKEND := freertos
CONFIG_CASES := tick rtl8xxxu
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u
CONFIG_RTL8XXXU_CHIP := 8723bu
CONFIG_RTL8XXXU_USB_BUS := 1
CONFIG_RTL8XXXU_EXPECT_MAC := 88:e6:28:53:10:9c
CONFIG_RTL8XXXU_AP_SSID := labwifi-mtk24
CONFIG_RTL8XXXU_AP_FREQ := 2432
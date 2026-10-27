# 8188EUS ladder, stage 1: the RTL8188EUS (0bda:8179) on a USB2 controller, bring-up
# only.  The bus family is the lab's 480 M path (both xHCI roots); the one
# discovery boot uses `-bringup-all` instead and pins whichever bus claims the
# dongle.  Backend: FreeRTOS first (the ladder's reference arm).
CONFIG_BACKEND := freertos
CONFIG_CASES := tick rtl8xxxu
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u
CONFIG_RTL8XXXU_USB_BUS := 3
CONFIG_RTL8XXXU_EXPECT_MAC := 20:f4:1b:52:8f:01
CONFIG_RTL8XXXU_AP_SSID := labwifi-brmc
CONFIG_RTL8XXXU_AP_FREQ := 2432

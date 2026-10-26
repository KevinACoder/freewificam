# L3-08 diagnostic arm: the bring-up plus the high-priority heartbeat that
# reports through the polled UART.  Used to tell a dead kernel/console from a
# blocked runner after the ThreadX claim hang; not a delivery profile.
CONFIG_BACKEND := threadx
CONFIG_CASES := tick rtl8xxxu
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u
CONFIG_RTL8XXXU_USB_BUS := 3
CONFIG_RTL8XXXU_AP_SSID := labwifi-brmc
CONFIG_RTL8XXXU_AP_FREQ := 2432
CONFIG_RTL8XXXU_HB := 1

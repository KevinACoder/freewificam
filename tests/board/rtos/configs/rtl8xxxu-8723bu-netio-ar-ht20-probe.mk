# RTL8723BU NETIO on the AR9271 bench AP, HT20 with short GI, no RX block-ack,
# with the RTOS-03 receive-path counters (positive control for the P0 arm).
#
# Same as rtl8xxxu-8723bu-netio-ar-ht20.mk (the arm that measures 10.0-10.5
# Mbit/s with no aggregation) plus the observation levers documented in
# rtl8xxxu-8723bu-netio-ar-ht20-ba-probe.mk.  Its job is to show the same
# counters on a receive path that works, so the probe arm's numbers have a
# baseline in the same image family.
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
CONFIG_LEVERS += -DCONFIG_NETIO_CPU_KHZ=1992000
CONFIG_LEVERS += -DCONFIG_NETIO_RXPATH=1 -DCONFIG_LKPI_USB_OBS=1

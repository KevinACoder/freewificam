# L3-08 stage 5: the ladder plus the lwIP netif and the two iperf phases
# (downlink server, uplink client).  The peer is the bench AP host's iperf2.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick rtl8xxxu rtl8xxxu_scan rtl8xxxu_join rtl8xxxu_dataplane rtl8xxxu_net
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u
CONFIG_RTL8XXXU_USB_BUS := 3
CONFIG_RTL8XXXU_EXPECT_MAC := 20:f4:1b:52:8f:01
CONFIG_RTL8XXXU_AP_SSID := labwifi-brmc
CONFIG_RTL8XXXU_AP_FREQ := 2432

# 8188EUS NETIO on the AR9271 bench AP, HT20 with short GI, downlink RX BA on
# (RTOS-02 P1 arm).
#
# The sibling of rtl8xxxu-8188eu-netio-ar-ht20.mk.  The only difference is
# CONFIG_WLAN_RX_BA: the image then links wlan_rx_ba.c, which answers the AP's
# ADDBA Request through the bridge's management window and delivers the
# aggregated MPDUs in sequence.  Without it the imported net80211 drops the
# action frame, the AP retries five times and tears the session down, and the
# downlink stays at one MPDU per TXOP (RTOS-02 P0: 19.9 Mbit/s, BA 0,
# 327 us/frame of gap).  The pre-BA arm stays buildable and untouched: it is
# this round's control.
#
# Everything else - AP, OPP, bus, MAC - is the legacy/HT20 arms' recipe; see
# rtl8xxxu-8188eu-netio-ar-ht20.mk for why the OPP is 1.992 GHz and why the
# controller list is 3.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick rtl8xxxu rtl8xxxu_scan rtl8xxxu_join rtl8xxxu_dataplane rtl8xxxu_netio
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u
CONFIG_RTL8XXXU_USB_BUS := 3
CONFIG_RTL8XXXU_EXPECT_MAC := 20:f4:1b:52:8f:01
CONFIG_RTL8XXXU_AP_SSID := labwifi-ar-ht20s
CONFIG_RTL8XXXU_AP_FREQ := 2432
# the downlink block-ack receive side
CONFIG_WLAN_RX_BA := 1
CONFIG_LEVERS += -DCONFIG_WLAN_RX_BA=1 -DCONFIG_NETIO_CPU_KHZ=1992000

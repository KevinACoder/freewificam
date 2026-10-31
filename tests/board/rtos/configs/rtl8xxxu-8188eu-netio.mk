# 8188EUS ladder, stage 5b: the ladder plus the WLAN NETIO server over lwIP.
# Same bench AP and same presentation layer as rtl8xxxu-8188eu-net; the peer is
# the bench host's pinned NETIO client instead of iperf2.  The case prints
# `net: netio ready port=18767 aux=18768 buf=16384` and never returns.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick rtl8xxxu rtl8xxxu_scan rtl8xxxu_join rtl8xxxu_dataplane rtl8xxxu_netio
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u
CONFIG_RTL8XXXU_USB_BUS := 3
CONFIG_RTL8XXXU_EXPECT_MAC := 20:f4:1b:52:8f:01
# W1-01 AP gate: the bench AP is the brcmfmac wlan0 hostapd (labwifi-brmc,
# ch5/2432, HT20, open), the AP the 8188EU is documented to see (L3-08,
# 2026-10-26).  The 2026-10-28 PLAT-01 file pinned the MT7921AU (labwifi-mtk24,
# 3 dBm) before that AP was ever tested against this dongle; the MTK card is
# the monitor half of the air-truth bench, not the AP for this arm.  If the
# gate boot shows the brcmf AP invisible, the round stops and writes
# `TRM-GAP: AP visibility` instead of guessing.
CONFIG_RTL8XXXU_AP_SSID := labwifi-brmc
CONFIG_RTL8XXXU_AP_FREQ := 2432

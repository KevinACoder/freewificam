# 8188EUS ladder, stage 5b: the ladder plus the WLAN NETIO server over lwIP.
# Same bench AP and same presentation layer as rtl8xxxu-8188eu-net; the peer is
# the bench host's pinned NETIO client instead of iperf2.  The case prints
# `net: netio ready port=18767 aux=18768 buf=16384` and never returns.
#
# HISTORY (2026-11-02): this file pins `labwifi-brmc`, the brcmfmac AP retired
# from the AP role on 2026-11-01; that SSID is no longer on the bench.  Keep it
# as the W1-01 arm's reproduction recipe only.  The current APs are the AR9271
# ones and the measurement arms are the two named files:
#   rtl8xxxu-8188eu-netio-ar-g.mk     legacy 11g (labwifi-ar-g)
#   rtl8xxxu-8188eu-netio-ar-ht20.mk  HT20-SGI  (labwifi-ar-ht20s)
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
# The CPU OPP is part of this arm's ruler (K2-12/K2-13 compare across stacks at
# one actual clock), and the netio case no longer pins it by default: K2-14
# measured that requesting 1.992 GHz here collapses the *wired* uplink of the
# same tree from 956 to 62-125 Mbit/s, so a measurement arm that does not ask
# for a clock is measuring whatever the boot happened to leave behind.  The
# 8188EU uplink does not traverse the GMAC, but the number is only comparable
# to the earlier WLAN rounds at the same OPP.
CONFIG_LEVERS += -DCONFIG_NETIO_CPU_KHZ=1992000

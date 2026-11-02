# RTL8723BU NETIO on the AR9271 bench AP, HT20 with short GI, no RX block-ack.
#
# The sibling of rtl8xxxu-8723bu-netio-ar-ht20-ba.mk with the BA lever OFF.
# Why this is the arm that measures: the 8723BU's receive path cannot consume
# the A-MPDUs that a block-ack session unlocks.  With the BA module on, the AP
# aggregates (monitor: 22,342 data frames to the STA, 21,921 A-MPDU-marked,
# 1,097 BAs from the chip) but the host's bridge sees almost none of them
# (+3 data frames in a 20 s cell; the netio client had to be killed).  The
# chip's RXDMA aggregation is disabled by the driver itself
# (rtl8723bu_init_aggregation, "For now simply disable RX aggregation"), so an
# A-MPDU arrives as a transfer the 2328-byte URB path cannot parse.  Enabling
# it is a driver change, which this line does not make; this arm therefore
# measures the aggregation-free configuration, like the 8188 line's pre-BA row.
#
# Chip identity, bus and AP are the same as the BA arm.
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

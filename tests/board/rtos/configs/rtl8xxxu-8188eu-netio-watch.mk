# 8188EUS NETIO at the boot OPP, with the wedge ledger on (observation image).
#
# The A/B question this image answers was closed by the clean arm: the wedge
# reproduces at the boot OPP on the first cell (runs/wireless/2026-11-01-W2-01-
# wedge/A1, 816 MHz readback, form B one second after the S2C window opened), so
# it is not the high-OPP stall the wired line measured in K2-14/K2-15.  What is
# still unknown is *which layer* stops, and that is what this image adds.
#
# Everything below is `rtl8xxxu-8188eu-netio-bootopp.mk` plus the ledger.  The
# three levers are one build: CONFIG_NET_WATCH adds net_watch.c and the bridge /
# serializer observations, NET_WATCH is what the case and the receive seam
# compile their own markers behind, and CONFIG_LKPI_USB_OBS fills the drain
# thread's phase counter.  Off-the-clock behaviour is unchanged: the case still
# leaves the boot OPP alone and still prints the probe and readback.
#
# The ledger prints one short line a second and a long one every fifth, which is
# the console tax this image pays; it exists to place the stall, not to measure
# throughput.  A rate taken here is instrumented-class and must not be compared
# with a clean arm's.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick rtl8xxxu rtl8xxxu_scan rtl8xxxu_join rtl8xxxu_dataplane rtl8xxxu_netio
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u
CONFIG_RTL8XXXU_USB_BUS := 3
CONFIG_RTL8XXXU_EXPECT_MAC := 20:f4:1b:52:8f:01
CONFIG_RTL8XXXU_AP_SSID := labwifi-brmc
CONFIG_RTL8XXXU_AP_FREQ := 2432
# the ledger (net_watch.c plus the per-layer markers)
CONFIG_NET_WATCH := 1
CONFIG_LEVERS += -DCONFIG_NET_WATCH=1 -DNET_WATCH=1 -DCONFIG_LKPI_USB_OBS=1

# 8188EUS NETIO at the boot OPP: the control arm beside the pinned-clock one.
#
# `rtl8xxxu-8188eu-netio.mk` still carries the K2-12/K2-13 lever that requests
# 1.992 GHz (actual 1.689 GHz) so its numbers compare with the earlier WLAN
# rounds.  K2-14 then measured that this exact lever collapses the *wired*
# uplink of the same tree (956 -> 62-125 Mbit/s, first ring-full completion
# never returns) while the boot OPP (816 MHz) delivers 104-105 MiB/s at zero
# loss.  The 8188EUS wedge evidence (a7e9daa8 / b3ea6449 / 89261288, 2026-11-01)
# was all taken with the pinned lever on, so "wireless stack defect" and "the
# high-OPP stall seen on the wire" are confounded in it.
#
# This file removes the lever and nothing else: the case leaves whatever OPP
# the bootloader chose (`CONFIG_NETIO_CPU_KHZ` defaults to 0) and still prints
# the probe plus the readback, so the two arms differ in exactly one bit of the
# ruler.  Pair it with the pinned arm as an A/B on the same tree, same client,
# same AP, same 1440 B.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick rtl8xxxu rtl8xxxu_scan rtl8xxxu_join rtl8xxxu_dataplane rtl8xxxu_netio
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u
CONFIG_RTL8XXXU_USB_BUS := 3
CONFIG_RTL8XXXU_EXPECT_MAC := 20:f4:1b:52:8f:01
# The W1-01 AP gate, unchanged from the pinned arm: brcmfmac wlan0 hostapd
# (labwifi-brmc, ch5/2432, HT20, open).  A gate boot that cannot see the AP
# stops the round with `TRM-GAP: AP visibility` instead of guessing.
CONFIG_RTL8XXXU_AP_SSID := labwifi-brmc
CONFIG_RTL8XXXU_AP_FREQ := 2432

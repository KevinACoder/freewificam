# One-shot helper for tools/build/convert-profiles.py.
#
# The legacy profile was a make fragment that could include another profile
# and append with "+=".  This file includes exactly that fragment, without
# the build Makefile's defaults, and prints the final make values the
# converter turns into a defconfig.  It is not used by the build.
#
#   make -s --no-print-directory -f tools/build/dump-profile.mk \
#        PROFILE=rtl8xxxu-8723bu-netio-ar-ht20-ba
#
# Output lines are "__KEY <name>=<value>"; the converter splits on the first
# "=".  An empty value is a real answer: the old Makefile pre-defined several
# WLAN_* variables to empty before including the fragment, so an unset key
# must stay unset rather than take the fragment's "?=" default.

LINE := $(abspath tests/board/rtos)
PROFILE ?=

include $(LINE)/configs/$(PROFILE).mk

KEYS := \
	BACKEND BOARD CASES FREERTOS_HEAP_SIZE GMAC_PROF HEAP_SIZE LEVERS \
	NET_WATCH OPT PROFILE RTL8XXXU_AP_FREQ RTL8XXXU_AP_SSID \
	RTL8XXXU_CHIP RTL8XXXU_EXPECT_MAC RTL8XXXU_HB RTL8XXXU_USB_BUS \
	RTW89_USB_BUS USB_MSC_DIAG USB_MSC_DIAG_FULL UVC_USB_BUS \
	WLAN_OBS WLAN_PING_COUNT WLAN_PING_INTERVAL_MS WLAN_PING_TRACE \
	WLAN_PUMP_DRAIN WLAN_PUMP_PRIO WLAN_RX_BA WLAN_SCAN_BATCH \
	WLAN_SCAN_DIAG WLAN_TCP_SINK WLAN_TCP_SINK_SECONDS WLAN_UDP_PORT \
	WLAN_UDP_SINK WLAN_UDP_SINK_SECONDS

$(foreach k,$(KEYS),$(info __KEY $(k)=$(CONFIG_$(k))))

.PHONY: dump
dump:
	@:

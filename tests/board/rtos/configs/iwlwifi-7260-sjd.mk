# B4-B6 profile: the 7260 ladder's scan, open join and raw-L2 dataplane on the
# net80211 bridge.  The bring-up case stays first (it owns the linuxkpi PCI
# engine, the firmware download and the attach); the three ladder cases only
# read the state it leaves behind.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick pcie iwl7260 iwl7260_scan iwl7260_join iwl7260_dataplane
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u
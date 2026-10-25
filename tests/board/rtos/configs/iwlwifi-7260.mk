# B3 profile: the PCIe bus gate plus the iwlwifi/mvm attach case.  The pcie
# case group is selected for the arch ITS/MSI bridge and the board root complex;
# the wireless world is added by build/iwlwifi.mk.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick pcie iwl7260
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u

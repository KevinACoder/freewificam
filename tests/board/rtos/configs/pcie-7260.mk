# B2 profile: the bus gate plus the AC 7260 endpoint probe (enumeration, BAR,
# CSR_HW_REV, one registered message vector).  No wireless driver is carried;
# that is B3.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick pcie pcie_7260
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 8M

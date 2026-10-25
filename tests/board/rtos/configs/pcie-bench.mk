# B1 profile: the bus gate plus the throughput matrix (4K/64K/1M x QD1/QD8,
# read/write, 3 reps).  The raw namespace is used; no filesystem is carried.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick pcie pcie_bench
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 8M

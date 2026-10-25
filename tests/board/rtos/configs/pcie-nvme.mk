# B0/B1 profile: one image for the PCIe + NVMe ladder.  The bus case (pcie)
# brings the root complex up and proves the ITS path with one real read; the
# benchmark (pcie_bench) is added by the B1 config.  One backend per build.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick pcie
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 8M

# @file    firefly-v1-pcie.mk
# @brief   The firefly v1 board image: ThreadX behind CMSIS-RTOS2 (the
#          line's kernel charter), the tick/cmsis2 smoke pair plus the
#          PCIe/NVMe/wired-NIC cases the round adds.
#
# Deploy: build/rtos-threadx.bin published as firefly-v1-rtos.bin, loaded
# with `tftpboot 0x90000000 firefly-v1-rtos.bin` and entered with
# `go 0x90000000` (after `dcache flush; dcache off; icache off`).

# --- image -----------------------------------------------------------------
CONFIG_BOARD := firefly-v1
CONFIG_BACKEND := threadx
CONFIG_CASES := tick cmsis2 pcie nvme net_e1000e 
CONFIG_OPT := -O2
CONFIG_PROFILE := perf
CONFIG_HEAP_SIZE := 16M

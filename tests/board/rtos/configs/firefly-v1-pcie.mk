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
# Both wired-NIC cases are parked (user ruling 2026-10-26, end of round):
# the client leg fails to complete a TCP handshake while ARP flows - and
# the board-green reference image fails identically on today's bench while
# the same-boot OS pushes hundreds of Mbit/s from the same addresses, so
# the cause is somewhere in the stack layer and is not this platform's
# bring-up. Re-enable by adding the cases back.
CONFIG_CASES := tick cmsis2 pcie nvme
CONFIG_OPT := -O2
# The perf profile belongs to the rk3568 GMAC line (its driver carries the
# hardware checksum offload the profile's CONFIG_GMAC_* levers describe);
# this board's NICs checksum in software.
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M

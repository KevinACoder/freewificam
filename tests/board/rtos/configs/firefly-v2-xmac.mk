# @file    firefly-v2-xmac.mk
# @brief   The firefly v2 board image: ThreadX behind CMSIS-RTOS2, the
#          tick/cmsis2 smoke pair plus the on-SoC xmac wired-NIC case.
#
# Deploy: build/rtos-threadx.bin published as firefly-v2-rtos.bin, loaded with
# `tftpboot 0x90000000 firefly-v2-rtos.bin` and entered with
# `go 0x90000000`.

# --- image -----------------------------------------------------------------
CONFIG_BOARD := firefly-v2
CONFIG_BACKEND := threadx
CONFIG_CASES := tick cmsis2 net_xmac
CONFIG_OPT := -O2
# The perf profile belongs to the rk3568 GMAC line (its driver carries the
# hardware checksum offload the profile's CONFIG_GMAC_* levers describe); this
# board's NIC checksums in software.
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M

# --- TCP window, measured on this port --------------------------------------
# The shared lwipopts.h carries 131072 / RCV_SCALE 4, which the rk3568 wired
# line sized for its own path. On this board that pair leaves the receive leg
# at ~600 Mbit/s while the transmit leg runs at ~970; the same stack reaches
# ~920 both ways with the pair the reference platform runs.
#
# The two must move together and in opposite directions from what "bigger is
# better" suggests: TCP_WND 240000 with RCV_SCALE 2 opens the receive window,
# and TCP_SND_BUF drops to 60000 because the send buffer shares the same heap.
# Scaling both up cost the transmit leg 400 Mbit/s (957 -> 558) on this part.
CONFIG_LEVERS := -DTCP_WND=240000 -DTCP_RCV_SCALE=2 -DTCP_SND_BUF=60000

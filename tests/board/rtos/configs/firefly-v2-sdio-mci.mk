# @file    firefly-v2-sdio-mci.mk
# @brief   The firefly v2 SDIO socket image: the ported Phytium MCI controller
#          driver under the imported fsl_sdmmc stack, proving CMD52 and CMD53
#          on the board's mmc1 (the socket the WiFi part sits in).
#
# What this image is for: the SDIO data path on this board, driven by the same
# controller programme the Linux driver uses - not by a re-derivation of it.
# The case brings the card up, enumerates it with direct commands, and round
# trips extended commands in byte and block mode, then prints the controller's
# registers next to the Linux ground truth's values.
#
# Deploy: build/rtos-threadx.bin published as firefly-v2-sdio-mci.bin, loaded
# with `tftpboot 0x90000000 firefly-v2-sdio-mci.bin` and entered with
# `go 0x90000000`.

# --- image -----------------------------------------------------------------
CONFIG_BOARD := firefly-v2
CONFIG_BACKEND := threadx
CONFIG_CASES := tick cmsis2 sdio_mci
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M

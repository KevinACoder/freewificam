# @file    sdio_mci.mk
# @brief   The SDIO-socket world: the imported fsl_sdmmc stack driven by this
#          board's own MCI controller driver, ported from the Linux
#          driver that is proven on this board.
#
# One card type (SDIO), one controller (mmc1 @ 0x28001000), polled transfers.
# The controller driver is board/firefly/drivers/mci.c - a first-party port of
# linux-upstream/drivers/mmc/host/firefly-mci.c - and the glue that maps the
# imported stack's ops table onto it is
# third-party/fsl_sdmmc/ports/sdmmc_host_firefly_mci.c.
#
# The stack's own units are the same four the 8189FTV line uses (the OSAL, the
# card bring-up, the common layer and the SDIO card type); only the host glue
# and the board driver differ.

ifneq ($(filter sdio_mci,$(CASES)),)

SDIO_MCI_APP_SRCS := $(LINE)/app/cases/sdio_mci.c

SDIO_MCI_PORT_SRCS := \
	$(THIRD_PARTY)/fsl_sdmmc/ports/fsl_sdmmc_osa.c \
	$(THIRD_PARTY)/fsl_sdmmc/ports/sdmmc_card.c \
	$(THIRD_PARTY)/fsl_sdmmc/ports/sdmmc_host_firefly_mci.c

SDIO_MCI_BOARD_SRCS := \
	$(PLATFORM)/board/firefly/drivers/mci.c

SDIO_MCI_VENDOR_SRCS := \
	$(THIRD_PARTY)/fsl_sdmmc/common/fsl_sdmmc_common.c \
	$(THIRD_PARTY)/fsl_sdmmc/sdio/fsl_sdio.c

SDIO_MCI_ALL_SRCS := $(SDIO_MCI_APP_SRCS) $(SDIO_MCI_PORT_SRCS) \
	$(SDIO_MCI_BOARD_SRCS) $(SDIO_MCI_VENDOR_SRCS)

# The card type key is the stack's own (CONFIG_FSL_SDMMC_ENABLE_SDIO); the
# host-type override names the glue this image carries. A trace build adds
# MCI_TRACE=1, which is the per-command anchor family the round reads.
SDIO_MCI_CFLAGS := $(CFLAGS) \
	 \
	 \
	 \
	-I$(THIRD_PARTY)/fsl_sdmmc/sdio \
	-I$(PLATFORM)/board/firefly/drivers

SDIO_MCI_FLAGS_STAMP := $(SDIO_MCI_CFLAGS)
SDIO_MCI_STAMP := $(BUILD)/sdio-mci-flags-$(BACKEND).stamp
SDIO_MCI_LDFLAGS :=

# The stack's units keep the platform's warning set relaxed the same way the
# other imported trees' do (vendored C, not first-party).
SDIO_MCI_VENDOR_CFLAGS := $(filter-out -Wextra,$(SDIO_MCI_CFLAGS))

# The per-file rules and the flags stamp live in the Makefile, next to every
# other world's: COMPILE_C is defined there, after the includes.
endif # sdio_mci

/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    face_contract_mmc.c
 * @brief   The eMMC line's contract translation unit: the imported MMC card
 *          layer's shadow headers, this port's card service and host glue, and
 *          the board controller's interface on their own, with the constants
 *          and the signatures the layers above them are written against pinned.
 *
 * @design contract translation unit, the same shape as the SDIO line's
 *         face_contract.c beside this file and the driver port-build suites'
 *         contract units. It compiles behind the MMC card-type key, so the two
 *         lines' faces are each exercised without the other's configuration.
 *
 * What is asserted here is what a silent change would hurt:
 *  - the stack's vocabulary the port maps onto: the bus-width enumeration the
 *    glue turns into a count of signal lines, the response types it turns into
 *    controller bits, and the block size the whole storage line is addressed in;
 *  - the two commands the port answers without hardware access (the optional
 *    bus-width test), whose opcodes must stay the ones the stack sends;
 *  - the board driver's own vocabulary: the return-code family the glue maps
 *    onto the stack's status codes, and the register map's field positions the
 *    driver's sequences are written against.
 */

#include "fsl_common.h"
#include "fsl_sdmmc_host.h"
#include "fsl_sdmmc.h"

#include "dwc_mshc.h"
#include "dwc_mshc_dma.h"
#include "dwc_mshc_regs.h"
#include "sdmmc_mmc.h"
#include "sdmmc_port.h"

/* --- the imported stack's vocabulary -------------------------------------- */

/* the width enumeration the glue maps to signal lines (1, 4, 8 lines are
 * 0, 1, 2 - the mapping exists because the controller encodes a count) */
_Static_assert(kSDMMC_BusWdith1Bit == 0, "bus width enumeration: 1 bit");
_Static_assert(kSDMMC_BusWdith4Bit == 1, "bus width enumeration: 4 bit");
_Static_assert(kSDMMC_BusWdith8Bit == 2, "bus width enumeration: 8 bit");

/* the response types the glue turns into controller command bits */
_Static_assert(kCARD_ResponseTypeR2 == 3, "the 136-bit response type");
_Static_assert(kCARD_ResponseTypeR3 == 4, "the OCR response has no CRC");
_Static_assert(kCARD_ResponseTypeR4 == 5, "the OCR response has no CRC");

/* the block size the storage line is addressed in, end to end */
_Static_assert(FSL_SDMMC_DEFAULT_BLOCK_SIZE == 512U, "the default block size");
_Static_assert(SDMMC_MMC_SECTOR_SIZE == FSL_SDMMC_DEFAULT_BLOCK_SIZE,
	       "the seam's sector is the card's block");

/* the staging buffer the card service allocates, and the alignment it asks for */
_Static_assert(SDMMC_DATA_BUFFER_ALIGN_CACHE == 4096U,
	       "the alignment the staging buffer is allocated with");

/* the card-type selector the port's configuration declares */
_Static_assert(kSDMMCHOST_TYPE_FDWMSHC == 5, "this controller's host type");
_Static_assert(kSDMMCHOST_CARD_TYPE_EMMC == 3, "the soldered card's type");

/* --- the two commands answered without hardware access --------------------- */

_Static_assert(kMMC_SendingBusTest == 19U, "BUSTEST_W: the test pattern's write");
_Static_assert(kMMC_BusTestRead == 14U, "BUSTEST_R: the test pattern's read-back");
_Static_assert(kMMC_SendTuningBlock == 21U, "the tuning block the next round uses");

/* --- the board driver's vocabulary ---------------------------------------- */

_Static_assert(DWC_MSHC_OK == 0, "the driver's success code");
_Static_assert(DWC_MSHC_ERR_ARG < 0 && DWC_MSHC_ERR_DATA < 0,
	       "the driver's failures are negative");

/* The register map's load-bearing positions: the clock divider's field, the
 * transfer-mode bits, and the command register's response encoding. */
_Static_assert(DWC_MSHC_CLK_FREQ_SEL(3U) == (3U << 8), "the divider field");
_Static_assert(DWC_MSHC_CLK_INTERNAL_EN == (1U << 0), "the internal clock bit");
_Static_assert(DWC_MSHC_CLK_SD_EN == (1U << 2), "the card clock bit");
_Static_assert(DWC_MSHC_XFER_DMA_EN == (1U << 0), "DMA enable");
_Static_assert(DWC_MSHC_XFER_BLK_CNT_EN == (1U << 1), "block count enable");
_Static_assert(DWC_MSHC_XFER_DATA_DIR_READ == (1U << 4), "read direction");
_Static_assert(DWC_MSHC_XFER_MULTI_BLK == (1U << 5), "multi-block transfer");
_Static_assert(DWC_MSHC_CMD_RESP_136 == (1U << 0), "the 136-bit response code");
_Static_assert(DWC_MSHC_CMD_RESP_48 == (1U << 1), "the 48-bit response code");
_Static_assert(DWC_MSHC_CMD_CRC_CHECK == (1U << 3), "the CRC check bit");
_Static_assert(DWC_MSHC_CMD_INDEX_CHECK == (1U << 4), "the index check bit");
_Static_assert(DWC_MSHC_CMD_DATA_PRESENT == (1U << 5), "the data-present bit");
_Static_assert(DWC_MSHC_CMD_INDEX_SHIFT == 8U, "the command index field");

/* The SDMA boundary the engine stops at, and the continuation arithmetic: the
 * first boundary strictly after the address the engine was last given. */
_Static_assert(DWC_MSHC_SDMA_BOUNDARY == (512U * 1024U), "the boundary");
_Static_assert(DWC_MSHC_BLKSZ_SDMA_BOUNDARY_512K == (7U << 12),
	       "the boundary's field in BLOCK_SIZE");
/* The continuation arithmetic itself is a function, so it is exercised at run
 * time by the line's host suite rather than asserted here: a static assertion
 * cannot call it. What this unit pins is the boundary the arithmetic is about. */

/* The CRU coordinates: the mux field the clock table writes, and the slots it
 * selects. The rates those slots carry on this board are the driver's clock
 * table's business; the positions are the contract. */
_Static_assert(DWC_MSHC_CRU_CCLK_MUX_MASK == (7U << 12), "the CCLK mux field");
_Static_assert(DWC_MSHC_CRU_CCLK_SEL_24M == 0U, "the 24 MHz slot");
_Static_assert(DWC_MSHC_CRU_CCLK_SEL_50M == 4U, "the 50 MHz slot");
_Static_assert(DWC_MSHC_CRU_CCLK_SEL_375K == 5U, "the identification slot");

/*
 * The declarations themselves, pinned by signature: taking each one's address
 * through a pointer of the expected type fails here if a prototype drifts.
 */
status_t (*const face_mmc_cfg_initialize)(sdmmc_mmc_t *, sdmmchost_config_t *) =
	MMC_CfgInitialize;
status_t (*const face_mmc_host_init)(sdmmchost_t *) = SDMMCHOST_Init;
status_t (*const face_mmc_host_config)(sdmmc_mmc_t *, sdmmchost_config_t *) =
	FDWMSHCHOST_MMCConfig;
status_t (*const face_mmc_card_init)(mmc_card_t *) = MMC_Init;
status_t (*const face_mmc_read_blocks)(mmc_card_t *, uint8_t *, uint32_t, uint32_t) =
	MMC_ReadBlocks;
status_t (*const face_mmc_write_blocks)(mmc_card_t *, const uint8_t *, uint32_t,
					uint32_t) = MMC_WriteBlocks;

int (*const face_mmc_card_start)(void) = sdmmc_mmc_card_start;
void (*const face_mmc_card_stop)(void) = sdmmc_mmc_card_stop;
int (*const face_mmc_info)(struct sdmmc_mmc_info *) = sdmmc_mmc_info_get;
int (*const face_mmc_read)(uint64_t, void *, uint32_t) = sdmmc_mmc_read;
int (*const face_mmc_write)(uint64_t, const void *, uint32_t) = sdmmc_mmc_write;
int (*const face_mmc_flush)(void) = sdmmc_mmc_flush;
int (*const face_mmc_host_state)(struct sdmmc_mmc_host_state *) =
	sdmmc_mmc_host_state_get;

int (*const face_board_init)(struct dwc_mshc *, const struct dwc_mshc_cfg *) =
	dwc_mshc_init;
uint32_t (*const face_board_set_clock)(struct dwc_mshc *, uint32_t) =
	dwc_mshc_set_clock;
void (*const face_board_set_width)(struct dwc_mshc *, unsigned int) =
	dwc_mshc_set_bus_width;
int (*const face_board_transfer)(struct dwc_mshc *, struct dwc_mshc_cmd *) =
	dwc_mshc_transfer;
bool (*const face_board_busy)(const struct dwc_mshc *) = dwc_mshc_card_busy;
const struct dwc_mshc_cfg *(*const face_board_cfg)(unsigned int) =
	dwc_mshc_board_cfg;

/* One symbol, so the unit produces an object and the faces' linkage is exercised. */
int fsl_sdmmc_mmc_face_contract_present;

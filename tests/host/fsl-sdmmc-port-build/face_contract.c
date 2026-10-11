/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    face_contract.c
 * @brief   One translation unit that includes the imported stack's shadow
 *          headers, the SDIO seam and the board controller's interface on their
 *          own, and asserts the constants the layer above them is written
 *          against.
 *
 * @design contract translation unit, the same shape as the driver port-build
 *         suites' contract unit (tests/host/rtl8xxxu-port-build/face_contract.c):
 *         the faces are the product of this round, so they must stand without
 *         the units that consume them, and the numbers the adapter's mapping
 *         and its length discipline depend on are asserted rather than assumed.
 *
 * The two groups of assertions are the two places a silent change would hurt:
 *
 *  - the imported stack's enumerations and field widths. The adapter maps the
 *    bus-width enumeration to signal lines and chunks a byte count to the
 *    extended command's nine-bit count field; if either value moves, the
 *    mapping stops meaning what it says.
 *  - the seam's error family, which is defined to be the stack's own numbering.
 *    The two sides must agree, and this is where they are checked against each
 *    other rather than against a comment.
 */

/*
 * Include order is deliberate and matches the adapter's: the seam header brings
 * in the linuxkpi face, which defines MIN/MAX/ARRAY_SIZE unconditionally, while
 * the imported stack's shadow of fsl_common.h defines the same three names
 * behind "if not already defined" guards. Seam first keeps one definition of
 * each, and the imported units use none of the three.
 */
#include "lkpi_sdio_backend.h"

#include "fsl_common.h"
#include "fsl_sdmmc_host.h"
#include "fsl_sdmmc.h"

#include "dwc_mmc.h"
#include "dwc_mmc_regs.h"
#include "sdmmc_port.h"

/* --- the imported stack's vocabulary -------------------------------------- */

/* the width enumeration the host glue maps from; the controller's own encoding
 * is a count of signal lines, which is the whole reason the mapping exists */
_Static_assert(kSDMMC_BusWdith1Bit == 0, "bus width enumeration: 1 bit");
_Static_assert(kSDMMC_BusWdith4Bit == 1, "bus width enumeration: 4 bit");
_Static_assert(kSDMMC_BusWdith8Bit == 2, "bus width enumeration: 8 bit");

/* the two commands the seam's direct and extended operations become */
_Static_assert(kSDIO_RWIODirect == 52, "the direct transfer command");
_Static_assert(kSDIO_RWIOExtended == 53, "the extended transfer command");

/* the extended command's argument fields the adapter sets and chunks to */
_Static_assert(SDIO_EXTEND_CMD_COUNT_MASK == 0x1ffU,
	       "the byte/block count field is nine bits wide");
_Static_assert(SDIO_EXTEND_CMD_BLOCK_MODE_MASK == 0x08000000U,
	       "the block-mode bit");
_Static_assert(SDIO_EXTEND_CMD_OP_CODE_MASK == 0x04000000U,
	       "the address-increment bit");
_Static_assert(SDIO_FBR_BASE(1) == 0x100U, "function 1's register window");

/* the staging buffer the card service allocates, and the block size it derives
 * the host's maximum block count from */
_Static_assert(FSL_SDMMC_DEFAULT_BLOCK_SIZE == 512U, "the default block size");
_Static_assert(FSL_SDMMC_CARD_INTERNAL_BUFFER_SIZE == 4608U,
	       "the staging buffer the extended transfers copy through");
_Static_assert(SDMMC_DATA_BUFFER_ALIGN_CACHE == 4096U,
	       "the alignment the staging buffer is allocated with");

/* --- the seam's error family is the stack's numbering --------------------- */

_Static_assert(LKPI_SDIO_BE_ERR_FAIL == kStatus_Fail,
	       "the seam's generic failure is the stack's");
_Static_assert(LKPI_SDIO_BE_ERR_INVAL == kStatus_InvalidArgument,
	       "the seam's invalid argument is the stack's");
_Static_assert(LKPI_SDIO_BE_ERR_TIMEOUT == kStatus_Timeout,
	       "the seam's timeout is the stack's");
_Static_assert(LKPI_SDIO_BE_ERR_XFER == kStatus_SDMMC_TransferFailed,
	       "the seam's transfer failure is the stack's");
_Static_assert(LKPI_SDIO_BE_ERR_BLKSIZE == kStatus_SDMMC_SetCardBlockSizeFailed,
	       "the seam's block-size failure is the stack's");
_Static_assert(LKPI_SDIO_BE_ERR_HOST == kStatus_SDMMC_HostNotSupport,
	       "the seam's unsupported-host code is the stack's");
_Static_assert(LKPI_SDIO_BE_ERR_CARD == kStatus_SDMMC_CardNotSupport,
	       "the seam's unsupported-card code is the stack's");
_Static_assert(LKPI_SDIO_BE_ERR_RESP == kStatus_SDMMC_SDIO_ResponseError,
	       "the seam's response-error code is the stack's");
_Static_assert(LKPI_SDIO_BE_ERR_CIS == kStatus_SDMMC_SDIO_ReadCISFail,
	       "the seam's CIS-failure code is the stack's");
_Static_assert(LKPI_SDIO_BE_ERR_NOTSDIO == kStatus_SDMMC_SDIO_InvalidCard,
	       "the seam's not-an-SDIO-card code is the stack's");

/* --- the seam's own shape -------------------------------------------------- */

_Static_assert(LKPI_SDIO_BE_MAX_FUNCS == 7, "the most I/O functions one card carries");
_Static_assert((LKPI_SDIO_BE_F_WRITE & LKPI_SDIO_BE_F_BLOCK) == 0U,
	       "the extended-transfer flags are distinct bits");
_Static_assert((LKPI_SDIO_BE_F_BLOCK & LKPI_SDIO_BE_F_INCR) == 0U,
	       "the extended-transfer flags are distinct bits");

/* --- the board interface --------------------------------------------------- */

_Static_assert(sizeof(struct dwc_mmc_desc) == 16U,
	       "the IDMAC descriptor the controller reads is four words");

/*
 * The declarations themselves, pinned by signature: taking each one's address
 * through a pointer of the expected type fails here if a prototype drifts,
 * which is the failure mode a header-only face otherwise hides until a caller
 * links against it.
 */
status_t (*const face_sdio_cfg_initialize)(sdmmc_sdio_t *, sdmmchost_config_t *) =
	SDIO_CfgInitialize;
status_t (*const face_sdmmc_host_init)(sdmmchost_t *) = SDMMCHOST_Init;
status_t (*const face_sdio_io_write_extended)(sdio_card_t *, sdio_func_num_t,
					      uint32_t, uint8_t *, uint32_t,
					      uint32_t) = SDIO_IO_Write_Extended;
status_t (*const face_sdio_io_read_extended)(sdio_card_t *, sdio_func_num_t,
					     uint32_t, uint8_t *, uint32_t,
					     uint32_t) = SDIO_IO_Read_Extended;
status_t (*const face_sdio_io_read_direct)(sdio_card_t *, sdio_func_num_t, uint32_t,
					   uint8_t *) = SDIO_IO_Read_Direct;
status_t (*const face_sdio_set_block_size)(sdio_card_t *, sdio_func_num_t,
					   uint32_t) = SDIO_SetBlockSize;
status_t (*const face_sdio_get_pending_interrupt)(sdio_card_t *, uint8_t *) =
	SDIO_GetPendingInterrupt;
status_t (*const face_sdmmc_sdio_config)(sdmmc_sdio_t *, sdmmchost_config_t *) =
	FDWMMCHOST_SDIOConfig;

int (*const face_seam_card_init)(struct lkpi_sdio_be_card_info *) =
	lkpi_sdio_be_card_init;
int (*const face_seam_io_extended)(unsigned int, unsigned int, void *, unsigned int,
				   unsigned int) = lkpi_sdio_be_io_extended;
void (*const face_seam_register)(const struct lkpi_sdio_be_ops *, void *) =
	lkpi_sdio_be_register;

int (*const face_board_init)(struct dwc_mmc *, const struct dwc_mmc_cfg *) =
	dwc_mmc_init;
uint32_t (*const face_board_set_clock)(struct dwc_mmc *, uint32_t) =
	dwc_mmc_set_clock;
int (*const face_board_transfer)(struct dwc_mmc *, struct dwc_mmc_cmd *) =
	dwc_mmc_transfer;
const struct dwc_mmc_cfg *(*const face_board_cfg)(unsigned int) = dwc_mmc_board_cfg;

void (*const face_port_install)(void) = lkpi_sdio_fsl_install;
status_t (*const face_card_start)(void) = sdmmc_card_start;
sdio_card_t *(*const face_card_descriptor)(void) = sdmmc_card;

/* One symbol, so the unit produces an object and the faces' linkage is exercised. */
int fsl_sdmmc_face_contract_present;

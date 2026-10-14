/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    scripted_mshc.c
 * @brief   The scripted eMMC card behind the board driver's interface.
 *
 * The card model is deliberately small but not permissive: it carries an
 * EXT_CSD whose fields the imported stack's decoder reads (the card type's
 * high-speed bits, the sector count, the bus-width and high-speed-timing bytes),
 * it answers the identity commands with packed CID and CSD words, and it moves
 * block data between the caller's buffer and a host-memory user area.
 *
 * One rule it enforces is the reason this suite exists: a data transfer whose
 * width disagrees with the width the card was switched to fails, exactly as it
 * would on a board. That is what makes the initialization ladder's width
 * handling assertable here rather than only on the board.
 *
 * @design test double of the board driver (dwc_mshc.h), the same shape as the
 *         PCIe/NVMe suite's scripted controller
 *         (tests/host/pcie-storage-test/scripted_nvme.c): the driver's interface
 *         is real, the device behind it is scripted, and every call is recorded
 *         so a case asserts the shape of what the stack asked for.
 */

#include <stdio.h>
#include <string.h>

#include "scripted_mshc.h"

/* The user area: the scripted card's blocks. */
static uint8_t scripted_user_area[SCRIPTED_MSHC_BLOCKS * SCRIPTED_MSHC_BLOCK_SIZE];

/* The card's EXT_CSD, 512 bytes, indexed as the specification indexes it. */
static uint8_t scripted_ext_csd[512];

static struct scripted_mshc_call calls[SCRIPTED_MSHC_MAX_CALLS];
static unsigned int call_count;

static unsigned int card_width_lines;	/* what the CARD was switched to */
static unsigned int host_width_lines;	/* what the host programmed */
static uint32_t card_clock_hz;
static uint32_t relative_address;

static int fail_clock;
static uint32_t fail_data_index;
static unsigned int width_override;

/* --- the synthesized identity --------------------------------------------- */

/* The CID, packed into the four words the stack's decoder reads (word 3 is the
 * most significant, and the last byte of word 0 is the CRC the decoder
 * ignores). */
static uint32_t scripted_cid[4];

/* The CSD, packed the same way: version 2 (high capacity), 52 MHz transfer
 * speed, 512-byte read blocks. */
static uint32_t scripted_csd[4];

static void scripted_build_identity(void)
{
	uint8_t cid[16] = { 0 };
	uint8_t csd[16] = { 0 };
	const char *product = SCRIPTED_MSHC_PRODUCT;
	unsigned int i;

	/* --- CID: manufacturer, product name, revision, serial --- */
	cid[0] = 0xF4U;					/* manufacturer ID */
	cid[1] = 0x01U;					/* application ID */
	cid[1] = (uint8_t)(cid[1] << 0);		/* kept simple: 0x0001 */
	cid[2] = 0x01U;
	for (i = 0; i < 6U; i++) {
		cid[3U + i] = (uint8_t)product[i];	/* product name, six bytes */
	}
	cid[9] = 0x10U;					/* product revision */
	cid[10] = (uint8_t)((SCRIPTED_MSHC_SERIAL >> 24) & 0xffU);
	cid[11] = (uint8_t)((SCRIPTED_MSHC_SERIAL >> 16) & 0xffU);
	cid[12] = (uint8_t)((SCRIPTED_MSHC_SERIAL >> 8) & 0xffU);
	cid[13] = (uint8_t)(SCRIPTED_MSHC_SERIAL & 0xffU);
	cid[14] = 0x02U;				/* manufacturing date */
	cid[15] = 0x01U;				/* CRC (not checked by the decoder) */

	scripted_cid[3] = ((uint32_t)cid[0] << 24) | ((uint32_t)cid[1] << 16) |
			  ((uint32_t)cid[2] << 8) | cid[3];
	scripted_cid[2] = ((uint32_t)cid[4] << 24) | ((uint32_t)cid[5] << 16) |
			  ((uint32_t)cid[6] << 8) | cid[7];
	scripted_cid[1] = ((uint32_t)cid[8] << 24) | ((uint32_t)cid[9] << 16) |
			  ((uint32_t)cid[10] << 8) | cid[11];
	scripted_cid[0] = ((uint32_t)cid[12] << 24) | ((uint32_t)cid[13] << 16) |
			  ((uint32_t)cid[14] << 8) | cid[15];

	/* --- CSD: the decoder reads the structure version and the transfer speed
	 * from the top word, and the read block length from the second. --- */
	scripted_csd[3] = (2U << 30) |			/* CSD structure version 2 */
			  (0U << 26) |			/* system specification 1.0 */
			  (0x0aU << 16) |		/* data read access time 1 */
			  (0x00U << 8) |
			  0x32U;			/* transfer speed: 52 MHz */
	scripted_csd[2] = (0x1fU << 20) |		/* command classes */
			  (9U << 16) |			/* read block length: 512 bytes */
			  0U;				/* device size (v2: unused) */
	scripted_csd[1] = 0U;
	scripted_csd[0] = 0U;
	(void)csd;
}

/* --- the EXT_CSD ----------------------------------------------------------- */

static void scripted_build_ext_csd(void)
{
	(void)memset(scripted_ext_csd, 0, sizeof(scripted_ext_csd));

	/* The card type's supported timings: high speed at 26 and 52 MHz, which is
	 * what the stack's flag bits come from. */
	scripted_ext_csd[196] = 0x03U;
	/* The current bus width (1 = four lines, 2 = eight) and timing: the card
	 * starts on one line at the default timing. */
	scripted_ext_csd[183] = 0x00U;
	scripted_ext_csd[185] = 0x00U;
	/* The extended CSD revision and the CSD structure version. */
	scripted_ext_csd[192] = 0x07U;
	scripted_ext_csd[194] = 0x02U;
	/* The IO driver strengths the card supports: bit 0 (the default strength)
	 * has to be there, or the stack refuses the high-speed timing switch on a
	 * revision 1.7 card before it even sends the command. */
	scripted_ext_csd[197] = 0x01U;
	/* The user area's size. The stack reads this field as a count of 512-byte
	 * blocks - which is also how the board's own card reports it (its raw
	 * value equals the block count the board suite prints) - so the scripted
	 * card states its blocks here. */
	scripted_ext_csd[212] = (uint8_t)(SCRIPTED_MSHC_BLOCKS & 0xffU);
	scripted_ext_csd[213] = (uint8_t)((SCRIPTED_MSHC_BLOCKS >> 8) & 0xffU);
	scripted_ext_csd[214] = (uint8_t)((SCRIPTED_MSHC_BLOCKS >> 16) & 0xffU);
	scripted_ext_csd[215] = (uint8_t)((SCRIPTED_MSHC_BLOCKS >> 24) & 0xffU);
	/* A plausible erase-group geometry. */
	scripted_ext_csd[224] = 0x00U;
	scripted_ext_csd[225] = 0x00U;
	scripted_ext_csd[226] = 0x00U;
	scripted_ext_csd[227] = 0x08U;
}

/* --- recording ------------------------------------------------------------- */

static struct scripted_mshc_call *scripted_record(unsigned int op)
{
	struct scripted_mshc_call *call;

	if (call_count >= SCRIPTED_MSHC_MAX_CALLS) {
		return NULL;
	}

	call = &calls[call_count++];
	(void)memset(call, 0, sizeof(*call));
	call->op = op;

	return call;
}

void scripted_mshc_reset(void)
{
	(void)memset(calls, 0, sizeof(calls));
	call_count = 0;
	card_width_lines = 1U;
	host_width_lines = 1U;
	card_clock_hz = 0U;
	relative_address = 0U;
	fail_clock = 0;
	fail_data_index = 0U;
	width_override = 0U;
	(void)memset(scripted_user_area, 0, sizeof(scripted_user_area));
	scripted_build_identity();
	scripted_build_ext_csd();
}

unsigned int scripted_mshc_call_count(void)
{
	return call_count;
}

const struct scripted_mshc_call *scripted_mshc_call(unsigned int index)
{
	return (index < call_count) ? &calls[index] : NULL;
}

unsigned int scripted_mshc_count(unsigned int op, uint32_t index)
{
	unsigned int i;
	unsigned int found = 0U;

	for (i = 0U; i < call_count; i++) {
		if (calls[i].op != op) {
			continue;
		}
		if (index != 0U && calls[i].index != index) {
			continue;
		}
		found++;
	}

	return found;
}

uint8_t *scripted_mshc_user_area(void)
{
	return scripted_user_area;
}

unsigned int scripted_mshc_width(void)
{
	return host_width_lines;
}

uint32_t scripted_mshc_clock_hz(void)
{
	return card_clock_hz;
}

void scripted_mshc_fail_clock(void)
{
	fail_clock = 1;
}

void scripted_mshc_fail_data(uint32_t index)
{
	fail_data_index = index;
}

void scripted_mshc_card_width_override(unsigned int lines)
{
	width_override = lines;
}

/* --- the card's own state ------------------------------------------------- */

/* The width the card believes it is on: its EXT_CSD bus-width byte, or the
 * override a case plants to model a card that refused the switch. */
static unsigned int scripted_card_width(void)
{
	if (width_override != 0U) {
		return width_override;
	}

	switch (scripted_ext_csd[183]) {
	case 2U:
		return 8U;
	case 1U:
		return 4U;
	default:
		return 1U;
	}
}

/* The card's answer to a command: fill the response words and say whether the
 * command was accepted. */
static int scripted_answer(struct dwc_mshc_cmd *cmd)
{
	cmd->response[0] = 0U;
	cmd->response[1] = 0U;
	cmd->response[2] = 0U;
	cmd->response[3] = 0U;

	switch (cmd->index) {
	case 0U:	/* GO_IDLE_STATE */
		relative_address = 0U;
		return DWC_MSHC_OK;
	case 1U:	/* SEND_OP_COND: busy, 3.3 V, high capacity */
		cmd->response[0] = 0xc0ff8080U;
		return DWC_MSHC_OK;
	case 2U:	/* ALL_SEND_CID */
		(void)memcpy(cmd->response, scripted_cid, sizeof(scripted_cid));
		return DWC_MSHC_OK;
	case 3U:	/* SET_RELATIVE_ADDR */
		relative_address = 1U;
		cmd->response[0] = 0x00000900U;		/* ready for data, transfer state */
		return DWC_MSHC_OK;
	case 6U:	/* SWITCH: the timing and the bus width */
		{
			/* The stack's CMD6 argument: the access mode in bits
			 * [26:24], the byte index in [23:16], the value in [15:8]. */
			uint32_t index = (cmd->arg >> 16) & 0xffU;
			uint32_t value = (cmd->arg >> 8) & 0xffU;
			uint32_t access = (cmd->arg >> 24) & 0x7U;

			/* Both access forms the stack uses land here: writing a byte
			 * (access 1) and writing bits (access 3). The scripted card is
			 * obedient - it records whatever the switch asks for, which is
			 * what makes the stack's write-then-read-back verifications
			 * (the timing, the power class) agree with it. The bus width
			 * additionally becomes the width the card's data lines use,
			 * which is what the data path checks against the host's
			 * programming. */
			if ((access != 0U) && (access != 2U) && (index < sizeof(scripted_ext_csd))) {
				scripted_ext_csd[index] = (uint8_t)value;
				card_width_lines = scripted_card_width();
			}
		}
		cmd->response[0] = 0x00000900U;
		return DWC_MSHC_OK;
	case 7U:	/* SELECT_CARD */
	case 13U:	/* SEND_STATUS */
	case 16U:	/* SET_BLOCKLEN */
	case 23U:	/* SET_BLOCK_COUNT */
	case 55U:	/* the SD command the stack sends and expects to time out */
		cmd->response[0] = 0x00000900U;
		return DWC_MSHC_OK;
	case 9U:	/* SEND_CSD */
		(void)memcpy(cmd->response, scripted_csd, sizeof(scripted_csd));
		return DWC_MSHC_OK;
	case 8U:	/* SEND_EXT_CSD: a 512-byte read of the register */
		cmd->response[0] = 0x00000900U;
		return DWC_MSHC_OK;
	case 17U:	/* READ_SINGLE_BLOCK */
	case 18U:	/* READ_MULTIPLE_BLOCK */
	case 24U:	/* WRITE_BLOCK */
	case 25U:	/* WRITE_MULTIPLE_BLOCK */
		cmd->response[0] = 0x00000900U;
		return DWC_MSHC_OK;
	default:
		/* Everything else the ladder sends is answered with a clean status;
		 * the suite asserts the sequence, not a per-command model. */
		cmd->response[0] = 0x00000900U;
		return DWC_MSHC_OK;
	}
}

/* The data stage: the EXT_CSD register, or the user area at the command's
 * address. A width disagreement fails the transfer, as it would on a board. */
static int scripted_data(struct dwc_mshc_cmd *cmd)
{
	const struct dwc_mshc_data *d = cmd->data;
	uint32_t offset = cmd->arg * SCRIPTED_MSHC_BLOCK_SIZE;	/* a block address */
	uint32_t bytes = d->len;

	if ((bytes == 0U) || (d->buf == NULL)) {
		return DWC_MSHC_ERR_ARG;
	}
	if (scripted_card_width() != host_width_lines) {
		return DWC_MSHC_ERR_DATA;
	}
	if ((fail_data_index != 0U) && (cmd->index == fail_data_index)) {
		return DWC_MSHC_ERR_DATA;
	}

	if (cmd->index == 8U) {
		if (d->write) {
			return DWC_MSHC_ERR_ARG;
		}
		(void)memcpy(d->buf, scripted_ext_csd,
			     (bytes < sizeof(scripted_ext_csd)) ? bytes : sizeof(scripted_ext_csd));
		return DWC_MSHC_OK;
	}

	if ((offset + bytes) > sizeof(scripted_user_area)) {
		return DWC_MSHC_ERR_ARG;
	}

	if (d->write) {
		(void)memcpy(&scripted_user_area[offset], d->buf, bytes);
	} else {
		(void)memcpy(d->buf, &scripted_user_area[offset], bytes);
	}

	return DWC_MSHC_OK;
}

/* --- the board driver's interface ----------------------------------------- */

const struct dwc_mshc_cfg *dwc_mshc_board_cfg(unsigned int index)
{
	static const struct dwc_mshc_cfg cfg = { .base = 0U, .intid = 0 };

	return (index == 0U) ? &cfg : NULL;
}

int dwc_mshc_init(struct dwc_mshc *hc, const struct dwc_mshc_cfg *cfg)
{
	struct scripted_mshc_call *call = scripted_record(SCRIPTED_MSHC_OP_INIT);

	(void)memset(hc, 0, sizeof(*hc));
	if (cfg != NULL) {
		hc->cfg = *cfg;
	}
	hc->ready = true;
	if (call != NULL) {
		call->result = DWC_MSHC_OK;
	}

	return DWC_MSHC_OK;
}

void dwc_mshc_deinit(struct dwc_mshc *hc)
{
	struct scripted_mshc_call *call = scripted_record(SCRIPTED_MSHC_OP_DEINIT);

	(void)call;
	hc->ready = false;
}

int dwc_mshc_reset(struct dwc_mshc *hc)
{
	struct scripted_mshc_call *call = scripted_record(SCRIPTED_MSHC_OP_RESET);

	(void)hc;
	if (call != NULL) {
		call->result = DWC_MSHC_OK;
	}

	return DWC_MSHC_OK;
}

uint32_t dwc_mshc_set_clock(struct dwc_mshc *hc, uint32_t hz)
{
	struct scripted_mshc_call *call = scripted_record(SCRIPTED_MSHC_OP_SET_CLOCK);
	uint32_t achieved = hz;

	if (fail_clock) {
		achieved = 0U;
	}
	if (call != NULL) {
		call->hz = hz;
		call->result = (int)achieved;
	}

	card_clock_hz = achieved;
	hc->clock_hz = hz;
	hc->achieved_hz = achieved;
	hc->cclk_sel = 0U;

	return achieved;
}

void dwc_mshc_set_bus_width(struct dwc_mshc *hc, unsigned int width)
{
	struct scripted_mshc_call *call = scripted_record(SCRIPTED_MSHC_OP_SET_WIDTH);

	if (call != NULL) {
		call->width = width;
		call->result = DWC_MSHC_OK;
	}

	host_width_lines = width;
	hc->width = width;
}

bool dwc_mshc_card_present(const struct dwc_mshc *hc)
{
	(void)hc;

	return true;
}

bool dwc_mshc_card_busy(const struct dwc_mshc *hc)
{
	(void)hc;

	return false;
}

void dwc_mshc_read_state(const struct dwc_mshc *hc, uint32_t *pstate,
			 uint32_t *normal_int, uint32_t *error_int)
{
	(void)hc;
	if (pstate != NULL) {
		*pstate = 0U;
	}
	if (normal_int != NULL) {
		*normal_int = 0U;
	}
	if (error_int != NULL) {
		*error_int = 0U;
	}
}

int dwc_mshc_transfer(struct dwc_mshc *hc, struct dwc_mshc_cmd *cmd)
{
	struct scripted_mshc_call *call = scripted_record(SCRIPTED_MSHC_OP_TRANSFER);
	int ret;

	(void)hc;

	if (call != NULL) {
		call->index = cmd->index;
		call->arg = cmd->arg;
		call->write = (cmd->data != NULL) ? cmd->data->write : false;
		call->bytes = (cmd->data != NULL) ? cmd->data->len : 0U;
	}

	ret = scripted_answer(cmd);
	if ((ret == DWC_MSHC_OK) && (cmd->data != NULL)) {
		ret = scripted_data(cmd);
	}

	if (call != NULL) {
		call->result = ret;
	}

	return ret;
}

void dwc_mshc_census_get(struct dwc_mshc_census *out)
{
	if (out != NULL) {
		(void)memset(out, 0, sizeof(*out));
	}
}

void dwc_mshc_census_reset(void)
{
}

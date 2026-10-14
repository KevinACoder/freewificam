/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    scripted_mshc.h
 * @brief   A scripted eMMC card behind the board driver's interface.
 *
 * The suite links the REAL imported MMC card layer, the REAL port over it and
 * the REAL host glue; the only thing scripted is the controller, because a
 * controller is what the host suite cannot have. This stand-in implements the
 * board driver's interface (dwc_mshc.h) and models just enough of a card to let
 * the stack's initialization ladder and its block transfers run: canned
 * identity registers, a user area in host memory, and the command semantics the
 * ladder depends on.
 *
 * What it records is what the cases assert: every command with its argument and
 * its data shape, every clock and width the glue programmed, and the data that
 * crossed in each direction. The bus-width test's two commands are recorded too,
 * which is how the suite witnesses that the port answers them without reaching
 * the controller at all.
 */

#ifndef SCRIPTED_MSHC_H
#define SCRIPTED_MSHC_H

#include <stdbool.h>
#include <stdint.h>

#include "dwc_mshc.h"

/* The scripted card's geometry: a 512 Mbyte user area (1,048,576 blocks of
 * 512 bytes), which is small enough to keep in host memory and large enough for
 * the suite's block transfers. */
#define SCRIPTED_MSHC_BLOCKS		1048576U
#define SCRIPTED_MSHC_BLOCK_SIZE	512U

/* The scripted card's identity, as the CID and the CSD carry it. */
#define SCRIPTED_MSHC_PRODUCT		"ARJ11X"
#define SCRIPTED_MSHC_SERIAL		287661952U

/* What one recorded call was. */
enum scripted_mshc_op {
	SCRIPTED_MSHC_OP_INIT = 1,
	SCRIPTED_MSHC_OP_DEINIT,
	SCRIPTED_MSHC_OP_RESET,
	SCRIPTED_MSHC_OP_SET_CLOCK,
	SCRIPTED_MSHC_OP_SET_WIDTH,
	SCRIPTED_MSHC_OP_TRANSFER,
};

struct scripted_mshc_call {
	unsigned int	op;
	uint32_t	index;		/* the command, for a transfer */
	uint32_t	arg;
	bool		write;		/* data direction, for a transfer */
	uint32_t	bytes;		/* data length, for a transfer */
	uint32_t	hz;		/* requested clock, for set_clock */
	unsigned int	width;		/* lines, for set_width */
	int		result;		/* what the call answered */
};

#define SCRIPTED_MSHC_MAX_CALLS		512

/* Forget every recorded call, reset the card's state and restore the default
 * scripting. */
void scripted_mshc_reset(void);

/* The calls recorded since the last reset, in order. */
unsigned int scripted_mshc_call_count(void);
const struct scripted_mshc_call *scripted_mshc_call(unsigned int index);

/* How many recorded calls match an operation (and, for a transfer, a command
 * index; pass 0 for any). */
unsigned int scripted_mshc_count(unsigned int op, uint32_t index);

/* The user area, for a case that wants to read or plant bytes. */
uint8_t *scripted_mshc_user_area(void);

/* The width the glue last programmed, in signal lines, and the clock it last
 * achieved. */
unsigned int scripted_mshc_width(void);
uint32_t scripted_mshc_clock_hz(void);

/* --- fault injection ------------------------------------------------------- */

/* Make the next clock request answer 0 (refused), like a source the port cannot
 * serve. Cleared by scripted_mshc_reset(). */
void scripted_mshc_fail_clock(void);

/* Make every data transfer of the given command index fail with a data error.
 * Cleared by scripted_mshc_reset(). */
void scripted_mshc_fail_data(uint32_t index);

/* Make the scripted card report a width that disagrees with the one the glue
 * programmed: the "card refused the width" case, which the data transfers then
 * expose. Cleared by scripted_mshc_reset(). */
void scripted_mshc_card_width_override(unsigned int lines);

#endif /* SCRIPTED_MSHC_H */

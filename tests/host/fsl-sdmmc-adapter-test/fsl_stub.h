/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * fsl_stub.h - a scripted stand-in for the imported SDIO stack.
 *
 * The adapter under test talks to exactly nine entry points of the imported
 * stack. This stub implements those nine, records every call with its
 * arguments, and answers with a scripted status, so the adapter's mapping and
 * its length discipline can be asserted call by call instead of inferred from a
 * card's behaviour.
 *
 * The card descriptor the stub hands out is a real sdio_card_t, because the
 * adapter reads the block sizes and the staging buffer's size from it - the
 * values are scripted here rather than enumerated from hardware.
 */

#ifndef FSL_STUB_H
#define FSL_STUB_H

#include <stdbool.h>
#include <stdint.h>

#include "fsl_common.h"
#include "fsl_sdmmc.h"

/* the recorded operations, one per entry point the adapter uses */
enum fsl_stub_op {
	FSL_STUB_OP_CARD_START = 1,
	FSL_STUB_OP_CARD_STOP,
	FSL_STUB_OP_DIRECT,
	FSL_STUB_OP_EXTENDED,
	FSL_STUB_OP_SET_BLOCK_SIZE,
	FSL_STUB_OP_IRQ_PENDING,
};

#define FSL_STUB_MAX_CALLS	64

struct fsl_stub_call {
	unsigned int	op;
	unsigned int	func;
	unsigned int	addr;
	unsigned int	count;	/* bytes in byte mode, blocks in block mode */
	unsigned int	flags;	/* the flags the adapter passed through */
	bool		write;
	status_t	status;	/* what this call answered */
};

/* forget every recorded call and restore the default scripting */
void fsl_stub_reset(void);

/* the calls recorded since the last reset, in order */
unsigned int fsl_stub_count(void);
const struct fsl_stub_call *fsl_stub_call(unsigned int index);

/* make every entry point answer with this status until it is changed back */
void fsl_stub_fail_with(status_t status);

/* the card descriptor the stub hands out */
sdio_card_t *fsl_stub_card(void);

/* Script the card: the block size function 1 reports, the block size function 0
 * reports, the staging buffer's size, and the interrupt byte. */
void fsl_stub_set_block_size(unsigned int func, uint32_t size);
void fsl_stub_set_internal_buffer_size(uint32_t size);
void fsl_stub_set_pending_interrupt(uint8_t value);

/* The byte an extended or direct read writes into the caller's buffer: the
 * first byte is `base`, the next `base + 1`, and so on, so a caller can tell
 * which transfer filled which part of its buffer. */
void fsl_stub_set_rx_pattern(uint8_t base);

#endif /* FSL_STUB_H */

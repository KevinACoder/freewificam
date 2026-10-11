/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * mock_sdio_card.h - the synthetic SDIO card the engine's seam drives.
 *
 * @design test double: a scripted responder behind the seam (the SDIO layer's
 *         Gate 1 shape). The card answers from its own constant tables - the CCCR
 *         registers, each function's basic registers and a per-function data
 *         window - so a case can check the engine against the operations it
 *         asked for rather than against a card's behaviour. Every request is
 *         captured, faults can be injected one at a time, and the pending
 *         interrupt word is set by the case. Shape reference: the USB mock's
 *         synthetic controller, tests/host/linuxkpi-usb-test/mock_hcd.h:1-20.
 */

#ifndef MOCK_SDIO_CARD_H
#define MOCK_SDIO_CARD_H

#include "lkpi_sdio_backend.h"

/* the model's memory: eight function windows of this size (function 0's window
 * carries the CCCR at its start and each function's basic registers at
 * function_number * 0x100) */
#define	MOCK_SDIO_MEM_SIZE	0x800

/* one captured request */
struct mock_sdio_req {
	unsigned int	func;
	unsigned int	addr;
	unsigned int	bytes;		/* bytes moved, or the block size for a
						 * set-block-size request */
	unsigned int	flags;		/* the LKPI_SDIO_BE_F_* set, 0 for direct */
	int		direct;		/* one direct transfer */
	int		extended;	/* one extended transfer */
	int		block_size;	/* one block-size configuration */
	int		write;
	int		block;		/* block-mode extended transfer */
	int		incr;		/* address advances */
};

/* what a case can make the card do wrong */
enum mock_sdio_fault {
	MOCK_SDIO_FAULT_NONE = 0,
	MOCK_SDIO_FAULT_DIRECT,		/* the next direct transfer fails */
	MOCK_SDIO_FAULT_EXTENDED,	/* the next extended transfer fails */
	MOCK_SDIO_FAULT_LIE,		/* every read answers an inverted byte */
	MOCK_SDIO_FAULT_NO_CARD,	/* enumeration fails */
};

void		mock_sdio_card_install(void);
void		mock_sdio_card_reset(void);
void		mock_sdio_card_fault(enum mock_sdio_fault which);

/* the model's bytes, for seeding and for checking what a transfer moved */
u8		*mock_sdio_card_mem(unsigned int func, unsigned int addr);

/* the pending-interrupt word the card reports */
void		mock_sdio_card_set_pending(u8 bits);

/* the request capture */
void		mock_sdio_card_capture_clear(void);
unsigned int	mock_sdio_card_capture_count(void);
int		mock_sdio_card_capture_get(unsigned int i, struct mock_sdio_req *out);

/* how many transfers of each kind the card served */
unsigned int	mock_sdio_card_direct_calls(void);
unsigned int	mock_sdio_card_extended_calls(void);

/* the block size the card will use for a function's block transfers (the model
 * keeps it where the seam's set_block_size put it) */
unsigned int	mock_sdio_card_blk(unsigned int func);

#endif /* MOCK_SDIO_CARD_H */

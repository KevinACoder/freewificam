/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * lkpi_sdio_backend.h - the backend seam of the linuxkpi SDIO layer.
 *
 * The SDIO engine (linuxkpi_sdio.c) speaks Linux semantics to its callers and
 * reaches the card through this narrow interface only: bring the card up and
 * report what it is, tear it down, one direct register transfer, one extended
 * transfer, one block-size configuration, and one look at the pending-interrupt
 * word. The seam carries no register, ring, DMA or command-number vocabulary -
 * a backend is anything that can enumerate a card and move bytes to and from it.
 *
 * Implementations:
 *   - the host mock suite plugs in a synthetic card model (the Gate 1 shape:
 *     a scripted CCCR/FBR/CIS answer table, no hardware);
 *   - the target build maps the same six calls onto the imported SDIO stack
 *     over the board's host controller.
 *
 * Length discipline (a recorded design decision): the seam does NOT know that
 * the underlying host may refuse a transfer whose length is not a multiple of
 * four. Callers ask for the byte count they mean, and the adapter below the
 * seam is what makes that request hold on the host it sits on. Keeping the
 * rule out of the seam is what lets a second backend - the synthetic card -
 * ignore it entirely.
 *
 * Error codes are this seam's own, with the values kept identical to the SDMMC
 * library numbering the adapter maps from, so every mapping keeps its meaning
 * (kStatus_Fail is 1, kStatus_InvalidArgument 4, kStatus_Timeout 5, and the
 * SDMMC group's 18xx codes keep their numbers). LKPI_SDIO_BE_ERR_NOCARD is the
 * seam's addition, placed in the group's unused range. Error values travel as
 * the negative of the code.
 *
 * @design ports & adapters: the engine is the adapter-neutral core, a backend
 * is the adapter; the seam is deliberately narrow (six operations, one card)
 * so a second backend cannot fork the engine. Shape reference: this project's
 * USB seam, components/linuxkpi/usb/lkpi_usb_backend.h:1-37 (the vocabulary
 * discipline, the error-code family and the @design note this header follows).
 */

#ifndef LKPI_SDIO_BACKEND_H
#define LKPI_SDIO_BACKEND_H

#include "linuxkpi_core.h"

/* the most I/O functions one SDIO card can carry (CCCR function count is 3 bits) */
#define	LKPI_SDIO_BE_MAX_FUNCS		7

/* io_extended() flags */
#define	LKPI_SDIO_BE_F_WRITE		(1u << 0)	/* buffer is written to the card */
#define	LKPI_SDIO_BE_F_BLOCK		(1u << 1)	/* count is a block count, not bytes */
#define	LKPI_SDIO_BE_F_INCR		(1u << 2)	/* the card address advances */

/* error codes (the negative of these travels in return values) */
#define	LKPI_SDIO_BE_ERR_FAIL		1	/* generic failure (kStatus_Fail) */
#define	LKPI_SDIO_BE_ERR_INVAL		4	/* invalid argument (kStatus_InvalidArgument) */
#define	LKPI_SDIO_BE_ERR_TIMEOUT	5	/* timed out (kStatus_Timeout) */
#define	LKPI_SDIO_BE_ERR_XFER		1801	/* transfer failed */
#define	LKPI_SDIO_BE_ERR_BLKSIZE	1802	/* setting the block size failed */
#define	LKPI_SDIO_BE_ERR_HOST		1803	/* the host does not support this */
#define	LKPI_SDIO_BE_ERR_CARD		1804	/* the card does not support this */
#define	LKPI_SDIO_BE_ERR_RESP		1826	/* the card answered with an error response */
#define	LKPI_SDIO_BE_ERR_CIS		1831	/* reading the card's CIS failed */
#define	LKPI_SDIO_BE_ERR_NOTSDIO	1832	/* the card is not an SDIO card */
/* seam addition: no card came up, or no backend is installed */
#define	LKPI_SDIO_BE_ERR_NOCARD		1901

/* what the card turned out to be; the engine's function layer and the driver's
 * id table both read this, so it carries identity and geometry only - never a
 * register image. The two host limits are the controller's own ceilings: the
 * engine's transfer splitter respects them, and the backend is the only party
 * that knows them. */
struct lkpi_sdio_be_card_info {
	u16	vendor;				/* manufacturer code from the CIS */
	u16	device;				/* manufacturer information from the CIS */
	u16	fn0_max_block;			/* function 0's maximum block size, 0 if unstated */
	u16	fn_max_block[LKPI_SDIO_BE_MAX_FUNCS];	/* per I/O function, 0 if unstated */
	u8	func_count;			/* number of I/O functions present */
	u8	cccr_version;			/* CCCR format version (0x02 = 2.00) */
	u8	sdio_version;			/* SDIO specification version */
	u32	ocr;				/* the card's OCR, as the card reported it */
	u32	clock_hz;			/* the bus clock the card came up at */
	u32	host_max_blk_size;		/* the host's largest single block */
	u32	host_max_blk_count;		/* the host's largest block count in one transfer */
	u8	bus_width;			/* data lines in use (1, 4 or 8) */
	bool	memory_present;			/* the card also carries memory */
	bool	high_speed;			/* the bus was switched to high speed */
	bool	multi_block;			/* the card supports extended block transfers */
};

struct lkpi_sdio_be_ops {
	/* Bring the card up, enumerate it and fill *info. Returns 0 or the
	 * negative of a code above; on failure no other entry may be called. */
	int	(*card_init)(void *ctx, struct lkpi_sdio_be_card_info *info);
	/* Tear the card down; the seam calls it at most once per card_init(). */
	void	(*card_deinit)(void *ctx);
	/* One direct (single-register) transfer. On write *value is the byte to
	 * store; on read it is the destination. */
	int	(*io_direct)(void *ctx, unsigned int func, unsigned int addr,
		    bool write, u8 *value);
	/* One extended (buffered) transfer of count bytes, or of count blocks
	 * when LKPI_SDIO_BE_F_BLOCK is set. */
	int	(*io_extended)(void *ctx, unsigned int func, unsigned int addr,
		    void *buf, unsigned int count, unsigned int flags);
	/* Configure the block size the next block-mode transfer uses. */
	int	(*set_block_size)(void *ctx, unsigned int func, unsigned int size);
	/* Report which I/O functions have a pending interrupt. */
	int	(*irq_pending)(void *ctx, u8 *pending);
};

/* install the backend (the host mock harness and the target bring-up call this
 * once before the first card_init; without a backend every entry is refused
 * with -LKPI_SDIO_BE_ERR_NOCARD) */
void	lkpi_sdio_be_register(const struct lkpi_sdio_be_ops *ops, void *ctx);

/* non-zero once a backend is installed */
bool	lkpi_sdio_be_ready(void);

/* the seam's entries; each returns 0 or the negative of a code above */
int	lkpi_sdio_be_card_init(struct lkpi_sdio_be_card_info *info);
void	lkpi_sdio_be_card_deinit(void);
int	lkpi_sdio_be_io_direct(unsigned int func, unsigned int addr, bool write,
	    u8 *value);
int	lkpi_sdio_be_io_extended(unsigned int func, unsigned int addr, void *buf,
	    unsigned int count, unsigned int flags);
int	lkpi_sdio_be_set_block_size(unsigned int func, unsigned int size);
int	lkpi_sdio_be_irq_pending(u8 *pending);

#endif /* LKPI_SDIO_BACKEND_H */

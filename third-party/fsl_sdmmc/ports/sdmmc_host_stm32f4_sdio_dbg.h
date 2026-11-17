/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * sdmmc_host_stm32f4_sdio_dbg.h - the F429 SDIO host glue's own account.
 *
 * The host glue is the only layer that sees how a CMD53 actually ended: how
 * many bytes the card sent against the count the engine asked for, which
 * verdict ended the transfer (count, quiet FIFO, no progress, or an error
 * flag), and what STA/DCTRL/DCOUNT said at that moment.  The engine's
 * counters say what it asked for; this says what came back.
 *
 * One boot's records are a ring: a failing boot needs the last few transfers
 * before the stop, not aggregate counters alone.
 */

#ifndef SDMMC_HOST_STM32F4_SDIO_DBG_H
#define SDMMC_HOST_STM32F4_SDIO_DBG_H

#include <stdint.h>

#define F4SDIO_RX_REC_MAX	8u

/* how a multibyte read ended */
#define F4SDIO_RX_END_COUNT	0u	/* the requested byte count was read */
#define F4SDIO_RX_END_IDLE	1u	/* FIFO stayed quiet after progress */
#define F4SDIO_RX_END_NOBODY	2u	/* no byte arrived at all */
#define F4SDIO_RX_END_ERR	3u	/* RXOVERR/DCRCFAIL/DTIMEOUT/STBITERR */
#define F4SDIO_RX_END_OTHER	4u

struct f4sdio_rx_rec {
	uint32_t	req;		/* bytes the engine asked for */
	uint32_t	got;		/* bytes copied out of the FIFO */
	uint32_t	end;		/* F4SDIO_RX_END_* */
	uint32_t	us;		/* transfer duration */
	uint32_t	sta;		/* SDIO->STA at the end */
	uint32_t	dctrl;		/* SDIO->DCTRL at the end */
	uint32_t	dcount;		/* SDIO->DCOUNT at the end */
};

/* An RX-FIFO port read (CMD53 device 7), the only transfer that carries the
 * card's received frames.  Its size and first bytes say whether the card ever
 * handed the host frame data at all, which no driver counter can answer once
 * the driver's own parser has dropped it. */
#define F4SDIO_FIFO_REC_MAX	4u
#define F4SDIO_FIFO_HEAD	24u

struct f4sdio_fifo_rec {
	uint32_t	req;
	uint32_t	got;		/* bytes the FIFO actually delivered */
	uint32_t	copied;		/* bytes copied into the caller's buffer */
	uint32_t	end;		/* F4SDIO_RX_END_* */
	uint32_t	status;		/* the verdict the seam returned (0 ok) */
	uint32_t	zero;		/* the whole transfer came back zero */
	uint32_t	first;		/* first byte that was not zero, or got */
	uint8_t		head[F4SDIO_FIFO_HEAD];
};

struct f4sdio_dbg {
	uint32_t	irq_entries;	/* SDIO_IRQHandler entries */
	uint32_t	irq_sdioit;	/* entries that saw SDIOIT */
	uint32_t	irq_spurious;	/* entries without SDIOIT */
	uint32_t	dat1_high;	/* ISR samples with PC9 (DAT1) high */
	uint32_t	rx_xfers;
	uint32_t	rx_bytes;
	uint32_t	rx_count_end;
	uint32_t	rx_idle_end;
	uint32_t	rx_zero;
	uint32_t	rx_err;
	uint32_t	tx_xfers;
	uint32_t	tx_ok;
	uint32_t	tx_err;
	uint32_t	rec_head;	/* records written (ring index is head-1) */
	struct f4sdio_rx_rec rec[F4SDIO_RX_REC_MAX];
	/* the RX-FIFO port reads, separately from the register reads */
	uint32_t	fifo_reads;
	uint32_t	fifo_bytes;
	uint32_t	fifo_zero;	/* reads that delivered nothing but zeros */
	uint32_t	fifo_rec_head;
	struct f4sdio_fifo_rec fifo[F4SDIO_FIFO_REC_MAX];
};

/* Snapshot; the counters are written by the calling thread and the ISR, so a
 * torn 32-bit field is the only risk and every field stands alone. */
void f4sdio_dbg_get(struct f4sdio_dbg *out);

#endif /* SDMMC_HOST_STM32F4_SDIO_DBG_H */

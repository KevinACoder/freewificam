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
 * the driver's own parser has dropped it.  The head covers the card's receive
 * status descriptor (24 B) and the first bytes of the 802.11 frame behind it,
 * so the frame-control byte and the descriptor's own length/offset fields can
 * be read off a single record. */
#define F4SDIO_FIFO_REC_MAX	4u
#define F4SDIO_FIFO_HEAD	64u

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

/* A transmit-queue write (CMD53 to a TX FIFO port, devices 4/5/6): the frame
 * the driver handed the card.  The recorded head is the vendor's transmit
 * descriptor followed by the first bytes of the 802.11 frame, so "the frame
 * was written" and "the frame never left the driver" are distinguishable
 * without a second instrument. */
#define F4SDIO_TX_REC_MAX	4u
#define F4SDIO_TX_HEAD		16u

struct f4sdio_tx_rec {
	uint32_t	dev;		/* CMD53 device id: 4/5/6 = HI/MID/LO */
	uint32_t	len;		/* bytes the write carried */
	uint32_t	status;		/* the seam's verdict (0 ok) */
	/* The data path's own word at the instant the transfer's completion
	 * criterion was met, i.e. just before the glue drops DTEN.  The byte
	 * counter is what the criterion looks at; TXACT is what the criterion
	 * does not look at, and a still-set TXACT there means the last word was
	 * still on the wire when the clock was stopped. */
	uint32_t	sta_drop;	/* SDIO->STA at the completion instant */
	uint32_t	dcnt_drop;	/* SDIO->DCOUNT at the completion instant */
	uint32_t	txact_drop;	/* SDIO_FLAG_TXACT at the completion instant */
	uint32_t	drop_end;	/* 0 none, 1 done, 2 error flag, 3 spin */
	/* The data path's state at the two instants the write can go wrong: one
	 * bus write after the deferred arm (an empty-FIFO underrun already set
	 * there means arming itself failed) and right after the frame was loaded
	 * (an empty FIFO there means the words never landed, and a count still
	 * at the full length means the DPSM never started). */
	uint32_t	sta_arm;	/* SDIO->STA just after the data path armed */
	uint32_t	dctrl_arm;	/* SDIO->DCTRL just after the frame was loaded */
	uint32_t	sta_burst;	/* SDIO->STA just after the frame was loaded */
	uint32_t	dcnt_burst;	/* SDIO->DCOUNT just after the load */
	uint32_t	burst_cycles;	/* the fill loop's own duration, in core cycles */
	uint8_t		head[F4SDIO_TX_HEAD];
};

struct f4sdio_dbg {
	uint32_t	irq_entries;	/* SDIO_IRQHandler entries */
	uint32_t	irq_sdioit;	/* entries that saw SDIOIT */
	uint32_t	irq_spurious;	/* entries without SDIOIT */
	uint32_t	dat1_high;	/* ISR samples with PC9 (DAT1) high */
	/* The card-interrupt line's control state, sampled at the points that
	 * change it.  A line that was never armed shows up here before any
	 * counter does: `mask` without SDIOITIE or `sdioen` without DCTRL[11]
	 * means the detector was not listening, whatever the card did. */
	uint32_t	irq_armed;	/* engine asked for card interrupts */
	uint32_t	irq_masked_xfer;/* transfers that took the line down */
	uint32_t	mask;		/* SDIO->MASK at the last sample */
	uint32_t	dctrl;		/* SDIO->DCTRL at the last sample */
	uint32_t	sta;		/* SDIO->STA at the last sample */
	uint32_t	dat1_level;	/* PC9 at the last sample: 1 high, 0 low */
	uint32_t	self_poke;	/* level-held line re-notified in software */
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
	/* the transmit-queue writes, separately from the register writes */
	uint32_t	txq_rec_head;
	struct f4sdio_tx_rec txq[F4SDIO_TX_REC_MAX];
	/* The transmit completion's two counters.  `tx_txact_set` counts the
	 * byte-mode writes whose data path was still transmitting when the
	 * completion criterion fired; `tx_settle_timeout` counts the ones whose
	 * post-completion wait for that to end did not run out on its own. */
	uint32_t	tx_txact_set;
	uint32_t	tx_settle_timeout;
	/* byte-mode transmit writes whose completion wait ended on an error flag
	 * (TXUNDERR/DTIMEOUT/DCRCFAIL) rather than on the byte counter: the seam
	 * still returns success for those, so this counter is the only notice */
	uint32_t	tx_drop_err;
};

/* Snapshot; the counters are written by the calling thread and the ISR, so a
 * torn 32-bit field is the only risk and every field stands alone. */
void f4sdio_dbg_get(struct f4sdio_dbg *out);

#endif /* SDMMC_HOST_STM32F4_SDIO_DBG_H */

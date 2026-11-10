/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    lkpi_sdio_rx_probe.h
 * @brief   The receive-presentation probe: what the card offered, what the host
 *          read, and which interrupt bits announced it.
 *
 * @design  observation seam (the census' shape, components/linuxkpi/
 *          lkpi_census.h:39): the engine owns the counters and the vendored
 *          driver only publishes the three events it alone can see - the
 *          request length it just read from the card, the completion of the
 *          read it issued, and the interrupt status it is about to dispatch.
 *          A plain prototype header rather than the engine's own header keeps
 *          the driver's translation unit free of the SDIO engine's include
 *          world.
 *
 *          The events are no-ops when the image builds without the census, so
 *          the same driver source links into every image.
 * @date    18.10.2026
 */

#ifndef LKPI_SDIO_RX_PROBE_H
#define LKPI_SDIO_RX_PROBE_H

#ifdef CONFIG_LKPI_CENSUS

/* The card's answer to "how much is pending": one call per poll of
 * SDIO_REG_RX0_REQ_LEN (and per loop pass when the length already arrived
 * with the interrupt status). Zero is the card saying "nothing yet". */
void	lkpi_sdio_rx_note_offer(unsigned int req_len);

/* The read the driver issued for that offer: want is the requested length,
 * got the length the read carried (0 when it failed), ok non-zero when the
 * receive buffer was filled. */
void	lkpi_sdio_rx_note_read(unsigned int want, unsigned int got, int ok);

/* The interrupt status the driver is dispatching, before it is masked. */
void	lkpi_sdio_rx_note_hisr(unsigned int hisr);

#else	/* !CONFIG_LKPI_CENSUS */

static inline void
lkpi_sdio_rx_note_offer(unsigned int req_len)
{

	(void)req_len;
}

static inline void
lkpi_sdio_rx_note_read(unsigned int want, unsigned int got, int ok)
{

	(void)want;
	(void)got;
	(void)ok;
}

static inline void
lkpi_sdio_rx_note_hisr(unsigned int hisr)
{

	(void)hisr;
}

#endif	/* CONFIG_LKPI_CENSUS */

#endif /* LKPI_SDIO_RX_PROBE_H */

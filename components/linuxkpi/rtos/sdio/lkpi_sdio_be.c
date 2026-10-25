/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * lkpi_sdio_be - the SDIO seam's registration and its six dispatchers.
 *
 * The seam header declares the interface; this file is the one place that
 * holds the installed backend and turns a call into a call on it. It is kept
 * apart from the engine so the seam's vocabulary stays independent of the
 * engine's state, and so a backend can be exercised (the host test's synthetic
 * card) with nothing else of the layer linked in.
 *
 * @design ports & adapters, the port's plumbing: the same shape as the USB
 * seam's registration and dispatchers
 * (components/linuxkpi/usb/linuxkpi_usb.c:1986-2025) - a null backend refuses
 * every entry with the seam's own no-device code rather than dereferencing.
 */

#include "lkpi_sdio_backend.h"

static const struct lkpi_sdio_be_ops *lkpi_sdio_be_ops;
static void *lkpi_sdio_be_ctx;

void
lkpi_sdio_be_register(const struct lkpi_sdio_be_ops *ops, void *ctx)
{

	lkpi_sdio_be_ops = ops;
	lkpi_sdio_be_ctx = ctx;
}

bool
lkpi_sdio_be_ready(void)
{

	return lkpi_sdio_be_ops != NULL;
}

int
lkpi_sdio_be_card_init(struct lkpi_sdio_be_card_info *info)
{

	if (lkpi_sdio_be_ops == NULL || lkpi_sdio_be_ops->card_init == NULL)
		return -LKPI_SDIO_BE_ERR_NOCARD;
	return lkpi_sdio_be_ops->card_init(lkpi_sdio_be_ctx, info);
}

void
lkpi_sdio_be_card_deinit(void)
{

	if (lkpi_sdio_be_ops != NULL && lkpi_sdio_be_ops->card_deinit != NULL)
		lkpi_sdio_be_ops->card_deinit(lkpi_sdio_be_ctx);
}

int
lkpi_sdio_be_io_direct(unsigned int func, unsigned int addr, bool write,
    u8 *value)
{

	if (lkpi_sdio_be_ops == NULL || lkpi_sdio_be_ops->io_direct == NULL)
		return -LKPI_SDIO_BE_ERR_NOCARD;
	return lkpi_sdio_be_ops->io_direct(lkpi_sdio_be_ctx, func, addr, write,
	    value);
}

int
lkpi_sdio_be_io_extended(unsigned int func, unsigned int addr, void *buf,
    unsigned int count, unsigned int flags)
{

	if (lkpi_sdio_be_ops == NULL || lkpi_sdio_be_ops->io_extended == NULL)
		return -LKPI_SDIO_BE_ERR_NOCARD;
	return lkpi_sdio_be_ops->io_extended(lkpi_sdio_be_ctx, func, addr, buf,
	    count, flags);
}

int
lkpi_sdio_be_set_block_size(unsigned int func, unsigned int size)
{

	if (lkpi_sdio_be_ops == NULL || lkpi_sdio_be_ops->set_block_size == NULL)
		return -LKPI_SDIO_BE_ERR_NOCARD;
	return lkpi_sdio_be_ops->set_block_size(lkpi_sdio_be_ctx, func, size);
}

int
lkpi_sdio_be_irq_pending(u8 *pending)
{

	if (lkpi_sdio_be_ops == NULL || lkpi_sdio_be_ops->irq_pending == NULL)
		return -LKPI_SDIO_BE_ERR_NOCARD;
	return lkpi_sdio_be_ops->irq_pending(lkpi_sdio_be_ctx, pending);
}

int
lkpi_sdio_be_card_irq_mode(unsigned int enable)
{

	if (lkpi_sdio_be_ops == NULL || lkpi_sdio_be_ops->card_irq_mode == NULL)
		return -LKPI_SDIO_BE_ERR_HOST;
	return lkpi_sdio_be_ops->card_irq_mode(lkpi_sdio_be_ctx, enable);
}

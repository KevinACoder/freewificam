/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/mmc/card.h
 * @brief   The card record: what an SDIO card is, and the functions it carries.
 *
 * @design declaration-only compatibility face, grown per driver. The record
 *         shapes are the interface's own (include/linux/mmc/card.h): the CCCR
 *         summary the core fills from the card's common control registers, the
 *         CIS identity tuple, and the card record itself, whose one SDIO field a
 *         driver reaches for is the table of function devices. Fields this face
 *         does not carry are fields no compiled caller reads; the set starts
 *         from the known consumers and converges on the compile closure.
 *         Shape reference for a record-only face:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_MMC_CARD_H_
#define _LINUXKPI_LINUX_MMC_CARD_H_

#include "../../../linuxkpi_core.h"

#include <linux/device.h>

/* the most I/O functions one SDIO card can carry (CCCR function count is 3 bits) */
#define	SDIO_MAX_FUNCS		7

struct sdio_func;
struct mmc_host;

/* the card types the interface names */
#define	MMC_TYPE_MMC		0
#define	MMC_TYPE_SD		1
#define	MMC_TYPE_SDIO		2
#define	MMC_TYPE_SD_COMBO	3

/* the card state bits a driver reads through mmc_card_* helpers */
#define	MMC_STATE_PRESENT	(1u << 0)
#define	MMC_STATE_DEAD		(1u << 2)

/*
 * The CCCR summary: the card common control registers, read once at enumeration
 * and kept so the core does not re-read them per transfer. The multi-block bit
 * is what the engine's transfer splitter branches on.
 */
struct sdio_cccr {
	unsigned int	sdio_vsn;
	unsigned int	sd_vsn;
	unsigned int	multi_block:1,
			low_speed:1,
			wide_bus:1,
			high_power:1,
			high_speed:1,
			disable_cd:1,
			enable_async_irq:1;
};

/* the common CIS identity tuple (manufacturer and product codes, block size) */
struct sdio_cis {
	unsigned short	vendor;
	unsigned short	device;
	unsigned short	blksize;
};

/*
 * The card record. The SDIO half is the function table: the functions are the
 * devices a driver binds to, so the card keeps them and the count.
 */
struct mmc_card {
	struct mmc_host		*host;		/* the host this card belongs to */
	struct device		dev;		/* the card's own device */
	u32			ocr;		/* the OCR the card answered with */
	unsigned int		rca;		/* relative card address */
	unsigned int		type;		/* MMC_TYPE_* */
	unsigned int		state;		/* MMC_STATE_* */
	unsigned int		quirks;		/* card quirks */

	unsigned int		sdio_funcs;	/* number of I/O functions */
	struct sdio_cccr		cccr;		/* common card information */
	struct sdio_cis		cis;		/* common tuple information */
	struct sdio_func	*sdio_func[SDIO_MAX_FUNCS];	/* the functions */

	u8			major_rev;	/* card revision, major */
	u8			minor_rev;	/* card revision, minor */
};

#define	mmc_card_sdio(c)	((c)->type == MMC_TYPE_SDIO)

#endif /* _LINUXKPI_LINUX_MMC_CARD_H_ */

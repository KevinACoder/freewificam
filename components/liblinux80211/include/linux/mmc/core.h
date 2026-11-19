/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/mmc/core.h
 * @brief   The MMC/SD command, data and request records and the wait entry
 *          point a host driver's client uses to move one request.
 *
 * @design declaration-only compatibility face, grown per driver. The interface
 *         (include/linux/mmc/core.h) is the core's own header: the command/data/
 *         request records, the response-type and command-type encodings, and the
 *         entry points the block and SDIO layers issue requests through. What
 *         brcmfmac names here is the extended (CMD53) transfer it can build
 *         itself when the host supports scatter-gather: bcmsdh.c:334-500 fills one
 *         struct mmc_data/struct mmc_command pair per block window and calls
 *         mmc_wait_for_req(), and sdio.c:4174 resets the card through
 *         mmc_hw_reset().
 *
 *         The records are the interface's, member for member in its order for
 *         every member a compiled consumer names. Members the interface carries
 *         below that point (the UHS-II command slot, the CQE/crypto state, the
 *         block-layer-only pointers) are not carried: nothing here copies one of
 *         these records - the driver fills the members it names - so their absence
 *         changes no meaning, and they arrive with a consumer that reads them.
 *
 *         The port's answer for the three entry points is stated at their bodies
 *         (bus/sdio.c) and in the component's DIVERGENCES file: the engine moves
 *         one buffer per request (struct mmc_host.max_segs = 1), so the driver's
 *         scatter-gather branch - and with it mmc_wait_for_req() - is never taken,
 *         and the engine has no card-reset operation to run for mmc_hw_reset().
 * @date    19.11.2026
 */

#ifndef _LINUXKPI_LINUX_MMC_CORE_H_
#define _LINUXKPI_LINUX_MMC_CORE_H_

#include "../../../linuxkpi_core.h"

#include <linux/completion.h>
#include <linux/scatterlist.h>

struct mmc_card;
struct mmc_host;
struct mmc_request;
struct mmc_data;

/* [LNX] include/linux/mmc/core.h:28-45: the command record and the flags a
 * request's command carries. The driver builds one CMD53 per window:
 * `flags = MMC_RSP_SPI_R5 | MMC_RSP_R5 | MMC_CMD_ADTC`, and the data transfer
 * type in MMC_CMD_MASK is what tells the host the command has a data phase. */
struct mmc_command {
	u32			opcode;
	u32			arg;
	u32			resp[4];
	unsigned int		flags;		/* expected response type */
#define	MMC_RSP_PRESENT	(1 << 0)
#define	MMC_RSP_136	(1 << 1)	/* 136 bit response */
#define	MMC_RSP_CRC	(1 << 2)	/* expect valid crc */
#define	MMC_RSP_BUSY	(1 << 3)	/* card may send busy */
#define	MMC_RSP_OPCODE	(1 << 4)	/* response contains opcode */

#define	MMC_CMD_MASK	(3 << 5)	/* non-SPI command type */
#define	MMC_CMD_AC	(0 << 5)
#define	MMC_CMD_ADTC	(1 << 5)
#define	MMC_CMD_BC	(2 << 5)
#define	MMC_CMD_BCR	(3 << 5)

#define	MMC_RSP_SPI_S1	(1 << 7)	/* one status byte */
#define	MMC_RSP_SPI_S2	(1 << 8)	/* second byte */
#define	MMC_RSP_SPI_B4	(1 << 9)	/* four data bytes */
#define	MMC_RSP_SPI_BUSY (1 << 10)	/* card may send busy */

/* [LNX] include/linux/mmc/core.h:57-72: the native and SPI response types.
 * R5 is the SDIO command response the extended transfer is answered with. */
#define	MMC_RSP_NONE	(0)
#define	MMC_RSP_R1	(MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE)
#define	MMC_RSP_R1B	(MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE|MMC_RSP_BUSY)
#define	MMC_RSP_R2	(MMC_RSP_PRESENT|MMC_RSP_136|MMC_RSP_CRC)
#define	MMC_RSP_R3	(MMC_RSP_PRESENT)
#define	MMC_RSP_R4	(MMC_RSP_PRESENT)
#define	MMC_RSP_R5	(MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE)
#define	MMC_RSP_R6	(MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE)
#define	MMC_RSP_R7	(MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE)
#define	MMC_RSP_SPI_R5	(MMC_RSP_SPI_S1|MMC_RSP_SPI_S2)

	unsigned int		retries;	/* max number of retries */
	int			error;		/* command error */
	unsigned int		busy_timeout;	/* busy detect timeout in ms */
	struct mmc_data		*data;		/* data segment */
	struct mmc_request	*mrq;		/* associated request */
};

/* [LNX] include/linux/mmc/core.h:119-146: the data record. The driver fills sg
 * (the pre-allocated scatterlist), sg_len (how many entries this window uses),
 * blksz, blocks and flags, and reads error back. */
struct mmc_data {
	unsigned int		timeout_ns;	/* data timeout (in ns) */
	unsigned int		timeout_clks;	/* data timeout (in clocks) */
	unsigned int		blksz;		/* data block size */
	unsigned int		blocks;		/* number of blocks */
	unsigned int		blk_addr;	/* block address */
	int			error;		/* data error */
	unsigned int		flags;

/* [LNX] include/linux/mmc/core.h:128-129: the transfer direction. */
#define	MMC_DATA_WRITE		BIT(8)
#define	MMC_DATA_READ		BIT(9)

	unsigned int		bytes_xfered;

	struct mmc_command	*stop;		/* stop command */
	struct mmc_request	*mrq;		/* associated request */

	unsigned int		sg_len;		/* size of scatter list */
	int			sg_count;	/* mapped sg entries */
	struct scatterlist	*sg;		/* I/O scatter list */
	s32			host_cookie;	/* host private data */
};

/* [LNX] include/linux/mmc/core.h:147-166: the request that ties one command to
 * its data phase, plus the completion the core waits on. */
struct mmc_request {
	struct mmc_command	*sbc;		/* SET_BLOCK_COUNT for multiblock */
	struct mmc_command	*cmd;
	struct mmc_data		*data;
	struct mmc_command	*stop;

	struct completion	completion;
	struct completion	cmd_completion;
	void			(*done)(struct mmc_request *);	/* completion */

	struct mmc_host		*host;

	bool			cap_cmd_during_tfr;
	int			tag;
};

/* [LNX] include/linux/mmc/core.h:180,184,186: the entry points the compiled set
 * names. Bodies: bus/sdio.c (the engine owns the card record and the host). */
void	mmc_wait_for_req(struct mmc_host *host, struct mmc_request *mrq);
int	mmc_hw_reset(struct mmc_card *card);
void	mmc_set_data_timeout(struct mmc_data *data, const struct mmc_card *card);

#endif /* _LINUXKPI_LINUX_MMC_CORE_H_ */

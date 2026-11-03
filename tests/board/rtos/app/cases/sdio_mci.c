/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   sdio_mci.c
 * @brief  Case sdio_mci: the SDIO socket on this board (mmc1) driven by the
 *         ported MCI controller driver - brought up, enumerated by
 *         CMD52, and round-tripped by CMD53.
 *
 * What is claimed as evidence, one anchor family per question:
 *
 *   mci: hw ...            the controller's bring-up registers, as programmed
 *                          (FIFOTH, CARD_THRCTL, PWREN, UHS_REG_EXT, CNTRL)
 *   mci: clk ...           both clock points, as the register fields hold them
 *                          (the numbers the Linux GT trace records)
 *   mci: regdump ...       the whole register file, offset by offset
 *   mci: enum ...          the card as the stack parsed it (CCCR, vendor/device,
 *                          function count, block sizes, CIS pointer)
 *   mci: cmd52 ...         direct IO: N reads and N writes, zero errors
 *   mci: cmd53 byte ...    byte-mode extended IO in both directions, read back
 *                          through a direct command
 *   mci: cmd53 block ...   block-mode extended IO through the IDMAC
 *   mci: diff ...          the register fields this boot produced next to the
 *                          Linux GT's, on one line, so the two can be compared
 *   mci: result PASS|FAIL
 *
 * The pass conditions are the ones the round's plan states: A1 register
 * fields, A2 clock fields, A5 CMD52 all green, A6 CMD53 completing in both
 * modes. A failure prints the controller's snapshot taken at the failure
 * instant - the register is write-one-to-clear, so a later read is zeros.
 *
 * Non-goals kept explicit: rtw88 is not probed, DAT1 is not armed, and no
 * throughput is claimed. This case answers one question: does the SDIO path
 * this board's Linux driver is proven to drive complete commands and data
 * transfers here too?
 */

#include "cases.h"

#include "os/cmsis2/cmsis2.h"

#include "mci.h"
#include "cases/mci_gt_prelude.h"
#include "sdmmc_port.h"

/* The card's own CCCR/FBR registers (SDIO Simplified Spec 3.0). The stack
 * parsed these already; the case reads them again so the evidence does not
 * depend on the stack's bookkeeping. */
#define CCCR_CCCR_REV		0x00u
#define CCCR_SDIO_REV		0x01u
#define CCCR_IO_ENABLE		0x02u
#define CCCR_IO_READY		0x03u
#define CCCR_INT_ENABLE		0x04u
#define CCCR_INT_PENDING	0x05u
#define CCCR_IO_ABORT		0x06u
#define CCCR_BUS_INTERFACE	0x07u
#define CCCR_CARD_CAPABILITY	0x08u
#define CCCR_CIS_POINTER	0x09u
#define CCCR_BUS_SUSPEND	0x0cu
#define CCCR_FUNCTION_SELECT	0x0du
#define CCCR_EXEC_FLAGS		0x0eu
#define CCCR_READY_FLAGS	0x0fu
#define CCCR_FN0_BLOCK_SIZE	0x10u
#define CCCR_POWER_CONTROL	0x12u
#define CCCR_BUS_SPEED		0x13u
#define CCCR_UHS_I_SUPPORT	0x14u
#define CCCR_DRIVER_STRENGTH	0x15u
#define CCCR_INTERRUPT_EXT	0x16u

/* function 1's FBR block, at 0x100 + n */
#define FBR_FN1			0x100u
#define FBR_FN1_CIS_POINTER	(FBR_FN1 + 0x09u)
#define FBR_FN1_CSA_POINTER	(FBR_FN1 + 0x0cu)
#define FBR_FN1_BLOCK_SIZE	(FBR_FN1 + 0x10u)

/* The receive-FIFO style ports a real driver reads; not touched here. The byte
 * round trip uses CCCR registers whose write has no side effect on a card that
 * is not being driven: the block-size low byte and the driver-strength field.
 * Both are read back through a direct command, so the claim is "the write
 * landed", not "the card did something with it". */

#define SDIO_MCI_CMD52_READS	64u
#define SDIO_MCI_CMD52_WRITES	16u
#define SDIO_MCI_BLOCK_SIZE	512u
#define SDIO_MCI_BYTE_COUNT	4u

/*
 * The block arms, in the shape the real driver uses - both numbers come from
 * the driver, not from a round size that looks tidy.
 *
 * Write: 9 blocks is what the GT itself sends (aln-mmc1-init.txt:315,
 * arg=9d081809 -> blkcnt=9 = the 4608-byte TX aggregate rtw88's H2C path
 * pushes). Reproducing the GT's own transfer is the only way the ALN DONE
 * lines are comparable field for field.
 *
 * Read: 8 blocks is rtw88's receive read. rtw_sdio_read_port() takes
 * min(host->max_req_size, count) per call (sdio.c:521-523) and the mmc core
 * sets max_req_size = PAGE_SIZE (core/host.c:571), so a 4 KiB chunk is 8
 * blocks of 512. That is the chunk size a real RX asks the controller for.
 */
#define SDIO_MCI_BLOCK_WR_COUNT	9u	/* GT's blkcnt=9 */
#define SDIO_MCI_BLOCK_RD_COUNT	8u	/* rtw88 max_req_size/PAGE_SIZE */

/*
 * Where the extended transfers go, taken from the card's own driver rather
 * than invented: the Linux GT trace for this socket (runs/20261030-firefly-v2-
 * sdio-mci-port/boot-linux-gt-2.log, the boot where the card enumerated) has
 * 563 CMD53s and every one of them is function 1, byte mode, four bytes,
 * incrementing - 281 reads and 278 writes to address 0x10030, plus a handful
 * at 0x14 and 0x24. There is not one CMD53 to function 0 in the whole boot:
 * function 0's CIS and CCCR are reached with CMD52 only.
 *
 * That distinction is the difference between the vendor stack's failure and
 * this port's first attempt: a CMD53 aimed at function 0 on this card is
 * accepted (R5 comes back clean) and then never produces a data phase - the
 * controller waits out its DTO budget with DATA_BUSY still set. Function 1 is
 * where the card actually moves bytes.
 *
 * 0x10030 is in rtw88's SDIO-local register window (SDIO_LOCAL_OFFSET
 * 0x10250000 with the top half masked off, sdio.h:19-21 in
 * wireless-drivers/rtw88), which is the address space the driver uses for its
 * per-function registers.
 */
#define SDIO_MCI_FN1		1u
/*
 * The GT's very first extended transfer on this socket is at 0x24, and the
 * 0x10030 address only starts being used once rtw88 has brought the MAC up.
 * The first read therefore targets 0x24 - the address a working driver uses
 * before any power-on sequence - so "does CMD53 complete on this controller"
 * is asked with the address the GT proves works, not with one that may need
 * MAC state a bare SDIO case has not created.
 */
#define SDIO_MCI_FN1_ADDR	0x24u
#define SDIO_MCI_FN1_ADDR_ALT	0x10030u
/* The address the GT's own first CMD53 uses (aln-mmc1-init.txt, arg=16210004). */
#define SDIO_MCI_FN1_ADDR_GT	0x11080u

/*
 * Block transfers must not use a byte-mode address, and this is the mistake
 * that made the block arm look broken.
 *
 * CMD53's arg[16:13] is not part of the register offset: it SELECTS A WINDOW
 * (rtw88 sdio.h:124-131):
 *
 *   0 SDIO_REG   0x24 is REG_SDIO_HCPWM1 - a four-byte configuration register
 *   4 TXFF_HIGH  the GT's own block write targets 0x840c, in this window
 *   7 RXFF       rtw88 reads its receive FIFO at 0x0e000 | id
 *
 * The block arm used to send 1024 bytes to 0x24, which is a four-byte
 * register. The card cannot supply ten blocks of data for a four-byte
 * register, so the read timing out was CORRECT CARD BEHAVIOUR, not a driver
 * defect - and the write that "passed" only proved the card accepts bytes it
 * then discards. Both arms were asking the wrong question.
 *
 * The two addresses below are the window-carrying ones the real driver uses:
 * the write is the GT's own first block write (aln-mmc1-init.txt:315,
 * arg=9d081809 -> 0x840c), and the read is rtw88's receive FIFO
 * (sdio.c:523, RTW_SDIO_ADDR_RX_RX0FF_GEN(0) = 0x0e000).
 */
#define SDIO_MCI_BLOCK_WR_ADDR	0x840cu	/* TXFF_HIGH, GT arg=9d081809 */
#define SDIO_MCI_BLOCK_RD_ADDR	0x0e000u	/* RXFF, rtw88 rx port */

/* Buffers: 4 KiB aligned (the staging path inside the stack copies through a
 * page-aligned buffer, and the IDMAC takes addresses as they come). */
static uint8_t sdio_mci_rbuf[SDIO_MCI_BLOCK_RD_COUNT * 512u] __attribute__((aligned(4096)));
static uint8_t sdio_mci_wbuf[SDIO_MCI_BLOCK_WR_COUNT * 512u] __attribute__((aligned(4096)));

static int sdio_mci_ok = 1;

static void sdio_mci_fail(const char *what, int rc)
{
	CASE_ANCHOR("mci: FAIL %s rc=%d\n", what, rc);
	sdio_mci_ok = 0;
}

/* --- A0: the same data path on the other instance -------------------------
 *
 * mmc0 is the TF slot: the same MCI IP, a different socket, and a card U-Boot
 * has just read at 50 MHz 4-bit. That makes it a control for the one question
 * the sweeps could not answer - whether this port's data path works at all.
 * If an SD block read completes on mmc0 while every CMD53 fails on mmc1, the
 * defect is in the SDIO path (or in what the card needs before a data phase);
 * if it fails identically there too, the defect is in the port's data
 * submission and the SDIO card is not implicated.
 *
 * The sequence is the SD initialisation, the short form, at 400 kHz then at
 * the working rate - the same shape firefly_mci's Linux host walks, minus
 * everything that belongs to the mmc core.
 */
static int sdio_mci_sd_read_probe(void)
{
	const struct mci_cfg *cfg;
	struct mci_cmd cmd;
	struct mci_cmd stop;
	struct mci_data data;
	uint32_t rca = 0u;
	int rc;
	int rc2 = 0;

	cfg = mci_board_cfg(0u);	/* mmc0: the TF slot */
	if (cfg == NULL) {
		CASE_ANCHOR("mci: sd mmc0 no cfg\n");

		return -1;
	}

	/*
	 * The stack owns the controller instance, so this probe cannot simply
	 * allocate a second one on the same registers - it would stomp the SDIO
	 * bring-up the rest of the case depends on. It is therefore run LAST,
	 * after every SDIO observation has been recorded, and it is allowed to
	 * leave the controller in whatever state it ends in.
	 */
	{
		static struct mci_host sd_hc;

		if (mci_init(&sd_hc, cfg) != MCI_OK) {
			CASE_ANCHOR("mci: sd mmc0 init failed\n");

			return -1;
		}

		(void)mci_set_clock(&sd_hc, 400000u);
		mci_set_bus_width(&sd_hc, 1u);

		/* CMD0: reset */
		(void)memset(&cmd, 0, sizeof(cmd));
		cmd.index = 0u;
		cmd.resp_class = MCI_RESP_NONE;
		rc = mci_transfer(&sd_hc, &cmd);
		CASE_ANCHOR("mci: sd CMD0 rc=%d\n", rc);

		/* CMD8: interface condition (SD v2) */
		(void)memset(&cmd, 0, sizeof(cmd));
		cmd.index = 8u;
		cmd.arg = 0x1aau;
		cmd.resp_class = MCI_RESP_SHORT_CRC;
		rc = mci_transfer(&sd_hc, &cmd);
		CASE_ANCHOR("mci: sd CMD8 rc=%d resp0=%08x\n", rc, cmd.response[0]);

		/* ACMD41 in a loop: the card comes out of idle when bit31 sets */
		{
			unsigned i;

			for (i = 0u; i < 20u; i++) {
				(void)memset(&cmd, 0, sizeof(cmd));
				cmd.index = 55u;
				cmd.arg = 0u;
				cmd.resp_class = MCI_RESP_SHORT_CRC;
				if (mci_transfer(&sd_hc, &cmd) != MCI_OK) {
					break;
				}
				(void)memset(&cmd, 0, sizeof(cmd));
				cmd.index = 41u;
				cmd.arg = 0x40ff8000u;
				cmd.resp_class = MCI_RESP_SHORT;
				if (mci_transfer(&sd_hc, &cmd) != MCI_OK) {
					break;
				}
				if ((cmd.response[0] & 0x80000000u) != 0u) {
					CASE_ANCHOR("mci: sd ACMD41 ocr=%08x after %u\n",
						    cmd.response[0], i + 1u);
					break;
				}
			}
		}

		/* CMD2 / CMD3: CID, then the RCA the card assigns itself */
		(void)memset(&cmd, 0, sizeof(cmd));
		cmd.index = 2u;
		cmd.resp_class = MCI_RESP_LONG_CRC;
		rc = mci_transfer(&sd_hc, &cmd);
		CASE_ANCHOR("mci: sd CMD2 rc=%d cid0=%08x\n", rc, cmd.response[0]);

		(void)memset(&cmd, 0, sizeof(cmd));
		cmd.index = 3u;
		cmd.resp_class = MCI_RESP_SHORT_CRC;
		rc = mci_transfer(&sd_hc, &cmd);
		rca = cmd.response[0] & 0xffff0000u;
		CASE_ANCHOR("mci: sd CMD3 rc=%d rca=%08x\n", rc, rca);

		/* CMD7: select it */
		(void)memset(&cmd, 0, sizeof(cmd));
		cmd.index = 7u;
		cmd.arg = rca;
		cmd.resp_class = MCI_RESP_SHORT_CRC;
		rc = mci_transfer(&sd_hc, &cmd);
		CASE_ANCHOR("mci: sd CMD7 rc=%d resp0=%08x\n", rc, cmd.response[0]);

		/* 4-bit, then the working clock */
		(void)memset(&cmd, 0, sizeof(cmd));
		cmd.index = 55u;
		cmd.arg = rca;
		cmd.resp_class = MCI_RESP_SHORT_CRC;
		if (mci_transfer(&sd_hc, &cmd) == MCI_OK) {
			(void)memset(&cmd, 0, sizeof(cmd));
			cmd.index = 6u;
			cmd.arg = 0x02u;	/* 4-bit */
			cmd.resp_class = MCI_RESP_SHORT_CRC;
			(void)mci_transfer(&sd_hc, &cmd);
		}
		mci_set_bus_width(&sd_hc, 4u);
		(void)mci_set_clock(&sd_hc, 25000000u);

		/* CMD17: single block read, the data path under test */
		(void)memset(sdio_mci_rbuf, 0, 512u);
		(void)memset(&cmd, 0, sizeof(cmd));
		(void)memset(&data, 0, sizeof(data));
		data.buf = sdio_mci_rbuf;
		data.len = 512u;
		data.blksz = 512u;
		data.blkcnt = 1u;
		data.write = false;
		cmd.index = 17u;
		cmd.arg = 0u;
		cmd.resp_class = MCI_RESP_SHORT_CRC;
		cmd.data = &data;

		mci_set_poll_probe(8u);
		rc = mci_transfer(&sd_hc, &cmd);
		CASE_ANCHOR("mci: sd CMD17 rc=%d resp0=%08x "
			    "data=%02x%02x%02x%02x\n", rc, cmd.response[0],
			    sdio_mci_rbuf[0], sdio_mci_rbuf[1],
			    sdio_mci_rbuf[2], sdio_mci_rbuf[3]);

		/*
		 * CMD18: the same socket, the same driver, two blocks.
		 *
		 * The single-block read above proves the IDMAC works here at
		 * all; it does not separate "multi-block" from "SDIO". The
		 * socket's block read is a two-block CMD53, so without this
		 * arm a failure there has two candidate causes and no way to
		 * tell them apart. If this one completes, multi-block is fine
		 * on this controller and the difference is the SDIO command
		 * itself; if it fails the same way, the defect is in the
		 * multi-block path and the card is not implicated at all.
		 *
		 * CMD12 ends the transfer: a multi-block read left open keeps
		 * the card in data state and the next command meets a busy
		 * card.
		 */
		(void)memset(sdio_mci_rbuf, 0, 1024u);
		(void)memset(&cmd, 0, sizeof(cmd));
		(void)memset(&data, 0, sizeof(data));
		data.buf = sdio_mci_rbuf;
		data.len = 1024u;
		data.blksz = 512u;
		data.blkcnt = 2u;
		data.write = false;
		cmd.index = 18u;
		cmd.arg = 0u;
		cmd.resp_class = MCI_RESP_SHORT_CRC;
		cmd.data = &data;

		rc2 = mci_transfer(&sd_hc, &cmd);
		CASE_ANCHOR("mci: sd CMD18 rc=%d resp0=%08x "
			    "data=%02x%02x%02x%02x\n", rc2, cmd.response[0],
			    sdio_mci_rbuf[0], sdio_mci_rbuf[1],
			    sdio_mci_rbuf[2], sdio_mci_rbuf[3]);

		(void)memset(&stop, 0, sizeof(stop));
		stop.index = 12u;
		stop.arg = 0u;
		stop.resp_class = MCI_RESP_SHORT_CRC;
		(void)mci_transfer(&sd_hc, &stop);
	}

	return rc;
}

/* --- A1: the bring-up registers ------------------------------------------- */

static void sdio_mci_report_hw(struct mci_host *hc)
{
	struct mci_state st;

	mci_state_get(hc, &st);

	CASE_ANCHOR("mci: hw cur_ios_clk=%lu width=%u transfers=%lu\n",
		    (unsigned long)st.cur_ios_clk, st.bus_width,
		    (unsigned long)st.transfers);
	CASE_ANCHOR("mci: clk_field cur_ios_clk=%lu clkdiv=%08lx uhs_ext=%08lx "
		    "clk_divider=%08lx ctype=%08lx cntrl=%08lx\n",
		    (unsigned long)st.cur_ios_clk,
		    (unsigned long)st.clkdiv, (unsigned long)st.uhs_ext,
		    (unsigned long)st.clk_divider, (unsigned long)st.ctype,
		    (unsigned long)st.cntrl);
}

/* --- the CMD52 battery ---------------------------------------------------- */

/* Read every CCCR register and count the ones that answer without an error.
 * This is the "CMD52 has a reply for every request" claim: the trace's DONE
 * lines are the per-command evidence, this line is the count. */
static int sdio_mci_cmd52_reads(sdio_card_t *card, unsigned reads)
{
	unsigned i;
	unsigned errors = 0u;
	uint8_t v = 0u;

	for (i = 0u; i < reads; i++) {
		uint32_t reg = (i % (CCCR_INTERRUPT_EXT + 1u));

		if (SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0, reg, &v) !=
		    kStatus_Success) {
			errors++;
		}
	}

	CASE_ANCHOR("mci: cmd52 read n=%u errors=%u\n", reads, errors);

	return (errors == 0u) ? 0 : -1;
}

/*
 * The write half. A direct write of the value just read back is a no-op for a
 * card whose register has no side effect, and its response still carries the
 * data byte - which is what the R5 check reads. Two registers are used so one
 * of them is a value the card is known to keep (the block-size low byte,
 * which the stack has just written itself).
 */
/*
 * The GT prelude, cell by cell. Each entry is one CMD52 write the Linux boot
 * performed on mmc1 before its first CMD53; the function-1 register block the
 * card's own driver programs is 39 of the 45, and this port has never written
 * any of it.
 *
 * `limit` is the bisection handle: the replay covers the first `limit` cells.
 * A run that stops at 6 (the CCCR-only prefix) and a run that covers all 45
 * both follow the same path, so the difference between them is the prelude and
 * nothing else.
 */
static int sdio_mci_gt_prelude_limit(sdio_card_t *card, unsigned limit)
{
	unsigned errors = 0u;
	unsigned i;

	if (limit > MCI_GT_PRELUDE_CELLS) {
		limit = MCI_GT_PRELUDE_CELLS;
	}

	for (i = 0u; i < limit; i++) {
		const struct mci_gt_cell *cell = &mci_gt_prelude[i];
		uint8_t byte = cell->data;

		/*
		 * The prelude opens with fn0 0x06 <- 0x08: CCCR_ABORT. In the GT
		 * that cell lands before enumeration, when nothing is enabled yet.
		 * Replayed here it lands after enable_fn1() and resets the card's
		 * SDIO state, which is exactly what the trace shows - the first
		 * function-1 write times out and the following 38 follow it, while
		 * function 0 keeps answering. A replay that means to configure
		 * function 1 must not carry the reset that disables it.
		 */
		if ((cell->fn == 0u) && (cell->addr == 0x006u)) {
			CASE_ANCHOR("mci: gt prelude skip abort cell\n");
			continue;
		}
		status_t st = SDIO_IO_Write_Direct(
			card, (sdio_func_num_t)cell->fn, cell->addr,
			&byte, false);

		if (st != kStatus_Success) {
			errors++;
		}
	}

	CASE_ANCHOR("mci: gt prelude cells=%u of %u errors=%u\n", limit,
		    (unsigned)MCI_GT_PRELUDE_CELLS, errors);
	return (errors == 0u) ? 0 : -1;
}

static int sdio_mci_gt_prelude(sdio_card_t *card)
{
	return sdio_mci_gt_prelude_limit(card, MCI_GT_PRELUDE_CELLS);
}

/* gcc only treats the pair as used when the gate is compiled in. */
__attribute__((unused))
static int (*const sdio_mci_gt_prelude_ref)(sdio_card_t *) = sdio_mci_gt_prelude;

static int sdio_mci_cmd52_writes(sdio_card_t *card, unsigned writes)
{
	unsigned i;
	unsigned errors = 0u;

	for (i = 0u; i < writes; i++) {
		uint8_t v = 0u;
		uint8_t w;

		if (SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0,
					CCCR_FN0_BLOCK_SIZE, &v) != kStatus_Success) {
			errors++;
			continue;
		}
		/* toggle the reserved high bit of the power-control field's
		 * neighbour: writing the value just read is the safe form */
		w = v;
		if (SDIO_IO_Write_Direct(card, kSDIO_FunctionNum0,
					 CCCR_FN0_BLOCK_SIZE, &w, false) !=
		    kStatus_Success) {
			errors++;
		}
	}

	CASE_ANCHOR("mci: cmd52 write n=%u errors=%u\n", writes, errors);

	return (errors == 0u) ? 0 : -1;
}

/* --- A5: the enumeration -------------------------------------------------- */

static int sdio_mci_report_card(sdio_card_t *card)
{
	uint8_t fn0_blk_lo = 0u;
	uint8_t fn0_blk_hi = 0u;
	uint8_t fn1_blk_lo = 0u;
	uint8_t fn1_blk_hi = 0u;
	uint8_t cis0 = 0u;
	uint8_t cis1 = 0u;
	uint8_t cis2 = 0u;
	uint8_t rev = 0u;
	uint8_t bus = 0u;
	uint8_t cap = 0u;

	if (SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0, CCCR_CCCR_REV, &rev) != 0) {
		return -1;
	}

	CASE_ANCHOR("mci: enum cccr=0x%02x sdio_ver=%u cccr_rev=%u fn=%u "
		    "vendor=0x%04x device=0x%04x cccrflags=0x%08lx\n",
		    rev, card->sdioVersion, card->cccrVersioin,
		    card->ioTotalNumber, card->commonCIS.mID,
		    card->commonCIS.funcID, (unsigned long)card->cccrflags);

	if (SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0, CCCR_BUS_INTERFACE,
				&bus) != 0) {
		return -1;
	}
	if (SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0, CCCR_CARD_CAPABILITY,
				&cap) != 0) {
		return -1;
	}
	CASE_ANCHOR("mci: cccr bus_interface=0x%02x capability=0x%02x "
		    "block_size_lo=0x%02x\n", bus, cap, card->io0blockSize & 0xffu);

	if ((SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0, CCCR_FN0_BLOCK_SIZE,
				 &fn0_blk_lo) != 0) ||
	    (SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0,
				 CCCR_FN0_BLOCK_SIZE + 1u, &fn0_blk_hi) != 0)) {
		return -1;
	}
	CASE_ANCHOR("mci: fn0 blksz=%u\n",
		    (unsigned)(fn0_blk_lo | ((unsigned)fn0_blk_hi << 8)));

	/* the CIS pointer, three bytes at 0x09..0x0b */
	if ((SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0, CCCR_CIS_POINTER,
				 &cis0) != 0) ||
	    (SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0, CCCR_CIS_POINTER + 1u,
				 &cis1) != 0) ||
	    (SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0, CCCR_CIS_POINTER + 2u,
				 &cis2) != 0)) {
		return -1;
	}
	CASE_ANCHOR("mci: cis ptr=0x%06x\n",
		    (unsigned)(cis0 | ((unsigned)cis1 << 8) | ((unsigned)cis2 << 16)));

	/* function 1: its block size and its CIS pointer, when it exists */
	if (card->ioTotalNumber >= 1u) {
		unsigned fn1 = card->ioFBR[0].ioBlockSize;
		uint8_t p0 = 0u, p1 = 0u, p2 = 0u;

		(void)SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0,
					  FBR_FN1_BLOCK_SIZE, &fn1_blk_lo);
		(void)SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0,
					  FBR_FN1_BLOCK_SIZE + 1u, &fn1_blk_hi);
		(void)SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0,
					  FBR_FN1_CIS_POINTER, &p0);
		(void)SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0,
					  FBR_FN1_CIS_POINTER + 1u, &p1);
		(void)SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0,
					  FBR_FN1_CIS_POINTER + 2u, &p2);
		CASE_ANCHOR("mci: fn1 blksz_stack=%u blksz_reg=%u cis=0x%06x\n",
			    fn1,
			    (unsigned)(fn1_blk_lo | ((unsigned)fn1_blk_hi << 8)),
			    (unsigned)(p0 | ((unsigned)p1 << 8) |
				       ((unsigned)p2 << 16)));
	}

	return 0;
}

/* --- A6: CMD53 ------------------------------------------------------------ */

/*
 * Function 1 has to be switched on before any extended transfer reaches it,
 * and the imported stack's enumerate path does not do it: SDIO_CardInitProc
 * stops after the CCCR/CIS read, the bus width and the timing, and never
 * touches CCCR's IO_ENABLE byte. Linux does it in sdio_enable_func(), which
 * rtw88 calls from rtw_sdio_probe() before its first register access.
 *
 * The GT trace for this socket shows the write and the block-size
 * programming right between the enumeration and the first CMD53:
 *   CMD52 fn0 0x02 (IO_ENABLE)  <- 0x02          (enable function 1)
 *   CMD52 fn0 0x110, 0x111      <- 0x00, 0x02    (FBR block size = 512)
 * ... and only then the first CMD53 at function 1.
 *
 * Without the IO_ENABLE step the card answers CMD53 with a clean R5 and then
 * never drives the data phase: measured on this board as rawints=0x04 only
 * (command done, no DTO) and DATA_BUSY stuck at 1 in STATUS, identically for
 * byte and block mode, reads and writes.
 */
static int sdio_mci_enable_fn1(sdio_card_t *card)
{
	uint8_t io_en = 0u;
	uint8_t bus_int = 0u;
	uint8_t blk_lo = 0u;
	uint8_t blk_hi = 0x02u;	/* 512 in little endian */
	uint8_t wanted;
	int ret = 0;

	/*
	 * The order is the GT's, not the specification's convenience. Linux's
	 * lab trace on this socket writes, in this sequence and no other:
	 *
	 *   fn0 0x007 <- 0x02   CCCR BUS_INTERFACE   (4-bit, from SetDataBusWidth)
	 *   fn0 0x110 <- 0x00   FBR fn1 block size low
	 *   fn0 0x111 <- 0x02   FBR fn1 block size high
	 *   fn0 0x002 <- 0x02   CCCR IO_ENABLE       (sdio_enable_func)
	 *
	 * The first port did "enable, then block size" and never wrote the bus
	 * interface byte itself - it inherited whatever the stack's enumerate
	 * path left there. Getting the order wrong is not cosmetic: the card
	 * latches its per-function state at the moment IO_ENABLE rises, so a
	 * block size written afterwards is too late to be part of it.
	 */
	if (SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0, CCCR_BUS_INTERFACE,
				&bus_int) != kStatus_Success) {
		CASE_ANCHOR("mci: bus interface read failed\n");

		return -1;
	}

	wanted = (uint8_t)((bus_int & 0xfcu) | 0x02u);	/* 4-bit */
	if (SDIO_IO_Write_Direct(card, kSDIO_FunctionNum0, CCCR_BUS_INTERFACE,
				 &wanted, false) != kStatus_Success) {
		CASE_ANCHOR("mci: bus interface write failed\n");

		return -1;
	}

	/* the function's block size, in the FBR block that function 0 owns */
	if (SDIO_IO_Write_Direct(card, kSDIO_FunctionNum0, FBR_FN1_BLOCK_SIZE,
				 &blk_lo, false) != kStatus_Success) {
		ret = -1;
	}
	if (SDIO_IO_Write_Direct(card, kSDIO_FunctionNum0,
				 FBR_FN1_BLOCK_SIZE + 1u, &blk_hi,
				 false) != kStatus_Success) {
		ret = -1;
	}

	/* and only now does the function come up */
	if (SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0, CCCR_IO_ENABLE,
				&io_en) != kStatus_Success) {
		CASE_ANCHOR("mci: fn1 enable read failed\n");

		return -1;
	}

	wanted = (uint8_t)(io_en | (1u << SDIO_MCI_FN1));
	if (SDIO_IO_Write_Direct(card, kSDIO_FunctionNum0, CCCR_IO_ENABLE,
				 &wanted, false) != kStatus_Success) {
		CASE_ANCHOR("mci: fn1 enable write failed io_enable=%02x\n",
			    (unsigned)io_en);

		return -1;
	}

	if (SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0, CCCR_IO_ENABLE,
				&io_en) != kStatus_Success) {
		io_en = 0u;
		ret = -1;
	}

	CASE_ANCHOR("mci: fn1 enable io_enable=%02x blksz=512 rc=%d\n",
		    (unsigned)io_en, ret);

	/*
	 * The stack's own bookkeeping has to learn the block size too, or block
	 * mode goes out with blksz=0. SDIO_CardInitProc never calls
	 * SDIO_SetBlockSize(), so ioFBR[].ioBlockSize stays zero from the
	 * descriptor's initialisation all the way to the first extended
	 * transfer - measured here as "blksz=0 blkcnt=2 bytcnt=00000000" in the
	 * CMD53 trace. Byte mode is unaffected, which is why the byte-mode
	 * probes looked identical either way.
	 */
	{
		status_t bs = SDIO_SetBlockSize(card, (sdio_func_num_t)SDIO_MCI_FN1,
						SDIO_MCI_BLOCK_SIZE);

		CASE_ANCHOR("mci: fn1 block size stack=%u rc=0x%x\n",
			    (unsigned)card->ioFBR[SDIO_MCI_FN1 - 1u].ioBlockSize,
			    (unsigned)bs);
	}

	/* A card may acknowledge the enable and still hold its IO_READY bit
	 * down while it brings the function up; a transfer started before that
	 * bit rises has no data phase. Linux's sdio_enable_func() waits for it
	 * (drivers/mmc/core/sdio_io.c), so this does too, and reports what it
	 * saw so a stuck bit is visible in the record rather than inferred. */
	{
		unsigned spins = 0u;
		uint8_t ready = 0u;

		while (spins < 1000u) {
			if (SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0,
						CCCR_IO_READY, &ready) !=
			    kStatus_Success) {
				break;
			}
			if ((ready & (uint8_t)(1u << SDIO_MCI_FN1)) != 0u) {
				break;
			}
			spins++;
		}

		CASE_ANCHOR("mci: fn1 ready io_ready=%02x spins=%u\n",
			    (unsigned)ready, spins);
	}

	return ret;
}

/*
 * The shape comes from the GT, not from the specification: function 1, byte
 * mode, four bytes, incrementing. On this card a CMD53 aimed at function 0 is
 * accepted (a clean R5 arrives) and then produces no data phase at all - the
 * controller sits out its DTO budget with DATA_BUSY still set. Function 1 is
 * where bytes actually move, which is also the function rtw88 binds to.
 */

/* One byte-mode read, reporting the raw stack status instead of a verdict.
 * The probe runs need the error code, not just "failed": the stack folds a
 * controller timeout and a card-side error flag into the same value, so the
 * distinction has to come from the driver's own anchors around it. */
static int sdio_mci_cmd53_byte_read_once(sdio_card_t *card)
{
	status_t st;

	(void)memset(sdio_mci_rbuf, 0, SDIO_MCI_BYTE_COUNT);

	st = SDIO_IO_Read_Extended(card, (sdio_func_num_t)SDIO_MCI_FN1,
				   SDIO_MCI_FN1_ADDR, sdio_mci_rbuf,
				   SDIO_MCI_BYTE_COUNT,
				   SDIO_EXTEND_CMD_OP_CODE_MASK);

	CASE_ANCHOR("mci: probe byte read st=0x%x data=%02x%02x%02x%02x\n",
		    (unsigned)st, sdio_mci_rbuf[0], sdio_mci_rbuf[1],
		    sdio_mci_rbuf[2], sdio_mci_rbuf[3]);

	return (st == kStatus_Success) ? 0 : -1;
}

/* Byte mode, read: four bytes out of function 1's register window. */
static int sdio_mci_cmd53_byte_read(sdio_card_t *card)
{
	unsigned errors = 0u;
	unsigned i;

	(void)memset(sdio_mci_rbuf, 0, SDIO_MCI_BYTE_COUNT);

	for (i = 0u; i < 4u; i++) {
		if (SDIO_IO_Read_Extended(card, (sdio_func_num_t)SDIO_MCI_FN1,
					  SDIO_MCI_FN1_ADDR, sdio_mci_rbuf,
					  SDIO_MCI_BYTE_COUNT,
					  SDIO_EXTEND_CMD_OP_CODE_MASK) !=
		    kStatus_Success) {
			errors++;
		}
	}

	CASE_ANCHOR("mci: cmd53 byte read n=4 func=%u addr=0x%05x errors=%u "
		    "data=%02x%02x%02x%02x\n", SDIO_MCI_FN1,
		    (unsigned)SDIO_MCI_FN1_ADDR, errors, sdio_mci_rbuf[0],
		    sdio_mci_rbuf[1], sdio_mci_rbuf[2], sdio_mci_rbuf[3]);

	return (errors == 0u) ? 0 : -1;
}

/*
 * Byte mode, write, then read the same window back with an extended read. The
 * write target is function 1's register window, the one the GT writes 278
 * times per boot. Both directions are proved by the pair: the write must
 * complete, and the read that follows must complete too.
 */
static int sdio_mci_cmd53_byte_write(sdio_card_t *card)
{
	unsigned errors = 0u;
	unsigned i;

	for (i = 0u; i < SDIO_MCI_BYTE_COUNT; i++) {
		sdio_mci_wbuf[i] = (uint8_t)(0xa5u + i);
	}

	if (SDIO_IO_Write_Extended(card, (sdio_func_num_t)SDIO_MCI_FN1,
				   SDIO_MCI_FN1_ADDR, sdio_mci_wbuf,
				   SDIO_MCI_BYTE_COUNT,
				   SDIO_EXTEND_CMD_OP_CODE_MASK) != kStatus_Success) {
		errors++;
	}

	(void)memset(sdio_mci_rbuf, 0, SDIO_MCI_BYTE_COUNT);
	if (SDIO_IO_Read_Extended(card, (sdio_func_num_t)SDIO_MCI_FN1,
				  SDIO_MCI_FN1_ADDR, sdio_mci_rbuf,
				  SDIO_MCI_BYTE_COUNT,
				  SDIO_EXTEND_CMD_OP_CODE_MASK) != kStatus_Success) {
		errors++;
	}

	CASE_ANCHOR("mci: cmd53 byte write n=%u func=%u addr=0x%05x errors=%u "
		    "readback=%02x%02x%02x%02x\n", SDIO_MCI_BYTE_COUNT,
		    SDIO_MCI_FN1, (unsigned)SDIO_MCI_FN1_ADDR, errors,
		    sdio_mci_rbuf[0], sdio_mci_rbuf[1], sdio_mci_rbuf[2],
		    sdio_mci_rbuf[3]);

	return (errors == 0u) ? 0 : -1;
}

/*
 * Block mode: a multi-block read through the IDMAC. Two 512-byte blocks is the
 * smallest transfer that both takes the block-mode path (a full block per
 * descriptor) and keeps the descriptor chain trivial to read back when it
 * fails.
 */
static int sdio_mci_cmd53_block_read(sdio_card_t *card)
{
	unsigned errors = 0u;
	unsigned i;

	(void)memset(sdio_mci_rbuf, 0, SDIO_MCI_BLOCK_RD_COUNT * 512u);

	for (i = 0u; i < 2u; i++) {
		if (SDIO_IO_Read_Extended(card, (sdio_func_num_t)SDIO_MCI_FN1,
					  SDIO_MCI_BLOCK_RD_ADDR, sdio_mci_rbuf,
					  SDIO_MCI_BLOCK_RD_COUNT,
					  SDIO_EXTEND_CMD_OP_CODE_MASK |
					  SDIO_EXTEND_CMD_BLOCK_MODE_MASK) !=
		    kStatus_Success) {
			errors++;
		}
	}

	CASE_ANCHOR("mci: cmd53 block read blkcnt=%u bytes=%u func=%u "
		    "addr=0x%05x errors=%u data=%02x%02x%02x%02x\n",
		    SDIO_MCI_BLOCK_RD_COUNT, SDIO_MCI_BLOCK_RD_COUNT * 512u,
		    SDIO_MCI_FN1, SDIO_MCI_BLOCK_RD_ADDR, errors,
		    sdio_mci_rbuf[0], sdio_mci_rbuf[1], sdio_mci_rbuf[2],
		    sdio_mci_rbuf[3]);

	return (errors == 0u) ? 0 : -1;
}

/* Block mode, write: the same blocks back into the same window. What the
 * anchor claims is that the transfer COMPLETED - R5 with no error flags, DTO
 * seen, IDMAC drained - which is the property under test. */
static int sdio_mci_cmd53_block_write(sdio_card_t *card)
{
	unsigned errors = 0u;
	unsigned i;

	for (i = 0u; i < (unsigned)sizeof(sdio_mci_wbuf); i++) {
		sdio_mci_wbuf[i] = (uint8_t)(i * 7u + 0x5au);
	}

	if (SDIO_IO_Write_Extended(card, (sdio_func_num_t)SDIO_MCI_FN1,
				   SDIO_MCI_BLOCK_WR_ADDR, sdio_mci_wbuf,
				   SDIO_MCI_BLOCK_WR_COUNT,
				   SDIO_EXTEND_CMD_OP_CODE_MASK |
				   SDIO_EXTEND_CMD_BLOCK_MODE_MASK) !=
	    kStatus_Success) {
		errors++;
	}

	CASE_ANCHOR("mci: cmd53 block write blkcnt=%u bytes=%u func=%u "
		    "addr=0x%05x errors=%u\n", SDIO_MCI_BLOCK_WR_COUNT,
		    SDIO_MCI_BLOCK_WR_COUNT * 512u, SDIO_MCI_FN1,
		    SDIO_MCI_BLOCK_WR_ADDR, errors);

	return (errors == 0u) ? 0 : -1;
}

/* gcc only treats the block-mode pair as used when the gate is compiled in. */
__attribute__((unused))
static int (*const sdio_mci_block_refs[])(sdio_card_t *) = {
	sdio_mci_cmd53_block_read,
	sdio_mci_cmd53_block_write,
};

/* --- the report ----------------------------------------------------------- */

static void sdio_mci_report_state(struct mci_host *hc)
{
	struct mci_state st;

	mci_state_get(hc, &st);

	CASE_ANCHOR("mci: counters transfers=%lu cmd52=%lu cmd53=%lu "
		    "data=%lu idmac=%lu fifo=%lu errors=%lu last_cmd=%lu "
		    "last_err=%d\n",
		    (unsigned long)st.transfers, (unsigned long)st.cmd52,
		    (unsigned long)st.cmd53, (unsigned long)st.data_stages,
		    (unsigned long)st.idmac_stages,
		    (unsigned long)st.fifo_stages,
		    (unsigned long)st.errors, (unsigned long)st.last_cmd,
		    st.last_err);
	CASE_ANCHOR("mci: last rawints=%08lx dmac=%08lx status=%08lx cmd=%08lx "
		    "resp0=%08lx blksz=%lu bytcnt=%lu imask=%08lx\n",
		    (unsigned long)st.rawints, (unsigned long)st.dmac,
		    (unsigned long)st.status, (unsigned long)st.cmd,
		    (unsigned long)st.resp0, (unsigned long)st.blksz,
		    (unsigned long)st.bytcnt, (unsigned long)st.int_mask);
}

/*
 * The diff line: the fields a Linux GT trace records, as this boot has them.
 * One line per field group, with the GT's value written next to it, so the
 * comparison is made where the reader is rather than after a transcript hunt.
 * The GT numbers are from runs/20261029-sdio-align-linux-parity's
 * aln-mmc1-init.txt / aln-mmc1-adtc-slice.txt (50 MHz, 4-bit).
 */
static void sdio_mci_report_diff(struct mci_host *hc)
{
	struct mci_state st;

	mci_state_get(hc, &st);

	CASE_ANCHOR("mci: diff clkdiv=%08lx gt=00040306 %s\n",
		    (unsigned long)st.clkdiv,
		    (st.clkdiv == 0x00040306UL) ? "same" : "DIFF");
	CASE_ANCHOR("mci: diff uhs_ext=%08lx gt=00000102 %s\n",
		    (unsigned long)st.uhs_ext,
		    (st.uhs_ext == 0x00000102UL) ? "same" : "DIFF");
	CASE_ANCHOR("mci: diff ctype=%08lx gt=00000001 %s\n",
		    (unsigned long)st.ctype,
		    (st.ctype == 0x00000001UL) ? "same" : "DIFF");
	CASE_ANCHOR("mci: diff cmdidx=%s\n",
		    (st.last_err == MCI_OK) ? "complete" : "failed");
}

/*
 * The CNTRL sweep. CMD53's data phase never starts on this board: the response
 * comes back clean (STATUS.RESPONSE_INDEX reads 53, CARD_BUSY clear) and
 * DATA_BUSY rises, but the FIFO stays empty and DTO never arrives - while
 * every command field matches the Linux GT that does complete the same
 * transfer. Nothing in the command itself is left to try, so the remaining
 * candidates are the controller's data-phase configuration, and CNTRL holds
 * all of them:
 *
 *   bit6  READ_WAIT          hold the read until the card's data start bit
 *   bit8  ABORT_READ_DATA    abandon a stuck read
 *   bit9  START_BIT_MODE     interpret a start bit at all
 *   bit10 CRC_SERIAL_DATA    CRC on the data lines
 *   bit3  DRV_SHIFT_EN       drive taps shifted
 *   bit5  DMA_ENABLE         the FIFO's own DMA request
 *
 * One boot, one variable at a time: each entry is applied, one byte-mode read
 * is attempted, and the result is recorded as an anchor. What distinguishes
 * "it worked" from "it did not" is the DTO bit in the driver's own DONE line,
 * which the trace prints either way.
 */
struct mci_cntrl_trial {
	const char	*name;
	uint32_t	bits;
};

static const struct mci_cntrl_trial sdio_mci_trials[] = {
	{ "readwait",		(1u << 6) },
	{ "readwait+startbit",	(1u << 6) | (1u << 9) },
	{ "startbit",		(1u << 9) },
	{ "crcdat",		(1u << 10) },
	{ "readwait+crcdat",	(1u << 6) | (1u << 10) },
	{ "readwait+startbit+crcdat", (1u << 6) | (1u << 9) | (1u << 10) },
	{ "dmaen",		(1u << 5) },
	{ "readwait+dmaen",	(1u << 6) | (1u << 5) },
	{ "drvshift",		(1u << 3) },
};

static void sdio_mci_cntrl_sweep(struct mci_host *hc, sdio_card_t *card)
{
	unsigned i;

	for (i = 0u; i < (unsigned)(sizeof(sdio_mci_trials) /
				    sizeof(sdio_mci_trials[0])); i++) {
		const struct mci_cntrl_trial *t = &sdio_mci_trials[i];
		int rc;

		mci_set_cntrl_extra(t->bits, 0u);
		CASE_ANCHOR("mci: sweep %s cntrl_bits=%08x\n", t->name,
			    (unsigned)t->bits);
		mci_set_poll_probe(3u);
		rc = sdio_mci_cmd53_byte_read_once(card);
		mci_set_cntrl_extra(0u, 0u);
		CASE_ANCHOR("mci: sweep %s rc=%d\n", t->name, rc);
	}
}

/*
 * The precondition sweep. CNTRL's own bits turned out not to matter - nine
 * variants, nine identical failures - so the difference has to be in what the
 * port does AROUND a data command rather than in how the command is shaped.
 * Four candidates, each a flag the driver honours only when armed:
 *
 *   no-fd0     the vendor register at 0xfd0 is written before every data
 *              command. Nothing documents it; the value is copied from Linux,
 *              which clears it from its interrupt handler. If the port's write
 *              is what holds the data path down, not writing it fixes that.
 *   fd0-ones   the same register the other way round, to tell "the write is
 *              harmful" from "this register is simply not the cause".
 *   dmac-on    Linux clears USE_INTERNAL_DMAC for a byte-mode transfer. The
 *              GT's idle register dump has it SET, so which state the data
 *              engine wants is an open question, not a settled one.
 *   busmode    the GT's idle BUS_MODE reads 0x200; the port leaves whatever
 *              the reset produced.
 */
struct mci_precond_trial {
	const char	*name;
	uint32_t	flags;
};

static const struct mci_precond_trial sdio_mci_preconds[] = {
	{ "no-fd0",		MCI_TRIAL_NO_FD0 },
	{ "fd0-ones",		MCI_TRIAL_FD0_ONES },
	{ "dmac-on",		MCI_TRIAL_DMAC_ON },
	{ "busmode-200",	MCI_TRIAL_BUSMODE },
	{ "no-fd0+dmac-on",	MCI_TRIAL_NO_FD0 | MCI_TRIAL_DMAC_ON },
	{ "dmac-on+busmode",	MCI_TRIAL_DMAC_ON | MCI_TRIAL_BUSMODE },
	{ "no-fd0+dmac-on+busmode",
	  MCI_TRIAL_NO_FD0 | MCI_TRIAL_DMAC_ON | MCI_TRIAL_BUSMODE },
};

static void sdio_mci_precond_sweep(sdio_card_t *card)
{
	unsigned i;

	for (i = 0u; i < (unsigned)(sizeof(sdio_mci_preconds) /
				    sizeof(sdio_mci_preconds[0])); i++) {
		const struct mci_precond_trial *t = &sdio_mci_preconds[i];
		int rc;

		mci_set_trial(t->flags);
		CASE_ANCHOR("mci: precond %s flags=%08x\n", t->name,
			    (unsigned)t->flags);
		mci_set_poll_probe(3u);
		rc = sdio_mci_cmd53_byte_read_once(card);
		mci_set_trial(0u);
		CASE_ANCHOR("mci: precond %s rc=%d\n", t->name, rc);
	}
}

/* --- the case ------------------------------------------------------------- */

int case_sdio_mci_run(void)
{
	struct mci_host *hc;
	sdio_card_t *card;
	status_t status;
	uint32_t clock;

	sdio_mci_ok = 1;

	/*
	 * The bring-up the seam performs, minus the seam: SDMMCHOST_Init runs
	 * from SDIO_Init's host step, so the controller's registers are only
	 * readable after the card has been brought up. Reading them before
	 * means reading a controller that has not been programmed.
	 */
	status = sdmmc_card_start();
	if (status != kStatus_Success) {
		CASE_ANCHOR("mci: FAIL card start status=0x%x\n", (unsigned)status);
		return -1;
	}

	hc = sdmmc_port_mci_host();
	if (hc == NULL) {
		CASE_ANCHOR("mci: FAIL no controller instance\n");
		return -1;
	}

	card = sdmmc_card();
	if (card == NULL) {
		CASE_ANCHOR("mci: FAIL no card instance\n");
		return -1;
	}

	/* A1: the bring-up registers, and the two clock points the stack asked
	 * for along the way (400 kHz identification, then its working rate). */
	sdio_mci_report_hw(hc);
	clock = card->busClock_Hz;
	CASE_ANCHOR("mci: clk bus=%luHz src=%luHz\n", (unsigned long)clock,
		    (unsigned long)card->host->sourceClock_Hz);

	/* the working clock point, re-applied so its fields are the ones the
	 * diff compares (the stack may already have it, in which case this is a
	 * no-op and the fields are still the ones it produced) */
	(void)mci_set_clock(hc, 50000000u);
	mci_set_bus_width(hc, 4u);
	{
		struct mci_state st;

		mci_state_get(hc, &st);
		CASE_ANCHOR("mci: clk50 clkdiv=%08lx uhs_ext=%08lx clkdiv2=%08lx\n",
			    (unsigned long)st.clkdiv, (unsigned long)st.uhs_ext,
			    (unsigned long)st.clk_divider);
	}

	mci_trace_dump(hc);

	/* A5: the enumeration, then the CMD52 battery. */
	if (sdio_mci_report_card(card) != 0) {
		sdio_mci_fail("card reports", -1);
	}

	if (sdio_mci_cmd52_reads(card, SDIO_MCI_CMD52_READS) != 0) {
		sdio_mci_fail("cmd52 reads", -1);
	}
	if (sdio_mci_cmd52_writes(card, SDIO_MCI_CMD52_WRITES) != 0) {
		sdio_mci_fail("cmd52 writes", -1);
	}

	/*
	 * C6: the GT prelude, replayed. The Linux boot performs 45 CMD52 writes
	 * on mmc1 before its first CMD53, and 39 of them are into function 1 -
	 * a register block this port never touches, because it only ever read
	 * CCCR/CIS and then went straight to the data command. The table below
	 * is generated from that capture (tools/rtos/mci-cells.py --emit-c), so
	 * the bytes are the GT's own, not a transcription.
	 *
	 * The first CMD53 of the GT follows this prelude on the same card, so a
	 * data phase that appears only after the replay, and only up to the
	 * prefix that carries the function-1 writes, is the answer to "what does
	 * the card need before it will move bytes".
	 */
	/*
	 * OFF BY DEFAULT. Replaying the prelude as a block wedges the card: its
	 * function-1 writes land while function 1 is still disabled, the card
	 * stops driving DAT, and from then on every fn0 read returns 0xff - which
	 * enable_fn1()'s read-modify-write then stores straight back into
	 * IO_ENABLE, killing fn1 for the rest of the boot. Measured on
	 * boot-rtos-prelude-replay.log: 39/39 fn1 CMD52 timeouts and
	 * io_enable=0xff, against 163/163 clean and io_enable=02 without it.
	 *
	 * That makes the replay a perturbation, not an experiment: it cannot be
	 * mixed into the baseline boot. It stays behind MCI_SDIO_GT_PRELUDE for
	 * whoever wants to measure its damage, and the cell-level comparison
	 * (tools/rtos/mci-cells.py) is the tool that survives from this branch.
	 */

	/* A6: the card has to be told to enable function 1 before any extended
	 * transfer can reach it; the imported enumerate path does not do it. */
	if (sdio_mci_enable_fn1(card) != 0) {
		sdio_mci_fail("fn1 enable", -1);
	}

	/*
	 * The GT prelude, replayed in the GT's own order. The order is the whole
	 * point: the Linux trace enables function 1 at write #6 (fn0 0x02 <- 0x02,
	 * line 122 of the capture) and only then writes the 39 function-1
	 * registers. Replaying the same bytes before enable_fn1() wedges the card
	 * - measured on boot-rtos-prelude-replay.log: every fn1 access times out
	 * and the fn0 read-back turns into 0xff, which enable_fn1()'s
	 * read-modify-write then stores back, killing fn1 for the rest of the
	 * boot. So the replay lives where the GT has it and nowhere else.
	 */
	if (sdio_mci_gt_prelude(card) != 0) {
		sdio_mci_fail("gt prelude", -1);
	}

	/*
	 * Is function 1 reachable at all? Every CMD53 on this card targets
	 * function 1, and this port has never issued a *direct* (CMD52) access
	 * inside function 1 - enumeration reads CCCR/CIS through function 0, and
	 * the FBR block size comes from the function-0 window. Linux's GT trace
	 * does read function 1 directly (fn1 0x11082 twice), so the window is
	 * reachable on this silicon. If it is not reachable from here, the
	 * CMD53 timeouts have nothing to do with the data phase: the card would
	 * simply be refusing function 1 altogether.
	 */
	{
		static const uint32_t fn1_cells[] = { 0x00u, 0x02u, 0x110u, 0x11082u };
		unsigned i;

		for (i = 0u; i < (unsigned)(sizeof(fn1_cells) /
					    sizeof(fn1_cells[0])); i++) {
			uint8_t val = 0xa5u;
			status_t st = SDIO_IO_Read_Direct(
				card, (sdio_func_num_t)SDIO_MCI_FN1,
				fn1_cells[i], &val);

			CASE_ANCHOR("mci: fn1 direct addr=0x%05x rc=0x%x val=%02x\n",
				    (unsigned)fn1_cells[i], (unsigned)st,
				    (unsigned)val);
		}
	}

	/*
	 * The GT's own first CMD53, byte for byte: function 1, byte mode,
	 * incrementing, address 0x11080, four bytes. Everything upstream of it
	 * is now as close to the GT as this port can get - same card, same
	 * clock, same bus width, same 45-cell prelude - so if this one also
	 * times out, the remaining difference is not in the command and not in
	 * the card's register state. The case's own 0x24 probe stays after it:
	 * a pass here and a pass there would mean two different things.
	 */
	{
		status_t st;

		(void)memset(sdio_mci_rbuf, 0, SDIO_MCI_BYTE_COUNT);
		st = SDIO_IO_Read_Extended(
			card, (sdio_func_num_t)SDIO_MCI_FN1,
			SDIO_MCI_FN1_ADDR_GT, sdio_mci_rbuf,
			SDIO_MCI_BYTE_COUNT, SDIO_EXTEND_CMD_OP_CODE_MASK);
		CASE_ANCHOR("mci: gt cmd53 st=0x%x data=%02x%02x%02x%02x\n",
			    (unsigned)st, sdio_mci_rbuf[0], sdio_mci_rbuf[1],
			    sdio_mci_rbuf[2], sdio_mci_rbuf[3]);
	}

	/*
	 * The differential experiment, one variable at a time. Every run so far
	 * has ended in the same place - a clean R5, no DTO, DATA_BUSY stuck -
	 * and the final state cannot say why. Three questions separate the
	 * candidate causes, and each has a cheap answer:
	 *
	 *   Q1 is the data phase never entered at all, or entered and stuck?
	 *      -> the per-pass time series (mci_set_poll_probe) shows whether
	 *         DATA_BUSY ever rises and whether CMD_START clears.
	 *   Q2 is it the 4-bit bus? CMD52 rides CMD only, so a misprogrammed
	 *      DAT path is invisible until the first data command.
	 *      -> the same transfer at 1-bit, then at 4-bit.
	 *   Q3 is it the block-size programming, or something more basic?
	 *      -> byte mode needs no FBR block size at all, so a byte-mode
	 *         pass at 1-bit with the FBR written is the cleanest case.
	 *
	 * The probe is armed only for the first of these, so its trace is one
	 * transfer's shape and not a mixture.
	 */
	/*
	 * The entry gate's control, inside this boot. mci_transfer() now waits
	 * for the controller the way firefly_mci_request() does; the two lines
	 * below submit the identical CMD53 twice, once with the gate and once
	 * with MCI_TRIAL_NO_GATE (the pre-gate behaviour). One boot, one
	 * variable, and the answer is the pair of completion events rather than
	 * two boots a day apart.
	 */
	CASE_ANCHOR("mci: pair control no-gate\n");
	mci_set_trial(MCI_TRIAL_NO_GATE);
	(void)sdio_mci_cmd53_byte_read_once(card);
	mci_set_trial(0u);
	CASE_ANCHOR("mci: pair experiment gate\n");
	(void)sdio_mci_cmd53_byte_read_once(card);

	CASE_ANCHOR("mci: q1 probe byte-read 4bit\n");
	mci_set_poll_probe(24u);
	(void)sdio_mci_cmd53_byte_read_once(card);

	CASE_ANCHOR("mci: q2 probe byte-read 1bit\n");
	mci_set_bus_width(hc, 1u);
	mci_set_poll_probe(24u);
	(void)sdio_mci_cmd53_byte_read_once(card);

	CASE_ANCHOR("mci: q3 probe byte-read 4bit again\n");
	mci_set_bus_width(hc, 4u);
	mci_set_poll_probe(24u);
	(void)sdio_mci_cmd53_byte_read_once(card);

	/* the CNTRL sweep: one boot, one variable at a time */
	sdio_mci_cntrl_sweep(hc, card);

	/* the precondition sweep: what surrounds the command */
	sdio_mci_precond_sweep(card);

	/* A6: the three data shapes. Each is independent: a byte-mode failure
	 * must not stop the block-mode probe, because the two shapes take
	 * different paths through the controller (FIFO vs IDMAC). */
	if (sdio_mci_cmd53_byte_read(card) != 0) {
		sdio_mci_fail("cmd53 byte read", -1);
	}
	if (sdio_mci_cmd53_byte_write(card) != 0) {
		sdio_mci_fail("cmd53 byte write", -1);
	}
	/*
	 * Block mode is behind a gate until the IDMAC path is understood. Both
	 * shapes abort the image (tx_fault_park, after an IDMAC command whose
	 * DMAC_STATUS stays 0x0000a000 - the descriptor is never consumed), and a
	 * crashed boot loses every anchor after it. The byte-mode shapes above
	 * are the ones this image now proves; a block-mode round turns this on
	 * and reads one failure at a time.
	 */
#if defined(MCI_SDIO_BLOCK_MODE)
	/*
	 * Write first, and the order is not cosmetic.
	 *
	 * A failed block transfer does not hand the controller back: CMD.START
	 * stays set and the clock restart spins out, so whatever runs next
	 * starts against a wedged controller. Measured: with the read first,
	 * the write that followed produced a command timeout (rawints=1000)
	 * rather than a data-phase result - it never got a fair test, and the
	 * one transfer Linux has ground truth for is the write (a blkcnt=9
	 * CMD53 whose ALN DONE reads ev=00000014). Putting the write first is
	 * what makes those two comparable.
	 *
	 * Each arm's probe covers its first attempt only: one trace, one
	 * transfer's shape.
	 */
	CASE_ANCHOR("mci: q4 probe block write\n");
	mci_set_poll_probe(24u);
	if (sdio_mci_cmd53_block_write(card) != 0) {
		sdio_mci_fail("cmd53 block write", -1);
	}
	/*
	 * The read arm runs LAST and is REPORTED, not asserted.
	 *
	 * The RX FIFO is only readable once the card's firmware has put a frame
	 * in it: rtw88 reads REG_SDIO_RX0_REQ_LEN first and issues the block
	 * read only when that is non-zero (sdio.c:1042-1052). This case runs no
	 * firmware, so the FIFO is empty and the card does not answer - measured
	 * as CMD.START staying set and a command timeout (rawints=00001000).
	 * That is the premise being absent, not the driver being wrong, so the
	 * arm still runs (the trace is the evidence) but its outcome does not
	 * decide the case. Two boots of this shape are recorded in
	 * runs/20261030-firefly-v2-sdio-mci-port/evidence/block-read-gap-
	 * 20261102.md.
	 *
	 * It goes after the write because a failed block transfer leaves the
	 * controller wedged (CMD.START set, clock update never completing), and
	 * the write is the direction the GT actually proves.
	 */
	CASE_ANCHOR("mci: q4 probe block read\n");
	mci_set_poll_probe(24u);
	(void)sdio_mci_cmd53_block_read(card);
#else
	CASE_ANCHOR("mci: block mode gated off (MCI_SDIO_BLOCK_MODE)\n");
#endif

	sdio_mci_report_state(hc);
	sdio_mci_report_diff(hc);

	/* A0 last: the same data path on mmc0, where a card is proven to work.
	 * Run after everything else because it re-programmes the controller
	 * instance at 0x28000000 and leaves it in whatever state it ends in. */
	(void)sdio_mci_sd_read_probe();

	CASE_ANCHOR("mci: result %s\n", sdio_mci_ok ? "PASS" : "FAIL");

	/* The card is left up: this case's evidence includes the state the
	 * stack is left in, and tearing it down would put the controller back
	 * into a state no later read could describe. */

	return sdio_mci_ok ? 0 : -1;
}

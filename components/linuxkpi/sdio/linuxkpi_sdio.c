/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_sdio.c
 * @brief   The SDIO engine: the Linux SDIO function API over the narrow backend
 *          seam.
 *
 * @design ports & adapters, the core side. The engine speaks Linux semantics
 *         (the interface in linux/mmc/sdio_func.h) and reaches the card through
 *         lkpi_sdio_backend.h only - six operations and one card record. It owns
 *         exactly the semantics that are the interface's, not the transport's:
 *         the exclusive bus claim, the function power-up sequence, the transfer
 *         splitter, the block-size bookkeeping, the interrupt pump and the
 *         driver registry. Shape references:
 *           - components/linuxkpi/usb/linuxkpi_usb.c:88-172 (the driver registry
 *             and the module-section walker this engine's registry follows);
 *           - components/linuxkpi/usb/linuxkpi_usb.c:2027-2062 (the pump thread
 *             and the idempotent start);
 *           - the interface's own bodies, which this file transcribes in
 *             behaviour and rewrites in code: the claim, the function enable
 *             sequence and the block-size rules come from the SDIO core, and the
 *             transfer splitter is the core's own helper (the multi-block path
 *             bounded by the host's block count and the nine-bit extended
 *             transfer's 511-block ceiling, the remainder in byte mode).
 *
 *         Two things the interface leaves to the port and this file therefore
 *         decides, both recorded in the component's DIVERGENCES file:
 *           - the pump thread is created once, at bring-up, and never torn down.
 *             It queries nothing while no handler is registered, so releasing the
 *             last IRQ stops delivery without a thread teardown whose races would
 *             cost more than the idle poll does;
 *           - a transfer whose ceiling is unstated (a function whose CIS ceiling
 *             was never read, reported as zero) is treated as "no ceiling" rather
 *             than as a zero-byte ceiling, which is what keeps the byte-mode chunk
 *             size from collapsing.
 * @date    12.10.2026
 */

#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"

#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>

#include "lkpi_sdio_backend.h"
#include "linuxkpi_sdio.h"

/* ------------------------------------------------------------------ */
/* constants and state                                                 */
/* ------------------------------------------------------------------ */

/* registered drivers (the USB engine's table depth; one driver per line) */
#define	LKPI_SDIO_DRIVERS_MAX		4

/* the interface caps a single extended transfer at 511 blocks */
#define	LKPI_SDIO_EXT_MAX_BLOCKS	511u

/* the byte-mode ceiling the interface states for a card without quirks */
#define	LKPI_SDIO_BYTE_MODE_MAX		512u

/* the function-enable poll budget, milliseconds (the interface's default) */
#define	LKPI_SDIO_ENABLE_TIMEOUT_MS	500u

/* the scratch buffer the 2-byte and 4-byte calls use */
#define	LKPI_SDIO_TMPBUF_SIZE		4u

/* the pump's idle period, milliseconds (the interface polls a host without an
 * interrupt line every 10 ms) */
#define	LKPI_SDIO_PUMP_IDLE_MS		10u

/* the pump runs a driver's handler, so its stack covers that handler's own
 * frames; sized from the engine's needs, raised if a handler overflows it */
#define	LKPI_SDIO_PUMP_STACK		8192u

/* the longest a pump_kick() waits for its cycle to finish */
#define	LKPI_SDIO_KICK_TIMEOUT_MS	200u

struct lkpi_sdio_card {
	bool				present;
	struct lkpi_sdio_be_card_info	info;
	struct mmc_host			host;
	struct mmc_card			card;
	/* one object per function number; [0] is the card itself */
	struct sdio_func		func[LKPI_SDIO_BE_MAX_FUNCS + 1];
	/* a function a driver's probe already claimed is not offered again */
	bool				claimed[LKPI_SDIO_BE_MAX_FUNCS + 1];
	u8				tmpbuf[LKPI_SDIO_BE_MAX_FUNCS + 1][LKPI_SDIO_TMPBUF_SIZE];
	char				name[LKPI_SDIO_BE_MAX_FUNCS + 1][12];
	osMutexId_t			claim;
	osThreadId_t			pump;
	osThreadId_t			claim_owner;
	osSemaphoreId_t			pump_req;
	osSemaphoreId_t			pump_done;
	bool				pump_running;
};

static struct lkpi_sdio_card lkpi_sdio_card_state;
static struct lkpi_sdio_stats lkpi_sdio_stats;
static bool lkpi_sdio_started;

static struct sdio_driver *lkpi_sdio_drivers[LKPI_SDIO_DRIVERS_MAX];
static unsigned int lkpi_sdio_driver_count;

/* ------------------------------------------------------------------ */
/* the seam's error family into Linux errno                            */
/* ------------------------------------------------------------------ */

/*
 * The seam's codes keep the imported stack's numbers, so this is a relabelling
 * into the interface's errno family. A transfer the card answered with an error
 * is -EIO (the interface maps a failed I/O command the same way); a missing card
 * is -ENODEV; a request the host cannot perform at all is -EOPNOTSUPP.
 */
static int
lkpi_sdio_errno(int be_err)
{

	switch (-be_err) {
	case 0:
		return 0;
	case LKPI_SDIO_BE_ERR_INVAL:
		return -EINVAL;
	case LKPI_SDIO_BE_ERR_TIMEOUT:
		return -ETIMEDOUT;
	case LKPI_SDIO_BE_ERR_NOCARD:
	case LKPI_SDIO_BE_ERR_NOTSDIO:
		return -ENODEV;
	case LKPI_SDIO_BE_ERR_HOST:
	case LKPI_SDIO_BE_ERR_CARD:
		return -EOPNOTSUPP;
	default:
		return -EIO;
	}
}

static struct lkpi_sdio_card *
lkpi_sdio_live(void)
{

	if (!lkpi_sdio_card_state.present)
		return NULL;
	return &lkpi_sdio_card_state;
}

/* ------------------------------------------------------------------ */
/* the bus claim                                                       */
/* ------------------------------------------------------------------ */

void
sdio_claim_host(struct sdio_func *func)
{
	struct lkpi_sdio_card *c = lkpi_sdio_live();

	if (func == NULL || c == NULL || c->claim == NULL)
		return;
	(void)osMutexAcquire(c->claim, osWaitForever);
	c->claim_owner = osThreadGetId();
	c->host.claimed = true;
	lkpi_sdio_stats.claims++;
}

void
sdio_release_host(struct sdio_func *func)
{
	struct lkpi_sdio_card *c = lkpi_sdio_live();

	if (func == NULL || c == NULL || c->claim == NULL)
		return;
	c->host.claimed = false;
	c->claim_owner = NULL;
	lkpi_sdio_stats.releases++;
	(void)osMutexRelease(c->claim);
}

bool
lkpi_sdio_claim_held_by_current(void)
{

	return lkpi_sdio_card_state.host.claimed &&
	    lkpi_sdio_card_state.claim_owner == osThreadGetId();
}

/* ------------------------------------------------------------------ */
/* single-register access (one direct transfer)                        */
/* ------------------------------------------------------------------ */

static int
lkpi_sdio_direct(unsigned int func, unsigned int addr, bool write, u8 *value)
{
	int st;

	st = lkpi_sdio_be_io_direct(func, addr, write, value);
	lkpi_sdio_stats.io_direct++;
	if (st != 0)
		lkpi_sdio_stats.errors++;
	return st;
}

u8
sdio_readb(struct sdio_func *func, unsigned int addr, int *err_ret)
{
	u8 val = 0;
	int st;

	if (func == NULL) {
		if (err_ret != NULL)
			*err_ret = -EINVAL;
		return 0xff;
	}
	st = lkpi_sdio_direct(func->num, addr, false, &val);
	if (st != 0) {
		if (err_ret != NULL)
			*err_ret = lkpi_sdio_errno(st);
		return 0xff;
	}
	if (err_ret != NULL)
		*err_ret = 0;
	return val;
}

void
sdio_writeb(struct sdio_func *func, u8 b, unsigned int addr, int *err_ret)
{
	int st;

	if (func == NULL) {
		if (err_ret != NULL)
			*err_ret = -EINVAL;
		return;
	}
	st = lkpi_sdio_direct(func->num, addr, true, &b);
	if (err_ret != NULL)
		*err_ret = st != 0 ? lkpi_sdio_errno(st) : 0;
}

unsigned char
sdio_f0_readb(struct sdio_func *func, unsigned int addr, int *err_ret)
{
	u8 val = 0;
	int st;

	if (func == NULL) {
		if (err_ret != NULL)
			*err_ret = -EINVAL;
		return 0xff;
	}
	st = lkpi_sdio_direct(0u, addr, false, &val);
	if (st != 0) {
		if (err_ret != NULL)
			*err_ret = lkpi_sdio_errno(st);
		return 0xff;
	}
	if (err_ret != NULL)
		*err_ret = 0;
	return val;
}

void
sdio_f0_writeb(struct sdio_func *func, unsigned char b, unsigned int addr,
    int *err_ret)
{
	int st;

	if (func == NULL) {
		if (err_ret != NULL)
			*err_ret = -EINVAL;
		return;
	}
	/* the interface restricts function-0 writes to the vendor range; this port
	 * carries no lenient-function-0 quirk, so the restriction always holds */
	if (addr < 0xf0u || addr > 0xffu) {
		if (err_ret != NULL)
			*err_ret = -EINVAL;
		return;
	}
	st = lkpi_sdio_direct(0u, addr, true, &b);
	if (err_ret != NULL)
		*err_ret = st != 0 ? lkpi_sdio_errno(st) : 0;
}

/* ------------------------------------------------------------------ */
/* buffered access (extended transfers)                                */
/* ------------------------------------------------------------------ */

/*
 * The largest byte-mode transfer this card and host allow: the controller's
 * block ceiling, the function's own ceiling when it stated one, and the
 * interface's 512-byte byte-mode maximum. A zero function ceiling means the CIS
 * never stated one, which is not the same as a zero-byte ceiling.
 */
static unsigned int
sdio_max_byte_size(const struct sdio_func *func)
{
	unsigned int mval = func->card->host->max_blk_size;

	if (func->max_blksize != 0u && mval > func->max_blksize)
		mval = func->max_blksize;
	if (mval > LKPI_SDIO_BYTE_MODE_MAX)
		mval = LKPI_SDIO_BYTE_MODE_MAX;
	if (mval == 0u)
		mval = LKPI_SDIO_BYTE_MODE_MAX;
	return mval;
}

/*
 * Split an arbitrarily sized transfer into extended transfers the card and the
 * host can each perform: whole blocks first (only when the card supports
 * multi-block transfers and the request is larger than one byte-mode chunk),
 * each command bounded by the host's block count and the interface's 511-block
 * ceiling, then the remainder in byte mode. The increment-address flag travels
 * with the request.
 */
static int
sdio_io_rw_ext_helper(struct sdio_func *func, bool write, unsigned int addr,
    bool incr, u8 *buf, unsigned int size)
{
	struct lkpi_sdio_card *c = lkpi_sdio_live();
	unsigned int remainder = size;
	unsigned int max_blocks;
	unsigned int base;

	if (func == NULL || c == NULL)
		return -ENODEV;
	if (func->num > (unsigned int)LKPI_SDIO_BE_MAX_FUNCS)
		return -EINVAL;
	if (size == 0u)
		return 0;

	base = write ? LKPI_SDIO_BE_F_WRITE : 0u;
	if (incr)
		base |= LKPI_SDIO_BE_F_INCR;

	if (func->card->cccr.multi_block != 0 &&
	    size > sdio_max_byte_size(func) && func->cur_blksize != 0u) {
		max_blocks = func->card->host->max_blk_count;
		if (max_blocks > LKPI_SDIO_EXT_MAX_BLOCKS)
			max_blocks = LKPI_SDIO_EXT_MAX_BLOCKS;
		if (max_blocks == 0u)
			max_blocks = LKPI_SDIO_EXT_MAX_BLOCKS;
		while (remainder >= func->cur_blksize) {
			unsigned int blocks = remainder / func->cur_blksize;
			unsigned int bytes;
			int st;

			if (blocks > max_blocks)
				blocks = max_blocks;
			bytes = blocks * func->cur_blksize;
			st = lkpi_sdio_be_io_extended(func->num, addr, buf, blocks,
			    base | LKPI_SDIO_BE_F_BLOCK);
			lkpi_sdio_stats.io_extended++;
			if (st != 0) {
				lkpi_sdio_stats.errors++;
				return lkpi_sdio_errno(st);
			}
			remainder -= bytes;
			buf += bytes;
			if (incr)
				addr += bytes;
		}
	}

	while (remainder > 0u) {
		unsigned int n = sdio_max_byte_size(func);
		int st;

		if (n > remainder)
			n = remainder;
		st = lkpi_sdio_be_io_extended(func->num, addr, buf, n, base);
		lkpi_sdio_stats.io_extended++;
		if (st != 0) {
			lkpi_sdio_stats.errors++;
			return lkpi_sdio_errno(st);
		}
		remainder -= n;
		buf += n;
		if (incr)
			addr += n;
	}
	return 0;
}

int
sdio_memcpy_fromio(struct sdio_func *func, void *dst, unsigned int addr,
    int count)
{

	if (count <= 0)
		return -EINVAL;
	return sdio_io_rw_ext_helper(func, false, addr, true, dst,
	    (unsigned int)count);
}

int
sdio_memcpy_toio(struct sdio_func *func, unsigned int addr, void *src,
    int count)
{

	if (count <= 0)
		return -EINVAL;
	return sdio_io_rw_ext_helper(func, true, addr, true, src,
	    (unsigned int)count);
}

int
sdio_readsb(struct sdio_func *func, void *dst, unsigned int addr, int count)
{

	if (count <= 0)
		return -EINVAL;
	return sdio_io_rw_ext_helper(func, false, addr, false, dst,
	    (unsigned int)count);
}

int
sdio_writesb(struct sdio_func *func, unsigned int addr, void *src, int count)
{

	if (count <= 0)
		return -EINVAL;
	return sdio_io_rw_ext_helper(func, true, addr, false, src,
	    (unsigned int)count);
}

/* the 2-byte and 4-byte calls ride the buffered path through the function's
 * scratch buffer, in the interface's byte order */
u16
sdio_readw(struct sdio_func *func, unsigned int addr, int *err_ret)
{
	int ret;

	if (func == NULL) {
		if (err_ret != NULL)
			*err_ret = -EINVAL;
		return 0xffff;
	}
	ret = sdio_memcpy_fromio(func, func->tmpbuf, addr, 2);
	if (err_ret != NULL)
		*err_ret = ret;
	if (ret != 0)
		return 0xffff;
	return le16_to_cpup((const __le16 *)func->tmpbuf);
}

u32
sdio_readl(struct sdio_func *func, unsigned int addr, int *err_ret)
{
	int ret;

	if (func == NULL) {
		if (err_ret != NULL)
			*err_ret = -EINVAL;
		return 0xffffffffu;
	}
	ret = sdio_memcpy_fromio(func, func->tmpbuf, addr, 4);
	if (err_ret != NULL)
		*err_ret = ret;
	if (ret != 0)
		return 0xffffffffu;
	return le32_to_cpup((const __le32 *)func->tmpbuf);
}

void
sdio_writew(struct sdio_func *func, u16 b, unsigned int addr, int *err_ret)
{
	int ret;

	if (func == NULL) {
		if (err_ret != NULL)
			*err_ret = -EINVAL;
		return;
	}
	*(__le16 *)func->tmpbuf = cpu_to_le16(b);
	ret = sdio_memcpy_toio(func, addr, func->tmpbuf, 2);
	if (err_ret != NULL)
		*err_ret = ret;
}

void
sdio_writel(struct sdio_func *func, u32 b, unsigned int addr, int *err_ret)
{
	int ret;

	if (func == NULL) {
		if (err_ret != NULL)
			*err_ret = -EINVAL;
		return;
	}
	*(__le32 *)func->tmpbuf = cpu_to_le32(b);
	ret = sdio_memcpy_toio(func, addr, func->tmpbuf, 4);
	if (err_ret != NULL)
		*err_ret = ret;
}

/* ------------------------------------------------------------------ */
/* function power                                                      */
/* ------------------------------------------------------------------ */

int
sdio_enable_func(struct sdio_func *func)
{
	struct lkpi_sdio_card *c = lkpi_sdio_live();
	unsigned int waited = 0;
	u8 reg = 0;
	int st;

	if (func == NULL || c == NULL)
		return -EINVAL;

	st = lkpi_sdio_direct(0u, SDIO_CCCR_IOEx, false, &reg);
	if (st != 0)
		return lkpi_sdio_errno(st);
	reg |= (u8)(1u << func->num);
	st = lkpi_sdio_direct(0u, SDIO_CCCR_IOEx, true, &reg);
	if (st != 0)
		return lkpi_sdio_errno(st);

	for (;;) {
		st = lkpi_sdio_direct(0u, SDIO_CCCR_IORx, false, &reg);
		if (st != 0)
			return lkpi_sdio_errno(st);
		if ((reg & (1u << func->num)) != 0)
			return 0;
		if (waited >= func->enable_timeout)
			return -ETIMEDOUT;
		lkpi_msleep(1);
		waited++;
	}
}

int
sdio_disable_func(struct sdio_func *func)
{
	struct lkpi_sdio_card *c = lkpi_sdio_live();
	u8 reg = 0;
	int st;

	if (func == NULL || c == NULL)
		return -EINVAL;

	st = lkpi_sdio_direct(0u, SDIO_CCCR_IOEx, false, &reg);
	if (st != 0)
		return lkpi_sdio_errno(st);
	reg &= (u8)~(1u << func->num);
	st = lkpi_sdio_direct(0u, SDIO_CCCR_IOEx, true, &reg);
	if (st != 0)
		return lkpi_sdio_errno(st);
	return 0;
}

int
sdio_set_block_size(struct sdio_func *func, unsigned int blksz)
{
	int st;

	if (func == NULL)
		return -EINVAL;
	if (blksz > func->card->host->max_blk_size)
		return -EINVAL;
	if (blksz == 0u) {
		blksz = func->card->host->max_blk_size;
		if (func->max_blksize != 0u && blksz > func->max_blksize)
			blksz = func->max_blksize;
		if (blksz > LKPI_SDIO_BYTE_MODE_MAX)
			blksz = LKPI_SDIO_BYTE_MODE_MAX;
	}
	st = lkpi_sdio_be_set_block_size(func->num, blksz);
	if (st != 0) {
		lkpi_sdio_stats.errors++;
		return lkpi_sdio_errno(st);
	}
	func->cur_blksize = blksz;
	return 0;
}

/* ------------------------------------------------------------------ */
/* the interrupt pump                                                  */
/* ------------------------------------------------------------------ */

static bool
lkpi_sdio_has_handler(void)
{
	unsigned int i;

	for (i = 1u; i <= (unsigned int)LKPI_SDIO_BE_MAX_FUNCS; i++) {
		if (lkpi_sdio_card_state.func[i].irq_handler != NULL)
			return true;
	}
	return false;
}

/*
 * One poll cycle, on the pump thread: claim the bus, read the card's pending
 * word, hand each pending function to its handler with the claim still held (the
 * interface's contract - a handler must not claim), then release. A function
 * with a pending bit but no handler is a finding, not a dispatch.
 */
static void
lkpi_sdio_pump_cycle(void)
{
	struct lkpi_sdio_card *c = &lkpi_sdio_card_state;
	u8 pending = 0;
	unsigned int i;

	(void)osMutexAcquire(c->claim, osWaitForever);
	c->claim_owner = osThreadGetId();
	c->host.claimed = true;
	lkpi_sdio_stats.claims++;

	lkpi_sdio_stats.pump_polls++;
	if (lkpi_sdio_be_irq_pending(&pending) == 0) {
		for (i = 1u; i <= (unsigned int)LKPI_SDIO_BE_MAX_FUNCS; i++) {
			if ((pending & (1u << i)) == 0)
				continue;
			if (c->func[i].irq_handler != NULL) {
				lkpi_sdio_stats.callbacks++;
				c->func[i].irq_handler(&c->func[i]);
			} else {
				lkpi_printf(ULOG_WARNING_LEVEL,
				    "sdiokpi: pending IRQ on function %u with no handler\n",
				    i);
			}
		}
	} else {
		lkpi_sdio_stats.errors++;
	}

	c->host.claimed = false;
	c->claim_owner = NULL;
	lkpi_sdio_stats.releases++;
	(void)osMutexRelease(c->claim);
}

static void
lkpi_sdio_pump_entry(void *arg)
{

	(void)arg;
	for (;;) {
		bool requested;

		requested = osSemaphoreAcquire(lkpi_sdio_card_state.pump_req,
		    LKPI_SDIO_PUMP_IDLE_MS) == osOK;
		if (lkpi_sdio_has_handler())
			lkpi_sdio_pump_cycle();
		if (requested)
			(void)osSemaphoreRelease(lkpi_sdio_card_state.pump_done);
	}
}

void
lkpi_sdio_pump_kick(void)
{
	struct lkpi_sdio_card *c = &lkpi_sdio_card_state;

	if (!c->pump_running)
		return;
	(void)osSemaphoreRelease(c->pump_req);
	(void)osSemaphoreAcquire(c->pump_done, LKPI_SDIO_KICK_TIMEOUT_MS);
}

int
sdio_claim_irq(struct sdio_func *func, sdio_irq_handler_t *handler)
{
	struct lkpi_sdio_card *c = lkpi_sdio_live();
	u8 reg = 0;
	int st;

	if (func == NULL || c == NULL)
		return -EINVAL;
	if (func->irq_handler != NULL)
		return -EBUSY;

	st = lkpi_sdio_direct(0u, SDIO_CCCR_IENx, false, &reg);
	if (st != 0)
		return lkpi_sdio_errno(st);
	reg |= (u8)(1u << func->num);
	reg |= 1u;	/* master interrupt enable */
	st = lkpi_sdio_direct(0u, SDIO_CCCR_IENx, true, &reg);
	if (st != 0)
		return lkpi_sdio_errno(st);

	func->irq_handler = handler;
	c->host.sdio_irqs++;
	lkpi_sdio_stats.irq_claims++;
	return 0;
}

int
sdio_release_irq(struct sdio_func *func)
{
	struct lkpi_sdio_card *c = lkpi_sdio_live();
	u8 reg = 0;
	int st;

	if (func == NULL || c == NULL)
		return -EINVAL;

	func->irq_handler = NULL;
	if (c->host.sdio_irqs > 0u)
		c->host.sdio_irqs--;

	st = lkpi_sdio_direct(0u, SDIO_CCCR_IENx, false, &reg);
	if (st != 0)
		return lkpi_sdio_errno(st);
	reg &= (u8)~(1u << func->num);
	/* the master bit goes with the last function interrupt */
	if ((reg & 0xfeu) == 0u)
		reg = 0u;
	st = lkpi_sdio_direct(0u, SDIO_CCCR_IENx, true, &reg);
	if (st != 0)
		return lkpi_sdio_errno(st);
	return 0;
}

/* ------------------------------------------------------------------ */
/* the driver registry                                                 */
/* ------------------------------------------------------------------ */

/*
 * The id table match: an entry matches when every field it states agrees with
 * the function, and the "any" value matches everything (the interface's own
 * comparison). The table ends at an all-zero entry.
 */
static const struct sdio_device_id *
lkpi_sdio_match(const struct sdio_func *func, const struct sdio_driver *drv)
{
	const struct sdio_device_id *id;

	if (drv->id_table == NULL)
		return NULL;
	for (id = drv->id_table; id->class != 0u || id->vendor != 0u ||
	    id->device != 0u; id++) {
		if (id->class != (u8)SDIO_ANY_ID && id->class != func->class)
			continue;
		if (id->vendor != (u16)SDIO_ANY_ID && id->vendor != func->vendor)
			continue;
		if (id->device != (u16)SDIO_ANY_ID && id->device != func->device)
			continue;
		return id;
	}
	return NULL;
}

/* hand every unclaimed function to a matching driver; the first claim wins */
static int
lkpi_sdio_probe(struct sdio_driver *drv)
{
	unsigned int i;

	for (i = 1u; i <= (unsigned int)LKPI_SDIO_BE_MAX_FUNCS; i++) {
		struct sdio_func *f = &lkpi_sdio_card_state.func[i];
		const struct sdio_device_id *id;
		int ret;

		if (!sdio_func_present(f) || lkpi_sdio_card_state.claimed[i])
			continue;
		id = lkpi_sdio_match(f, drv);
		if (id == NULL)
			continue;
		ret = drv->probe(f, id);
		if (ret == 0) {
			lkpi_sdio_card_state.claimed[i] = true;
			lkpi_sdio_stats.probes++;
			lkpi_printf(ULOG_INFO_LEVEL,
			    "sdiokpi: '%s' claimed %04x:%04x function %u\n",
			    drv->name, f->vendor, f->device, f->num);
			return 0;
		}
	}
	return -ENODEV;
}

int
__sdio_register_driver(struct sdio_driver *drv, struct module *owner)
{

	(void)owner;
	if (drv == NULL || drv->probe == NULL)
		return -EINVAL;
	if (lkpi_sdio_driver_count >= LKPI_SDIO_DRIVERS_MAX)
		return -ENOSPC;
	lkpi_sdio_drivers[lkpi_sdio_driver_count++] = drv;
	lkpi_sdio_stats.drivers++;
	lkpi_printf(ULOG_INFO_LEVEL, "sdiokpi: driver '%s' registered\n", drv->name);
	/* a driver that arrives after the card is up is probed at once, which is
	 * what the interface's bus does */
	if (lkpi_sdio_card_state.present)
		(void)lkpi_sdio_probe(drv);
	return 0;
}

void
sdio_unregister_driver(struct sdio_driver *drv)
{
	unsigned int i;

	for (i = 0u; i < lkpi_sdio_driver_count; i++) {
		if (lkpi_sdio_drivers[i] == drv)
			lkpi_sdio_drivers[i] = NULL;
	}
}

/* ------------------------------------------------------------------ */
/* power management                                                    */
/* ------------------------------------------------------------------ */

mmc_pm_flag_t
sdio_get_host_pm_caps(struct sdio_func *func)
{

	if (func == NULL)
		return 0;
	return func->card->host->pm_caps;
}

int
sdio_set_host_pm_flags(struct sdio_func *func, mmc_pm_flag_t flags)
{

	if (func == NULL)
		return -EINVAL;
	if ((flags & ~func->card->host->pm_caps) != 0u)
		return -EINVAL;
	func->card->host->pm_flags = flags;
	return 0;
}

/* ------------------------------------------------------------------ */
/* bring-up                                                            */
/* ------------------------------------------------------------------ */

/*
 * Read a function's standard-interface code from its own FBR block, which lives
 * in function 0's address space. The interface reads this at function
 * initialisation; a card that does not answer leaves the class unstated, which
 * is what a driver's class-based id entry then fails to match on.
 */
static u8
lkpi_sdio_read_class(unsigned int num)
{
	u8 val = 0;

	if (lkpi_sdio_direct(0u, SDIO_FBR_BASE(num) + SDIO_FBR_STD_IF, false,
	    &val) != 0)
		return 0;
	return val;
}

static void
lkpi_sdio_build_functions(void)
{
	struct lkpi_sdio_card *c = &lkpi_sdio_card_state;
	unsigned int i;

	(void)memset(c->claimed, 0, sizeof(c->claimed));
	c->card.host = &c->host;
	c->card.type = MMC_TYPE_SDIO;
	c->card.state = MMC_STATE_PRESENT;
	c->card.ocr = c->info.ocr;
	c->card.sdio_funcs = c->info.func_count;
	c->card.major_rev = (u8)((c->info.cccr_version >> 4) & 0x0fu);
	c->card.minor_rev = (u8)(c->info.cccr_version & 0x0fu);
	c->card.cccr.sdio_vsn = c->info.sdio_version;
	c->card.cccr.multi_block = c->info.multi_block ? 1u : 0u;
	c->card.cccr.wide_bus = (c->info.bus_width == 4u) ? 1u : 0u;
	c->card.cccr.high_speed = c->info.high_speed ? 1u : 0u;

	c->host.card = &c->card;
	c->host.max_blk_size = c->info.host_max_blk_size;
	c->host.max_blk_count = c->info.host_max_blk_count;
	c->host.ios.clock = c->info.clock_hz;
	c->host.ios.power_mode = MMC_POWER_ON;
	c->host.ios.bus_mode = MMC_BUSMODE_PUSHPULL;
	c->host.ios.bus_width = (c->info.bus_width == 4u) ? MMC_BUS_WIDTH_4 :
	    MMC_BUS_WIDTH_1;
	c->host.ios.timing = c->info.high_speed ? MMC_TIMING_SD_HS :
	    MMC_TIMING_LEGACY;
	c->host.ios.signal_voltage = MMC_SIGNAL_VOLTAGE_330;
	/* the card is hard-wired and always powered on this port, so the host can
	 * keep it powered across a suspend */
	c->host.pm_caps = MMC_PM_KEEP_POWER;
	c->host.pm_flags = MMC_PM_KEEP_POWER;

	for (i = 0u; i <= (unsigned int)LKPI_SDIO_BE_MAX_FUNCS; i++) {
		struct sdio_func *f = &c->func[i];

		(void)memset(f, 0, sizeof(*f));
		f->card = &c->card;
		f->num = i;
		f->tmpbuf = c->tmpbuf[i];
		f->enable_timeout = LKPI_SDIO_ENABLE_TIMEOUT_MS;
		(void)snprintf(c->name[i], sizeof(c->name[i]), "sdio%u", i);
		f->dev.name = c->name[i];
		f->vendor = c->info.vendor;
		f->device = c->info.device;
		f->major_rev = (u8)((c->info.cccr_version >> 4) & 0x0fu);
		f->minor_rev = (u8)(c->info.cccr_version & 0x0fu);
	}

	/* function 0 is the card itself and is always there */
	c->func[0].class = SDIO_CLASS_NONE;
	c->func[0].max_blksize = c->info.fn0_max_block;
	c->func[0].cur_blksize = c->info.fn0_max_block != 0u ?
	    c->info.fn0_max_block : c->info.host_max_blk_size;
	sdio_func_set_present(&c->func[0]);

	for (i = 1u; i <= c->info.func_count &&
	    i <= (unsigned int)LKPI_SDIO_BE_MAX_FUNCS; i++) {
		struct sdio_func *f = &c->func[i];

		f->class = lkpi_sdio_read_class(i);
		f->max_blksize = c->info.fn_max_block[i - 1u];
		/* the block size in effect starts at the largest the function, the
		 * host and the interface's byte-mode ceiling all allow */
		f->cur_blksize = f->max_blksize != 0u ? f->max_blksize :
		    c->info.host_max_blk_size;
		if (f->cur_blksize > c->info.host_max_blk_size)
			f->cur_blksize = c->info.host_max_blk_size;
		if (f->cur_blksize > LKPI_SDIO_BYTE_MODE_MAX)
			f->cur_blksize = LKPI_SDIO_BYTE_MODE_MAX;
		sdio_func_set_present(f);
		c->card.sdio_func[i - 1u] = f;
	}
}

static void
lkpi_sdio_pump_start(void)
{
	struct lkpi_sdio_card *c = &lkpi_sdio_card_state;
	osThreadAttr_t attr = { 0 };

	if (c->pump_running)
		return;
	c->pump_req = osSemaphoreNew(1u, 0u, NULL);
	c->pump_done = osSemaphoreNew(1u, 0u, NULL);
	if (c->pump_req == NULL || c->pump_done == NULL) {
		lkpi_printf(ULOG_ERROR_LEVEL, "sdiokpi: cannot create the pump semaphores\n");
		return;
	}
	attr.name = "lkpi_sdio_pump";
	attr.stack_size = LKPI_SDIO_PUMP_STACK;
	attr.priority = osPriorityNormal;
	c->pump = osThreadNew(lkpi_sdio_pump_entry, c, &attr);
	if (c->pump == NULL) {
		lkpi_printf(ULOG_ERROR_LEVEL, "sdiokpi: cannot start the pump thread\n");
		return;
	}
	c->pump_running = true;
}

void
lkpi_sdio_start(void)
{
	struct lkpi_sdio_card *c = &lkpi_sdio_card_state;
	unsigned int i;
	int st;

	if (lkpi_sdio_started)
		return;
	lkpi_sdio_started = true;

	if (!lkpi_sdio_be_ready()) {
		lkpi_printf(ULOG_ERROR_LEVEL,
		    "sdiokpi: no backend installed; the card cannot come up\n");
		return;
	}

	(void)memset(&c->info, 0, sizeof(c->info));
	st = lkpi_sdio_be_card_init(&c->info);
	if (st != 0) {
		lkpi_printf(ULOG_ERROR_LEVEL, "sdiokpi: card bring-up failed (%d)\n", st);
		return;
	}
	c->present = true;
	lkpi_sdio_stats.cards++;

	c->claim = osMutexNew(NULL);
	if (c->claim == NULL) {
		lkpi_printf(ULOG_ERROR_LEVEL, "sdiokpi: cannot create the bus claim\n");
		c->present = false;
		return;
	}

	lkpi_sdio_build_functions();
	lkpi_sdio_pump_start();

	/* a driver registered before the card arrived is probed now */
	for (i = 0u; i < lkpi_sdio_driver_count; i++) {
		if (lkpi_sdio_drivers[i] != NULL)
			(void)lkpi_sdio_probe(lkpi_sdio_drivers[i]);
	}

	lkpi_printf(ULOG_INFO_LEVEL,
	    "sdiokpi: card %04x:%04x, %u IO func(s), CCCR v%u.%u, bus %u kHz (%s)\n",
	    c->info.vendor, c->info.device, c->info.func_count,
	    (unsigned int)((c->info.cccr_version >> 4) & 0x0fu),
	    (unsigned int)(c->info.cccr_version & 0x0fu), c->info.clock_hz,
	    c->info.high_speed ? "HS" : "default");
}

int
lkpi_sdio_ready(void)
{

	return lkpi_sdio_card_state.present ? 1 : 0;
}

struct sdio_func *
lkpi_sdio_func(unsigned int num)
{

	if (!lkpi_sdio_card_state.present ||
	    num > (unsigned int)LKPI_SDIO_BE_MAX_FUNCS)
		return NULL;
	if (!sdio_func_present(&lkpi_sdio_card_state.func[num]))
		return NULL;
	return &lkpi_sdio_card_state.func[num];
}

struct sdio_func *
lkpi_sdio_func0(void)
{

	return lkpi_sdio_func(0u);
}

void
lkpi_sdio_stats_get(struct lkpi_sdio_stats *out)
{

	if (out == NULL)
		return;
	*out = lkpi_sdio_stats;
}

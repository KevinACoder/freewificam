/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * mock_sdio_card.c - the synthetic SDIO card behind the seam.
 *
 * @design test double (scripted responder). The six seam operations are answered
 *         from a fixed model: function 0's window holds the card common control
 *         registers at 0x00 and each function's basic registers at
 *         function_number * 0x100 (which is where the SDIO specification puts
 *         them, so the engine's own addressing is exercised rather than
 *         assumed), and every function's window holds a seeded data pattern. The
 *         register values are the specification's field values, not values read
 *         back from anything under test. Faults are injected one at a time, and
 *         every request is captured, so a case asserts the operation the engine
 *         asked for.
 */

#include <string.h>

#include "linuxkpi_core.h"

#include "mock_sdio_card.h"

#define	MOCK_SDIO_FUNCS		(LKPI_SDIO_BE_MAX_FUNCS + 1)

/* CCCR register offsets (function 0's window) */
#define	MOCK_CCCR_REVISION	0x00
#define	MOCK_CCCR_IOEX		0x02
#define	MOCK_CCCR_IORX		0x03
#define	MOCK_CCCR_IENX		0x04
#define	MOCK_CCCR_INTX		0x05
#define	MOCK_CCCR_BUS_IFACE	0x07
#define	MOCK_CCCR_CAPS		0x08

/* function basic register offsets, relative to function_number * 0x100 */
#define	MOCK_FBR_STD_IF		0x00
#define	MOCK_FBR_CIS		0x09
#define	MOCK_FBR_BLKSIZE	0x10

/* the capture depth: enough for a case's whole operation, small enough to scan */
#define	MOCK_SDIO_CAPTURE_MAX	512

static u8 mock_mem[MOCK_SDIO_FUNCS][MOCK_SDIO_MEM_SIZE];
static unsigned int mock_blk[MOCK_SDIO_FUNCS];
static u8 mock_pending;
static enum mock_sdio_fault mock_fault;
static unsigned int mock_direct_calls;
static unsigned int mock_extended_calls;

static struct mock_sdio_req mock_capture[MOCK_SDIO_CAPTURE_MAX];
static unsigned int mock_capture_n;

/* --- the model ------------------------------------------------------------ */

static unsigned int
mock_mask(unsigned int addr)
{

	return addr & (MOCK_SDIO_MEM_SIZE - 1u);
}

static void
mock_capture_add(unsigned int func, unsigned int addr, unsigned int bytes,
    unsigned int flags, int direct, int extended, int blk_size, int write,
    int block, int incr)
{
	struct mock_sdio_req *r;

	if (mock_capture_n >= MOCK_SDIO_CAPTURE_MAX)
		return;
	r = &mock_capture[mock_capture_n++];
	r->func = func;
	r->addr = addr;
	r->bytes = bytes;
	r->flags = flags;
	r->direct = direct;
	r->extended = extended;
	r->block_size = blk_size;
	r->write = write;
	r->block = block;
	r->incr = incr;
}

void
mock_sdio_card_reset(void)
{
	unsigned int f;
	unsigned int i;

	(void)memset(mock_mem, 0, sizeof(mock_mem));
	(void)memset(mock_blk, 0, sizeof(mock_blk));
	mock_pending = 0;
	mock_fault = MOCK_SDIO_FAULT_NONE;
	mock_direct_calls = 0;
	mock_extended_calls = 0;
	mock_capture_n = 0;

	/* the card's common control registers */
	mock_mem[0][MOCK_CCCR_REVISION] = 0x02;	/* CCCR/SDIO revision 2.00 */
	mock_mem[0][MOCK_CCCR_IOEX] = 0x00;	/* nothing enabled yet */
	mock_mem[0][MOCK_CCCR_IORX] = 0x02;	/* function 1 ready once enabled */
	mock_mem[0][MOCK_CCCR_IENX] = 0x00;
	mock_mem[0][MOCK_CCCR_INTX] = 0x00;
	mock_mem[0][MOCK_CCCR_BUS_IFACE] = 0x02;	/* four data lines */
	mock_mem[0][MOCK_CCCR_CAPS] = 0x02;	/* multi-block transfers */

	for (f = 1; f < MOCK_SDIO_FUNCS; f++) {
		unsigned int base = f * 0x100u;

		/* only function 1 names a standard interface (WLAN) */
		mock_mem[0][base + MOCK_FBR_STD_IF] = (f == 1u) ? 0x07u : 0x00u;
		/* a CIS pointer, three bytes little endian */
		mock_mem[0][base + MOCK_FBR_CIS + 0] = 0x00;
		mock_mem[0][base + MOCK_FBR_CIS + 1] = 0x10;
		mock_mem[0][base + MOCK_FBR_CIS + 2] = 0x00;
		/* the default block size, two bytes little endian */
		mock_mem[0][base + MOCK_FBR_BLKSIZE + 0] = 0x00;
		mock_mem[0][base + MOCK_FBR_BLKSIZE + 1] = 0x02;
		mock_blk[f] = 512u;

		for (i = 0; i < MOCK_SDIO_MEM_SIZE; i++)
			mock_mem[f][i] = (u8)(i + f);
	}
}

u8 *
mock_sdio_card_mem(unsigned int func, unsigned int addr)
{

	if (func >= MOCK_SDIO_FUNCS)
		return NULL;
	return &mock_mem[func][mock_mask(addr)];
}

void
mock_sdio_card_fault(enum mock_sdio_fault which)
{

	mock_fault = which;
}

void
mock_sdio_card_set_pending(u8 bits)
{

	mock_pending = bits;
}

void
mock_sdio_card_capture_clear(void)
{

	mock_capture_n = 0;
}

unsigned int
mock_sdio_card_capture_count(void)
{

	return mock_capture_n;
}

int
mock_sdio_card_capture_get(unsigned int i, struct mock_sdio_req *out)
{

	if (i >= mock_capture_n || out == NULL)
		return -1;
	*out = mock_capture[i];
	return 0;
}

unsigned int
mock_sdio_card_direct_calls(void)
{

	return mock_direct_calls;
}

unsigned int
mock_sdio_card_extended_calls(void)
{

	return mock_extended_calls;
}

unsigned int
mock_sdio_card_blk(unsigned int func)
{

	if (func >= MOCK_SDIO_FUNCS)
		return 0;
	return mock_blk[func];
}

/* --- the six seam operations ---------------------------------------------- */

static int
mock_card_init(void *ctx, struct lkpi_sdio_be_card_info *info)
{

	(void)ctx;
	if (mock_fault == MOCK_SDIO_FAULT_NO_CARD)
		return -LKPI_SDIO_BE_ERR_NOCARD;
	if (info == NULL)
		return -LKPI_SDIO_BE_ERR_INVAL;

	(void)memset(info, 0, sizeof(*info));
	info->vendor = 0x024c;
	info->device = 0xf179;
	info->fn0_max_block = 8;
	info->fn_max_block[0] = 512;
	info->func_count = 1;
	info->cccr_version = 0x02;
	info->sdio_version = 3;		/* SDIO 2.00 */
	info->ocr = 0x00ff8000u;
	info->clock_hz = 50000000u;
	info->host_max_blk_size = 512u;
	/* a deliberately small block count, so the engine's splitter has to split */
	info->host_max_blk_count = 4u;
	info->bus_width = 4;
	info->memory_present = false;
	info->high_speed = true;
	info->multi_block = true;
	return 0;
}

static void
mock_card_deinit(void *ctx)
{

	(void)ctx;
}

static int
mock_card_io_direct(void *ctx, unsigned int func, unsigned int addr, bool write,
    u8 *value)
{

	(void)ctx;
	if (func >= MOCK_SDIO_FUNCS || value == NULL)
		return -LKPI_SDIO_BE_ERR_INVAL;
	if (mock_fault == MOCK_SDIO_FAULT_DIRECT) {
		mock_fault = MOCK_SDIO_FAULT_NONE;
		return -LKPI_SDIO_BE_ERR_XFER;
	}

	mock_capture_add(func, addr, 1u, 0u, 1, 0, 0, write ? 1 : 0, 0, 0);
	mock_direct_calls++;

	if (write)
		mock_mem[func][mock_mask(addr)] = *value;
	else
		*value = (u8)(mock_mem[func][mock_mask(addr)] ^
		    (mock_fault == MOCK_SDIO_FAULT_LIE ? 0xffu : 0u));
	return 0;
}

static int
mock_card_io_extended(void *ctx, unsigned int func, unsigned int addr, void *buf,
    unsigned int count, unsigned int flags)
{
	bool write = (flags & LKPI_SDIO_BE_F_WRITE) != 0u;
	bool block = (flags & LKPI_SDIO_BE_F_BLOCK) != 0u;
	bool incr = (flags & LKPI_SDIO_BE_F_INCR) != 0u;
	unsigned int bytes;
	unsigned int i;
	u8 *p = buf;

	(void)ctx;
	if (func >= MOCK_SDIO_FUNCS || buf == NULL || count == 0u)
		return -LKPI_SDIO_BE_ERR_INVAL;
	if (block && mock_blk[func] == 0u)
		return -LKPI_SDIO_BE_ERR_BLKSIZE;
	if (mock_fault == MOCK_SDIO_FAULT_EXTENDED) {
		mock_fault = MOCK_SDIO_FAULT_NONE;
		return -LKPI_SDIO_BE_ERR_TIMEOUT;
	}

	bytes = block ? count * mock_blk[func] : count;
	mock_capture_add(func, addr, bytes, flags, 0, 1, 0, write ? 1 : 0,
	    block ? 1 : 0, incr ? 1 : 0);
	mock_extended_calls++;

	for (i = 0; i < bytes; i++) {
		unsigned int a = mock_mask(incr ? addr + i : addr);

		if (write)
			mock_mem[func][a] = p[i];
		else
			p[i] = (u8)(mock_mem[func][a] ^
			    (mock_fault == MOCK_SDIO_FAULT_LIE ? 0xffu : 0u));
	}
	return 0;
}

static int
mock_card_set_block_size(void *ctx, unsigned int func, unsigned int size)
{

	(void)ctx;
	if (func >= MOCK_SDIO_FUNCS || size == 0u || (size & 3u) != 0u)
		return -LKPI_SDIO_BE_ERR_INVAL;

	mock_blk[func] = size;
	mock_mem[0][func * 0x100u + MOCK_FBR_BLKSIZE + 0] = (u8)(size & 0xffu);
	mock_mem[0][func * 0x100u + MOCK_FBR_BLKSIZE + 1] = (u8)((size >> 8) & 0xffu);
	mock_capture_add(func, func * 0x100u + MOCK_FBR_BLKSIZE, size, 0u, 0, 0, 1,
	    1, 0, 0);
	return 0;
}

static int
mock_card_irq_pending(void *ctx, u8 *pending)
{

	(void)ctx;
	if (pending == NULL)
		return -LKPI_SDIO_BE_ERR_INVAL;
	*pending = mock_pending;
	return 0;
}

static const struct lkpi_sdio_be_ops mock_sdio_ops = {
	.card_init = mock_card_init,
	.card_deinit = mock_card_deinit,
	.io_direct = mock_card_io_direct,
	.io_extended = mock_card_io_extended,
	.set_block_size = mock_card_set_block_size,
	.irq_pending = mock_card_irq_pending,
};

void
mock_sdio_card_install(void)
{

	mock_sdio_card_reset();
	lkpi_sdio_be_register(&mock_sdio_ops, NULL);
}

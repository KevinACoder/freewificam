/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   gicv3_its.c
 * @brief  GICv3 Interrupt Translation Service driver (GIC-600, LPI delivery).
 *
 * The ITS turns a write to a doorbell address into a specific LPI on a
 * specific redistributor. That indirection is what lets a PCIe endpoint
 * raise an arbitrary MSI with nothing but a Memory Write, and it is the
 * reason a device does not need a dedicated interrupt line. M8/M9 (UVC)
 * sit on top of this.
 *
 * The non-negotiable rule here: EVERY CPU WRITE TO MEMORY THE ITS OR THE
 * REDISTRIBUTOR READS IS FOLLOWED BY AN EXPLICIT DCACHE FLUSH. Both fetch
 * over non-coherent ports. `dsb` orders stores, it does not write them back,
 * so ordering alone leaves the hardware looking at stale memory. The flush is
 * unconditional - shareability readback does not exempt a table from it, and
 * a revision that flushed only on a reported shareability demotion re-created
 * the silent-ITS failure: the command queue drains politely, every register
 * reads back exactly what was written, and no LPI is ever delivered.
 *
 * The driver-owned tables are static .bss buffers (identity-mapped Normal
 * RAM): the ITS reads physical DRAM over its non-coherent port, and PA == VA
 * under this project's identity map, so one address serves both sides.
 * Callers address LPIs by raw INTID (IRQ_LPI_INTID_FIRST + slot) rather than
 * through a kernel IRQ window, and gic_lpi_set_state takes the INTID
 * directly - the hook is wired in gicv3.c's IRQ_Enable/IRQ_Disable.
 *
 * The LPI priority byte is BOARD_IRQ_PRIORITY_API_CALL_RAW (0xb0). The level
 * decides whether a delivery handler may call a FromISR API, and 0xa0 sits
 * *above* the API-call level (smaller is more urgent in GIC terms), so the
 * PCIe MSI handlers - which wake a waiter through an event flag - would trip
 * the port's own priority assertion. 0xb0 is the level the GMAC and console
 * interrupt handlers already run at.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "arch_asm.h"
#include "board_conf.h"
#include "cache.h"
#include "gicv3_its.h"
#include "log.h"
#include "regs.h"

/* --- ITS registers (offsets from BOARD_ITS_BASE) -------------------------- */
#define GITS_CTLR		0x0000
#define GITS_TYPER		0x0008
#define GITS_CBASER		0x0080
#define GITS_CWRITER		0x0088
#define GITS_CREADR		0x0090
#define GITS_BASER(n)		(0x0100 + (n) * 8)
#define GITS_TRANSLATER		0x10040

#define GITS_CTLR_ENABLED	(1U << 0)
#define GITS_CTLR_QUIESCENT	(1U << 31)

#define GITS_TYPER_PTA		(1ULL << 19)

/* All ITS tables and the command queue are normal cached memory the ITS
 * reads over a non-coherent port: inner shareable, read and write
 * allocate, write back. */
#define ITS_CACHE_INNER_SHAREABLE 1U
#define ITS_CACHE_RAWAWB	7U

/* GITS_CBASER */
#define ITS_CBASER_SIZE_SHIFT	0	/* pages - 1, 4K pages */
#define ITS_CBASER_SHARE_SHIFT	10
#define ITS_CBASER_OUTER_SHIFT	53
#define ITS_CBASER_INNER_SHIFT	59
#define ITS_CBASER_VALID	(1ULL << 63)

/* GITS_BASER<n> */
#define ITS_BASER_SIZE_SHIFT	0	/* pages - 1 */
#define ITS_BASER_PGSZ_SHIFT	8	/* 0 = 4K pages, 1 = 16K */
#define ITS_BASER_SHARE_SHIFT	10
#define ITS_BASER_ENTSZ_SHIFT	48	/* bytes per entry, read-only */
#define ITS_BASER_OUTER_SHIFT	53
#define ITS_BASER_TYPE_SHIFT	56
#define ITS_BASER_INNER_SHIFT	59
#define ITS_BASER_VALID		(1ULL << 63)

/* GITS_BASER<n>.Type: the values a GIC-600 assigns on reset; the
 * collection table slot is fixed (BASER1 on this part). */
#define ITS_BASER_TYPE_DEVICE	1U
#define ITS_BASER_TYPE_COLLECTION 4U

/* GICR LPI accounting registers (RD_base frame of this core's
 * redistributor; all offsets relative to BOARD_GICR_BASE). */
#define GICR_CTLR		0x0000
#define GICR_TYPER		0x0008
#define GICR_PROPBASER		0x0070
#define GICR_PENDBASER		0x0078

#define ITS_GICR_CTLR_ENABLE_LPIS (1U << 0)
#define GICR_CTLR_RWP		(1U << 3)

#define ITS_PROP_IDBITS_SHIFT	0	/* INTID bits - 1 */
#define ITS_PROP_INNER_SHIFT	7
#define ITS_PROP_SHARE_SHIFT	10
#define ITS_PROP_OUTER_SHIFT	56

#define ITS_PEND_INNER_SHIFT	7
#define ITS_PEND_SHARE_SHIFT	10
#define ITS_PEND_OUTER_SHIFT	56
#define ITS_PEND_PTZ		(1ULL << 62)

/* LPI property table entry */
#define ITS_LPI_PROP_ENABLE	0x1
/* Priority byte for delivered LPIs, raw (left-justified). API-call level, not
 * the default driver level: a PCIe MSI handler runs here and wakes a waiter
 * through a FromISR API, which the kernel port asserts must not come from an
 * interrupt above that level. See the file header. */
#define ITS_LPI_PROP_PRIO	((uint8_t)BOARD_IRQ_PRIORITY_API_CALL_RAW)

/* Command queue entries are 32 bytes, four 64-bit words: word 0 carries
 * the opcode and the device id, word 1 the event id, the ITT size or the
 * target INTID, word 2 the collection id, the ITT or redistributor
 * address and the valid bit. */
#define ITS_CMD_INT		0x03
#define ITS_CMD_CLEAR		0x04	/* not 0x0d: 0x0d is INVALL */
#define ITS_CMD_SYNC		0x05
#define ITS_CMD_MAPD		0x08
#define ITS_CMD_MAPC		0x09
#define ITS_CMD_MAPTI		0x0a
#define ITS_CMD_INV		0x0c
#define ITS_CMD_DISCARD		0x0f

#define ITS_CMD_DEVID_SHIFT	32
#define ITS_CMD_PINTID_SHIFT	32
#define ITS_CMD_RDBASE_SHIFT	16
#define ITS_CMD_VALID		(1ULL << 63)

/* Bounded poll loops: a wedged ITS must disable itself instead of hanging
 * the boot. */
#define ITS_QUIESCENT_TRIES	1000000U
#define ITS_CMD_TRIES		10000000U
#define ITS_RWP_TRIES		1000000U

struct its_dev {
	uint32_t devid;
	uint8_t used;
	uint8_t itt[ITS_ITT_ENTRIES * 8U] __attribute__((aligned(256)));
};

static struct {
	int ready;
	unsigned int pta;	/* GITS_TYPER.PTA: RDBase encoding */
	uint64_t rd_target;	/* encoded redistributor for MAPC/SYNC */
	unsigned int cmd_tail;	/* next free command queue entry */
	unsigned int lpi_count;	/* high-water mark of the LPI slots */
	unsigned int dev_count;
	struct its_dev devs[ITS_MAX_DEVICES];
	struct {
		uint32_t devid;
		uint32_t eventid;
		uint8_t used;
	} lpi_map[ITS_LPI_QUANTITY];
} its;

/* Driver-owned ITS tables. The pending table is the only one with a
 * hardware alignment requirement: GICR_PENDBASER stores its address
 * shifted by 16 bits, so it must live 64K aligned. All of them are
 * identity-mapped Normal cacheable RAM (.bss), so the CPU address and
 * the address the ITS sees are the same number. */
static uint64_t its_cmdq[ITS_CMDQ_ENTRIES * 4U]
	__attribute__((aligned(4096)));
static uint64_t its_device_table[ITS_DEV_TABLE_ENTRIES]
	__attribute__((aligned(16384)));
static uint64_t its_coll_table[ITS_COLL_TABLE_ENTRIES]
	__attribute__((aligned(4096)));
static uint8_t its_lpi_prop[ITS_PROP_SIZE] __attribute__((aligned(4096)));
static uint8_t its_lpi_pend[ITS_PEND_SIZE] __attribute__((aligned(0x10000)));

/* --- diagnostics ----------------------------------------------------------- */

/* The toolchain's newlib carries the definition (every image links -lc); the
 * prototype is declared here because this file takes no stdio include. */
extern int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

void its_log(const char *fmt, ...)
{
	char buf[160];
	va_list ap;

	va_start(ap, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	board_log("%s", buf);
}

/* The console driver's polled putc turns '\n' into CRLF, so plain \n
 * endings are correct here. 64-bit values print as two hex halves: the
 * freestanding vsnprintf only promises 32-bit conversions. */
#define ITS_LOG_REG64(label, value) \
	its_log(label "%08x%08x\n", (uint32_t)((value) >> 32), \
		(uint32_t)(value))

/* --- primitives ------------------------------------------------------------ */

static void its_memset(void *dst, uint8_t value, size_t len)
{
	uint8_t *p = (uint8_t *)dst;

	while (len-- != 0U) {
		*p++ = value;
	}
}

static int its_wait_reg32(uintptr_t reg, uint32_t mask, uint32_t expect,
			  unsigned int tries)
{
	while (tries-- != 0U) {
		if ((reg_rd32(reg) & mask) == expect) {
			return 0;
		}
	}
	return -1;
}

static int its_wait_rwp(void)
{
	return its_wait_reg32(BOARD_GICR_BASE + GICR_CTLR, GICR_CTLR_RWP, 0U,
			      ITS_RWP_TRIES);
}

static int its_quiescent(void)
{
	uintptr_t ctlr = BOARD_ITS_BASE + GITS_CTLR;
	uint32_t val = reg_rd32(ctlr);

	if ((val & GITS_CTLR_ENABLED) != 0U) {
		reg_wr32(ctlr, val & ~(uint32_t)GITS_CTLR_ENABLED);
	}

	/* A wedged queue never reports quiescent; bail out instead of
	 * hanging the boot. */
	return its_wait_reg32(ctlr, GITS_CTLR_QUIESCENT, GITS_CTLR_QUIESCENT,
			      ITS_QUIESCENT_TRIES);
}

/* --- command queue --------------------------------------------------------- */

static int its_cmd_post(const uint64_t cmd[4])
{
	uint64_t *entry = &its_cmdq[its.cmd_tail * 4U];
	uint32_t next_offset;
	int ret;

	entry[0] = cmd[0];
	entry[1] = cmd[1];
	entry[2] = cmd[2];
	entry[3] = cmd[3];

	/* The ITS reads commands over its non-coherent port: the entry
	 * must reach memory before the writer pointer publishes it. */
	cache_dcache_flush((uintptr_t)entry, 4U * sizeof(uint64_t));
	ARCH_DSB();

	/* CWRITER points at the next free slot, i.e. one past the entry
	 * just written; that is what makes the entry consumable. Waiting
	 * for CREADR to catch this offset means the command has drained. */
	its.cmd_tail = (its.cmd_tail + 1U) % ITS_CMDQ_ENTRIES;
	next_offset = its.cmd_tail * 32U;
	reg_wr32(BOARD_ITS_BASE + GITS_CWRITER, next_offset);
	ARCH_DSB();

	ret = its_wait_reg32(BOARD_ITS_BASE + GITS_CREADR, 0xffffffffU,
			     next_offset, ITS_CMD_TRIES);
	if (ret != 0) {
		its_log("its: command %#llx timed out\n",
			(unsigned long long)cmd[0]);
	}

	return ret;
}

static int its_cmd_sync(void)
{
	/* SYNC targets a redistributor: with GITS_TYPER.PTA clear the
	 * RDbase field carries the processor number from GICR_TYPER.
	 * Unlike MAPD/MAPC there is no Valid bit in a SYNC. */
	const uint64_t cmd[4] = { ITS_CMD_SYNC, 0U,
		its.rd_target << ITS_CMD_RDBASE_SHIFT, 0U };

	return its_cmd_post(cmd);
}

static int its_cmd_mapd(struct its_dev *dev)
{
	/* Size encodes log2 of the ITT entry count minus one. The ITT
	 * address lives in the [47:8] field of word2; because the ITT is
	 * 256-byte aligned its low byte is zero, so the raw 64-bit value
	 * already places the address at the bit the ITS reads. */
	const uint64_t cmd[4] = {
		ITS_CMD_MAPD | ((uint64_t)dev->devid << ITS_CMD_DEVID_SHIFT),
		ITS_ITT_ENTRIES == 64 ? 5 : 0,
		((uint64_t)(uintptr_t)dev->itt) | ITS_CMD_VALID, 0U };
	int ret;

	its_memset(dev->itt, 0, sizeof(dev->itt));
	cache_dcache_flush((uintptr_t)dev->itt, sizeof(dev->itt));
	ARCH_DSB();

	ret = its_cmd_post(cmd);
	if (ret == 0) {
		ret = its_cmd_sync();
	}

	return ret;
}

static int its_cmd_mapc(void)
{
	/* word2: collection id (0), redistributor reference, Valid. */
	const uint64_t cmd[4] = { ITS_CMD_MAPC, 0U,
		(its.rd_target << ITS_CMD_RDBASE_SHIFT) | ITS_CMD_VALID, 0U };
	int ret;

	ret = its_cmd_post(cmd);
	if (ret == 0) {
		/* The collection mapping must be observable by later
		 * commands on the target redistributor before MAPTI can
		 * use it. */
		ret = its_cmd_sync();
	}

	return ret;
}

static int its_cmd_mapti(uint32_t devid, uint32_t eventid,
			 unsigned int lpi_slot)
{
	const uint64_t cmd[4] = {
		ITS_CMD_MAPTI | ((uint64_t)devid << ITS_CMD_DEVID_SHIFT),
		eventid |
			((uint64_t)(IRQ_LPI_INTID_FIRST + lpi_slot)
			 << ITS_CMD_PINTID_SHIFT),
		0U,	/* collection id 0 */
		0U };
	int ret;

	ret = its_cmd_post(cmd);
	if (ret == 0) {
		ret = its_cmd_sync();
	}

	return ret;
}

static int its_cmd_inv(uint32_t devid, uint32_t eventid)
{
	const uint64_t cmd[4] = {
		ITS_CMD_INV | ((uint64_t)devid << ITS_CMD_DEVID_SHIFT),
		eventid, 0U, 0U };
	int ret;

	ret = its_cmd_post(cmd);
	if (ret == 0) {
		ret = its_cmd_sync();
	}

	return ret;
}

/* --- bring-up --------------------------------------------------------------- */

static int its_lpi_tables_setup(void)
{
	uintptr_t gicr = BOARD_GICR_BASE;
	uint64_t reg;
	uint64_t id_bits;
	int ret;

	/* Program the redistributor LPI accounting for this CPU. The
	 * pending table is zero-filled through the PTZ bit and delivery
	 * starts disabled; individual LPIs are armed via the property
	 * table once they are mapped. */
	ret = its_wait_rwp();
	if (ret == 0) {
		reg_wr32(gicr + GICR_CTLR,
			 reg_rd32(gicr + GICR_CTLR) &
				 ~(uint32_t)ITS_GICR_CTLR_ENABLE_LPIS);
		ret = its_wait_rwp();
	}
	if (ret != 0) {
		return ret;
	}

	/* The tables are cached normal memory: stores must be flushed to
	 * memory before the ITS and redistributor observe them. */
	its_memset(its_lpi_prop, 0, ITS_PROP_SIZE);
	its_memset(its_lpi_pend, 0, ITS_PEND_SIZE);
	cache_dcache_flush((uintptr_t)its_lpi_prop, ITS_PROP_SIZE);
	cache_dcache_flush((uintptr_t)its_lpi_pend, ITS_PEND_SIZE);
	ARCH_DSB();

	/* PROPBASER.IDbits carries (number of LPI INTID bits - 1). The
	 * redistributor sizes its view of the property and pending tables
	 * from this field: this board reports a useless value in
	 * GICD_TYPER.IDbits, so program the field for the 64K LPIs the tables
	 * are sized for. The tables
	 * are cached normal memory with write-back attrs, so the
	 * cacheability fields keep the write-back the CPU uses. */
	id_bits = ITS_LPI_IDBITS_FIELD;
	its_log("its: PROPBASER.IDbits %u\n", (unsigned int)id_bits);

	reg = ((uint64_t)(uintptr_t)its_lpi_prop) |
	      (id_bits << ITS_PROP_IDBITS_SHIFT) |
	      ((uint64_t)ITS_CACHE_RAWAWB << ITS_PROP_INNER_SHIFT) |
	      ((uint64_t)ITS_CACHE_INNER_SHAREABLE << ITS_PROP_SHARE_SHIFT) |
	      ((uint64_t)ITS_CACHE_RAWAWB << ITS_PROP_OUTER_SHIFT);
	reg_wr64(gicr + GICR_PROPBASER, reg);

	reg = ((uint64_t)(uintptr_t)its_lpi_pend) |
	      ((uint64_t)ITS_CACHE_RAWAWB << ITS_PEND_INNER_SHIFT) |
	      ((uint64_t)ITS_CACHE_INNER_SHAREABLE << ITS_PEND_SHARE_SHIFT) |
	      ((uint64_t)ITS_CACHE_RAWAWB << ITS_PEND_OUTER_SHIFT) |
	      ITS_PEND_PTZ;
	reg_wr64(gicr + GICR_PENDBASER, reg);

	ret = its_wait_rwp();
	if (ret != 0) {
		return ret;
	}

	reg_wr32(gicr + GICR_CTLR,
		 reg_rd32(gicr + GICR_CTLR) | ITS_GICR_CTLR_ENABLE_LPIS);

	return its_wait_rwp();
}

static uint64_t its_baser_reg(const void *table, size_t size,
			      unsigned int type, uint64_t page_size)
{
	/* The device table must cover the full 16-bit DeviceID space the
	 * ITS reports, so it spans 512KB and wants 16K pages (the largest
	 * this GIC-600 accepts for it). The collection table is small and
	 * stays on 4K pages. */
	uint64_t pgsz;
	uint64_t pages = ((uint64_t)size + page_size - 1U) / page_size;

	pgsz = (page_size == 0x4000U) ? 1ULL : 0ULL; /* 1 = 16K, 0 = 4K */

	return ((pages - 1U) << ITS_BASER_SIZE_SHIFT) |
	       (pgsz << ITS_BASER_PGSZ_SHIFT) |
	       ((uint64_t)ITS_CACHE_RAWAWB << ITS_BASER_INNER_SHIFT) |
	       ((uint64_t)type << ITS_BASER_TYPE_SHIFT) |
	       ((uint64_t)ITS_CACHE_RAWAWB << ITS_BASER_OUTER_SHIFT) |
	       ((uint64_t)ITS_CACHE_INNER_SHAREABLE << ITS_BASER_SHARE_SHIFT) |
	       ((uint64_t)(uintptr_t)table) | ITS_BASER_VALID;
}

static int its_baser_setup(void)
{
	uint64_t reg;

	cache_dcache_flush((uintptr_t)its_device_table, sizeof(its_device_table));
	cache_dcache_flush((uintptr_t)its_coll_table, sizeof(its_coll_table));
	ARCH_DSB();

	reg = its_baser_reg(its_device_table, sizeof(its_device_table),
			    ITS_BASER_TYPE_DEVICE, 0x4000U);
	reg_wr64(BOARD_ITS_BASE + GITS_BASER(0), reg);
	ITS_LOG_REG64("its: BASER0 devices -> ", reg_rd64(BOARD_ITS_BASE +
							  GITS_BASER(0)));

	/* This ITS binds the collection table to BASER1: reset reports
	 * the collection type there and programming any other slot is
	 * silently dropped, which wedges the command queue. */
	reg = its_baser_reg(its_coll_table, sizeof(its_coll_table),
			    ITS_BASER_TYPE_COLLECTION, 0x1000U);
	reg_wr64(BOARD_ITS_BASE + GITS_BASER(1), reg);
	ITS_LOG_REG64("its: BASER1 collections -> ",
		      reg_rd64(BOARD_ITS_BASE + GITS_BASER(1)));

	return 0;
}

static int its_cmd_queue_setup(void)
{
	uint64_t reg;

	/* Size encodes the queue region in 4K pages minus one. */
	reg = ((((uint64_t)ITS_CMDQ_ENTRIES * 32U) / 0x1000U) - 1U)
		      << ITS_CBASER_SIZE_SHIFT |
	      ((uint64_t)ITS_CACHE_RAWAWB << ITS_CBASER_INNER_SHIFT) |
	      ((uint64_t)ITS_CACHE_RAWAWB << ITS_CBASER_OUTER_SHIFT) |
	      ((uint64_t)ITS_CACHE_INNER_SHAREABLE << ITS_CBASER_SHARE_SHIFT) |
	      ((uint64_t)(uintptr_t)its_cmdq) | ITS_CBASER_VALID;
	reg_wr64(BOARD_ITS_BASE + GITS_CBASER, reg);

	its.cmd_tail = 0U;
	reg_wr32(BOARD_ITS_BASE + GITS_CWRITER, 0U);

	return its_wait_reg32(BOARD_ITS_BASE + GITS_CREADR, 0xffffffffU, 0U,
			      ITS_CMD_TRIES);
}

/* --- public mapping API ----------------------------------------------------- */

uint64_t gic_its_trans_addr(void)
{
	return (uint64_t)(BOARD_ITS_BASE + GITS_TRANSLATER);
}

int gic_its_device_attach(uint32_t devid)
{
	struct its_dev *dev = NULL;
	unsigned int i;
	int ret;

	if (its.ready == 0) {
		return -1;
	}

	for (i = 0; i < its.dev_count; i++) {
		if (its.devs[i].devid == devid) {
			return 0;	/* already mapped */
		}
	}
	if (its.dev_count == ITS_MAX_DEVICES) {
		its_log("its: no device slot left for %#x\n", devid);
		return -1;
	}

	dev = &its.devs[its.dev_count];
	its_memset(dev, 0, sizeof(*dev));
	dev->devid = devid;

	ret = its_cmd_mapd(dev);
	if (ret == 0) {
		/* `used` is what the diagnostic readers (its_attached_devices,
		 * its_device_itt, and through them itsdump) key on; leaving it
		 * clear made a mapped device invisible to them. */
		dev->used = 1U;
		its.dev_count++;
	}

	return ret;
}

int gic_its_event_map(uint32_t devid, uint32_t eventid)
{
	unsigned int slot;

	if (its.ready == 0) {
		return -1;
	}

	/* Reuse the first freed slot, otherwise take the next one from
	 * the high-water mark. */
	for (slot = 0U; slot < its.lpi_count; slot++) {
		if (its.lpi_map[slot].used == 0U) {
			break;
		}
	}
	if (slot == ITS_LPI_QUANTITY) {
		its_log("its: LPI window exhausted\n");
		return -1;
	}
	if (slot == its.lpi_count) {
		its.lpi_count++;
	}

	if (its_cmd_mapti(devid, eventid, slot) != 0) {
		return -1;
	}

	its.lpi_map[slot].devid = devid;
	its.lpi_map[slot].eventid = eventid;
	its.lpi_map[slot].used = 1U;

	return (int)(IRQ_LPI_INTID_FIRST + slot);
}

void gic_its_event_unmap(uint32_t devid, uint32_t eventid)
{
	const uint64_t cmd[4] = {
		ITS_CMD_DISCARD | ((uint64_t)devid << ITS_CMD_DEVID_SHIFT),
		eventid, 0U, 0U };
	unsigned int slot;

	for (slot = 0U; slot < its.lpi_count; slot++) {
		if ((its.lpi_map[slot].used != 0U) &&
		    its.lpi_map[slot].devid == devid &&
		    its.lpi_map[slot].eventid == eventid) {
			its.lpi_map[slot].devid = 0U;
			its.lpi_map[slot].eventid = 0U;
			its.lpi_map[slot].used = 0U;
			break;
		}
	}

	(void)its_cmd_post(cmd);
}

int gic_its_send_int(uint32_t devid, uint32_t eventid)
{
	const uint64_t cmd[4] = {
		ITS_CMD_INT | ((uint64_t)devid << ITS_CMD_DEVID_SHIFT),
		eventid, 0U, 0U };

	/* Consuming the INT through CREADR is enough: the LPI write to
	 * the redistributor happens as the command executes. */
	return its_cmd_post(cmd);
}

int gic_its_send_clear(uint32_t devid, uint32_t eventid)
{
	const uint64_t cmd[4] = {
		ITS_CMD_CLEAR | ((uint64_t)devid << ITS_CMD_DEVID_SHIFT),
		eventid, 0U, 0U };

	return its_cmd_post(cmd);
}

/* Enable/disable delivery of one LPI: the property table byte is
 * CPU-written state the ITS fetches over a non-coherent port, so the
 * store must be flushed, and the ITS caches property entries, so an INV
 * is required before the change takes effect. */
void gic_lpi_set_state(uint32_t intid, int enable)
{
	uint32_t slot = intid - IRQ_LPI_INTID_FIRST;
	uint8_t *prop = &its_lpi_prop[slot];

	if ((its.ready == 0) || (slot >= its.lpi_count)) {
		return;
	}

	*prop = (enable != 0) ? (uint8_t)(ITS_LPI_PROP_ENABLE |
					  ITS_LPI_PROP_PRIO)
			      : 0U;
	cache_dcache_flush((uintptr_t)prop, 1U);
	ARCH_DSB();

	(void)its_cmd_inv(its.lpi_map[slot].devid, its.lpi_map[slot].eventid);
}

/* --- itsdump plumbing ------------------------------------------------------- */

uint64_t its_prop_table(void)
{
	return (uint64_t)(uintptr_t)its_lpi_prop;
}

uint64_t its_pend_table(void)
{
	return (uint64_t)(uintptr_t)its_lpi_pend;
}

uint64_t its_dev_table(void)
{
	return (uint64_t)(uintptr_t)its_device_table;
}

uint64_t its_device_itt(uint32_t devid)
{
	unsigned int i;

	for (i = 0; i < its.dev_count; i++) {
		if ((its.devs[i].used != 0U) && its.devs[i].devid == devid) {
			return (uint64_t)(uintptr_t)its.devs[i].itt;
		}
	}
	return 0U;
}

uint32_t its_attached_devices(uint32_t *out, uint32_t max)
{
	unsigned int i;
	uint32_t n = 0U;

	for (i = 0; (i < its.dev_count) && (n < max); i++) {
		if (its.devs[i].used != 0U) {
			out[n++] = its.devs[i].devid;
		}
	}
	return n;
}

/* --- init -------------------------------------------------------------------- */

int its_init(void)
{
	uint64_t typer;
	int ret;

	if (its.ready != 0) {
		return 0;
	}

	typer = reg_rd64(BOARD_ITS_BASE + GITS_TYPER);
	its.pta = ((typer & GITS_TYPER_PTA) != 0U) ? 1U : 0U;

	/* Encode the redistributor reference once: with PTA clear the
	 * commands want the processor number from GICR_TYPER bits
	 * [23:8], otherwise the 64K-aligned frame address. Assuming
	 * either form is a silent failure, so read it. */
	if (its.pta != 0U) {
		its.rd_target = (uint64_t)BOARD_GICR_BASE;
	} else {
		its.rd_target = ((uint64_t)reg_rd32(BOARD_GICR_BASE +
						     GICR_TYPER) >> 8) & 0xffffU;
	}

	its_log("its: typer %08x%08x pta %u rd_target %llx\n",
		(uint32_t)(typer >> 32), (uint32_t)typer, its.pta,
		(unsigned long long)its.rd_target);

	ret = its_quiescent();
	if (ret == 0) {
		ret = its_lpi_tables_setup();
	}
	if (ret == 0) {
		ret = its_baser_setup();
	}
	if (ret == 0) {
		ret = its_cmd_queue_setup();
	}
	if (ret != 0) {
		its_log("its: init failed (%d), LPI delivery stays disabled\n",
			ret);
		return -1;
	}

	reg_wr32(BOARD_ITS_BASE + GITS_CTLR,
		 reg_rd32(BOARD_ITS_BASE + GITS_CTLR) | GITS_CTLR_ENABLED);

	ret = its_cmd_mapc();
	if (ret != 0) {
		its_log("its: MAPC failed, LPI delivery stays disabled\n");
		return -1;
	}

	its.ready = 1;
	its_log("its: ready, lpi window %u..%u, doorbell %#llx\n",
		IRQ_LPI_INTID_FIRST,
		IRQ_LPI_INTID_FIRST + ITS_LPI_QUANTITY - 1U,
		(unsigned long long)gic_its_trans_addr());

	return 0;
}

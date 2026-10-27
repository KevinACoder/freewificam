/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   pcie_ecam.c
 * @brief  First-party ECAM PCIe bus driver (firefly family): probe-only
 *         enumeration of the firmware-trained tree, plus the sized config
 *         access and capability offsets the endpoint drivers need.
 *
 * WHAT THE ENUMERATION DOES AND REFUSES TO DO. The firmware (U-Boot) hands
 * the image a trained hierarchy: links up, bus numbers assigned, BARs
 * programmed, bus masters enabled (its `pci enum` output - the lab's
 * fixture - shows the whole tree). The walk records that tree READ-ONLY.
 * Nothing here renumbers a bus, re-sizes a BAR or toggles a command bit;
 * the write path below exists for endpoint drivers' own registers (see the
 * header's second discipline) and is never called by the enumeration.
 *
 * ECAM ADDRESSING (PCI Express Base Specification, Enhanced Configuration
 * Access Mechanism; Linux drivers/pci/ecam.c carries the same layout):
 *
 *   offset = bus << 20 | device << 15 | function << 12 | reg
 *
 * One function's config space is its own 4KiB page inside the window; the
 * window's geometry comes from the version's board_conf.h (firefly v1:
 * 0x40000000, 256MiB, bus/dev/fn 256/32/8). A function that does not
 * decode (empty slot) reads as all-ones on this root complex - the
 * 0xffff vendor id is the absent marker; an all-zero read is treated the
 * same (a slot that answers neither is not there).
 *
 * THE WALK (Linux drivers/pci/probe.c, pci_scan_child_bus_extend, minus the
 * assignment half): for every bus, for every device, function 0 answers or
 * the device is gone; header type bit 7 says whether the remaining
 * functions exist; a type-1 header is a bridge whose secondary bus number
 * (offset 0x19) is the next bus to walk. The recursion is bounded by the
 * visited-bus bitmap - a malformed secondary number that loops back to an
 * ancestor is dropped, not followed.
 *
 * BARS: recorded as the firmware assigned them. The flags dword keeps the
 * raw lower word (memory/io, 32/64-bit, prefetchable); a 64-bit BAR
 * consumes its successor register as the high half. Sizing is deliberately
 * NOT done: the write-all-ones sizing dance belongs to the round that
 * assigns, and doing it against a live assignment would disturb what the
 * image promised to keep.
 *
 * CAPABILITIES: the MSI/MSI-X offsets are recorded by walking the
 * capability list the function publishes (cap pointer at 0x34) - read-only,
 * bounded (the list is hardware-owned; a loop in it stops the walk, it
 * cannot hang it).
 */

#include <stdbool.h>
#include <stdint.h>

#include "board_conf.h"
#include "pcie_ecam.h"
#include "regs.h"
#include "log.h"

/* --- config register offsets (byte offsets inside a function's page) ------ */

#define REG_ID			0x00u	/* vendor | device */
#define REG_CMD_STATUS		0x04u	/* command (low 16) | status */
#define REG_RID_CLASS		0x08u	/* rev | prog-if | subclass | class */
#define REG_HDR_BIST		0x0cu	/* clsize | latency | header type (23:16) | bist */
#define REG_BAR0		0x10u	/* six BAR slots from here */
#define REG_BUS_NUMBERS		0x18u	/* type 1: primary | secondary | subordinate */
#define REG_CAP_PTR		0x34u	/* capability list head */

#define HDR_TYPE_BYTE_SHIFT	16u	/* the header-type byte lives at [23:16] of
					 * the dword at 0x0c - taking the low
					 * byte reads BIST instead (firefly v1
					 * board round 8: every bridge came out
					 * "endpoint" and the walk never
					 * recursed) */
#define HDR_TYPE_LAYOUT		0x7fu
#define HDR_TYPE_BRIDGE		0x01u
#define HDR_TYPE_MULTIFN	0x80u

/* BAR flag bits (the raw lower dword's low bits). */
#define BAR_FLAGS_IO		(1u << 0)
#define BAR_FLAGS_TYPE		(3u << 1)
#define BAR_FLAGS_TYPE_64	(2u << 1)
#define BAR_BASE_MASK_IO	0xfffffffcu
#define BAR_BASE_MASK_MEM	0xfffffff0u

/* Capability-list walk bound: a malformed list cannot hang the probe. */
#define CAP_WALK_MAX		48u

/* --- the discovered tree ---------------------------------------------------- */

/* 11 functions exist on this board (six root ports, five endpoints); 32
 * leaves room for a populated slot without a rebuild. */
#define PCIE_ECAM_MAX_FUNCS	32u

static struct pcie_ecam_func pcie_funcs[PCIE_ECAM_MAX_FUNCS];
static uint32_t pcie_func_count;

/* Visited-bus bitmap: the recursion guard. */
static uint8_t pcie_bus_visited[BOARD_PCIE_BUS_MAX / 8u];

/* --- config access ---------------------------------------------------------- */

static uintptr_t func_page(uint8_t bus, uint8_t dev, uint8_t fn)
{
	uint64_t offset = ((uint64_t)bus << 20) | ((uint64_t)dev << 15) |
			  ((uint64_t)fn << 12);

	return (uintptr_t)(BOARD_PCIE_ECAM_BASE + offset);
}

/* The one access discipline: 32-bit, naturally aligned, through the
 * Device-mapped window. The sized faces below compose every narrower access
 * from aligned dwords - a short ECAM access is not guaranteed to be a
 * single transaction on every root complex, and this driver refuses to
 * depend on one. A sub-dword WRITE is a read-modify-write of its dword. */
static uint32_t cfg_dword(uint8_t bus, uint8_t dev, uint8_t fn,
			  uint16_t where)
{
	return reg_rd32(func_page(bus, dev, fn) + (where & ~3u));
}

static void cfg_dword_write(uint8_t bus, uint8_t dev, uint8_t fn,
			    uint16_t where, uint32_t value)
{
	reg_wr32(func_page(bus, dev, fn) + (where & ~3u), value);
}

static uint16_t cfg_read16(uint8_t bus, uint8_t dev, uint8_t fn,
			   uint16_t reg)
{
	uint32_t dword = cfg_dword(bus, dev, fn, reg);

	return (uint16_t)((reg & 2u) != 0u ? (dword >> 16) : dword);
}

static bool func_answers(uint8_t bus, uint8_t dev, uint8_t fn)
{
	uint16_t vendor = cfg_read16(bus, dev, fn, REG_ID);

	return vendor != 0xffffu && vendor != 0x0000u;
}

int32_t pcie_ecam_cfg_read(uint8_t bus, uint8_t dev, uint8_t fn,
			   uint16_t where, uint32_t size, uint32_t *value)
{
	uint32_t dword;
	uint32_t shift;

	if (value == 0 || size == 0u || size > 4u ||
	    (where & (size - 1u)) != 0u) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}
	if (bus >= BOARD_PCIE_BUS_MAX || dev >= BOARD_PCIE_DEV_MAX ||
	    fn >= BOARD_PCIE_FN_MAX) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}

	dword = cfg_dword(bus, dev, fn, where);
	shift = (where & 3u) * 8u;
	*value = (dword >> shift) & (size == 4u ? 0xffffffffu
						: (1u << (size * 8u)) - 1u);

	return ARM_DRIVER_OK;
}

int32_t pcie_ecam_cfg_write(uint8_t bus, uint8_t dev, uint8_t fn,
			    uint16_t where, uint32_t size, uint32_t value)
{
	uint32_t dword;
	uint32_t mask;
	uint32_t shift;

	if (size == 0u || size > 4u || (where & (size - 1u)) != 0u) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}
	if (bus >= BOARD_PCIE_BUS_MAX || dev >= BOARD_PCIE_DEV_MAX ||
	    fn >= BOARD_PCIE_FN_MAX) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}

	/* A 4-byte write is the native transaction; anything narrower is a
	 * read-modify-write of its aligned dword. */
	if (size == 4u) {
		cfg_dword_write(bus, dev, fn, where, value);
		return ARM_DRIVER_OK;
	}

	mask = (1u << (size * 8u)) - 1u;
	shift = (where & 3u) * 8u;
	dword = cfg_dword(bus, dev, fn, where);
	dword = (dword & ~(mask << shift)) | ((value & mask) << shift);
	cfg_dword_write(bus, dev, fn, where, dword);

	return ARM_DRIVER_OK;
}

/* --- capability offsets ------------------------------------------------------ */

/* Walk the function's capability list and record the MSI/MSI-X offsets.
 * Read-only; bounded (CAP_WALK_MAX); a list that loops stops the walk. */
static void claim_caps(struct pcie_ecam_func *f)
{
	uint32_t ptr = cfg_dword(f->bus, f->dev, f->fn, REG_CAP_PTR) & 0xffu;
	unsigned int steps = 0u;

	f->msi_cap = 0u;
	f->msix_cap = 0u;

	while (ptr != 0u && (ptr & 3u) == 0u && steps++ < CAP_WALK_MAX) {
		uint32_t hdr = cfg_dword(f->bus, f->dev, f->fn, (uint16_t)ptr);
		uint8_t id = (uint8_t)(hdr & 0xffu);

		if (id == PCI_CAP_ID_MSI && f->msi_cap == 0u) {
			f->msi_cap = (uint16_t)ptr;
		} else if (id == PCI_CAP_ID_MSIX && f->msix_cap == 0u) {
			f->msix_cap = (uint16_t)ptr;
		}

		ptr = (hdr >> 8) & 0xffu;
	}
}

/* --- the walk ---------------------------------------------------------------- */

static bool bus_visited(uint8_t bus)
{
	return (pcie_bus_visited[bus / 8u] & (1u << (bus % 8u))) != 0u;
}

static void bus_mark(uint8_t bus)
{
	pcie_bus_visited[bus / 8u] |= (uint8_t)(1u << (bus % 8u));
}

/* Record one answering function: identity, class, command, header, the BARs
 * exactly as assigned, and the message-interrupt capability offsets.
 * Returns false when the table is full (the walk then stops - a tree that
 * outruns 32 functions is a firmware surprise the transcript's summary line
 * will show, not a reason to corrupt memory). */
static bool claim_func(uint8_t bus, uint8_t dev, uint8_t fn)
{
	struct pcie_ecam_func *f;
	uint32_t id = cfg_dword(bus, dev, fn, REG_ID);
	uint32_t cmd = cfg_dword(bus, dev, fn, REG_CMD_STATUS);
	uint32_t cls = cfg_dword(bus, dev, fn, REG_RID_CLASS);
	uint32_t hdr = cfg_dword(bus, dev, fn, REG_HDR_BIST);
	unsigned int i;
	bool prev_was_64 = false;

	if (pcie_func_count >= PCIE_ECAM_MAX_FUNCS) {
		return false;
	}
	f = &pcie_funcs[pcie_func_count++];

	f->bus = bus;
	f->dev = dev;
	f->fn = fn;
	f->vendor_id = (uint16_t)(id & 0xffffu);
	f->device_id = (uint16_t)(id >> 16);
	f->command = (uint16_t)(cmd & 0xffffu);
	f->class_code = cls >> 8;	/* rev byte drops off the top */
	f->header_type = (uint8_t)((hdr >> HDR_TYPE_BYTE_SHIFT) & 0xffu);
	f->is_bridge = (f->header_type & HDR_TYPE_LAYOUT) == HDR_TYPE_BRIDGE;
	f->secondary_bus = 0;

	if (f->is_bridge) {
		uint32_t numbers = cfg_dword(bus, dev, fn, REG_BUS_NUMBERS);

		f->secondary_bus = (uint8_t)((numbers >> 8) & 0xffu);
	}

	for (i = 0u; i < PCIE_ECAM_BARS; i++) {
		uint32_t lo = cfg_dword(bus, dev, fn,
					 (uint16_t)(REG_BAR0 + 4u * i));
		uint32_t hi = 0;
		struct pcie_ecam_bar *bar = &f->bars[i];

		bar->flags = lo;
		bar->is_64 = (lo & BAR_FLAGS_TYPE) == BAR_FLAGS_TYPE_64;

		/* A 64-bit BAR's successor slot is the high half, not a BAR:
		 * it is folded into this record and marked consumed by the
		 * prev_was_64 flag. The fold is why pcie_msix.c needs no
		 * second read of its own. */
		if (bar->is_64 && (i + 1u) < PCIE_ECAM_BARS) {
			hi = cfg_dword(bus, dev, fn,
					(uint16_t)(REG_BAR0 + 4u * (i + 1u)));
		}

		if ((lo & BAR_FLAGS_IO) != 0u) {
			bar->base = lo & BAR_BASE_MASK_IO;
		} else {
			bar->base = (uint64_t)(lo & BAR_BASE_MASK_MEM) |
				    ((uint64_t)hi << 32);
		}
		bar->present = bar->base != 0u || prev_was_64;
		prev_was_64 = bar->is_64;
	}

	claim_caps(f);

	return true;
}

static void scan_bus(uint8_t bus)
{
	uint8_t dev;

	if (bus >= BOARD_PCIE_BUS_MAX || bus_visited(bus)) {
		return;
	}
	bus_mark(bus);

	for (dev = 0u; dev < BOARD_PCIE_DEV_MAX; dev++) {
		uint8_t fn_count = 1u;
		uint8_t fn;

		if (!func_answers(bus, dev, 0u)) {
			continue;
		}
		if ((cfg_dword(bus, dev, 0u, REG_HDR_BIST) >>
		     HDR_TYPE_BYTE_SHIFT & HDR_TYPE_MULTIFN) != 0u) {
			fn_count = BOARD_PCIE_FN_MAX;
		}

		for (fn = 0u; fn < fn_count; fn++) {
			if (!func_answers(bus, dev, fn)) {
				continue;
			}
			if (!claim_func(bus, dev, fn)) {
				return;
			}
		}

		/* Bridges claim their secondary bus only through the
		 * function that owns it (function 0 on this tree). */
		{
			const struct pcie_ecam_func *claimed =
				&pcie_funcs[pcie_func_count - 1u];

			if (claimed->is_bridge && claimed->fn == 0u &&
			    claimed->secondary_bus != 0u) {
				scan_bus(claimed->secondary_bus);
			}
		}
	}
}

int32_t pcie_ecam_init(void)
{
	pcie_func_count = 0u;
	for (unsigned int i = 0u; i < sizeof(pcie_bus_visited); i++) {
		pcie_bus_visited[i] = 0u;
	}

	scan_bus(0u);

	LOG_INFO("pcie: ecam walk done funcs=%u (probe-only, firmware assignment kept)\n",
		 (unsigned)pcie_func_count);

	return (int32_t)pcie_func_count;
}

uint32_t pcie_ecam_dev_count(void)
{
	return pcie_func_count;
}

const struct pcie_ecam_func *pcie_ecam_dev(uint32_t index)
{
	if (index >= pcie_func_count) {
		return 0;
	}
	return &pcie_funcs[index];
}

const struct pcie_ecam_func *pcie_ecam_find_class(uint32_t from,
						  uint32_t class_code,
						  uint32_t mask)
{
	for (uint32_t i = from; i < pcie_func_count; i++) {
		if ((pcie_funcs[i].class_code & mask) ==
		    (class_code & mask)) {
			return &pcie_funcs[i];
		}
	}
	return 0;
}

uint32_t pcie_ecam_requester_id(const struct pcie_ecam_func *f)
{
	return ((uint32_t)f->bus << 8) | ((uint32_t)f->dev << 3) | f->fn;
}

void pcie_ecam_dump(void)
{
	LOG_INFO("pcie: ecam 0x%08x..0x%08x probe-only devs=%u\n",
		 (unsigned)BOARD_PCIE_ECAM_BASE,
		 (unsigned)(BOARD_PCIE_ECAM_BASE + BOARD_PCIE_ECAM_SIZE),
		 (unsigned)pcie_func_count);

	for (uint32_t i = 0u; i < pcie_func_count; i++) {
		const struct pcie_ecam_func *f = &pcie_funcs[i];

		LOG_INFO("pcie: %02x:%02x.%x %04x:%04x class %02x%02x %s%s\n",
			 f->bus, f->dev, f->fn, f->vendor_id, f->device_id,
			 (f->class_code >> 16) & 0xffu,
			 (f->class_code >> 8) & 0xffu,
			 f->is_bridge ? "bridge" : "endpoint",
			 (f->command & (1u << 2)) != 0u ? " bm" : "");

		if (!f->is_bridge) {
			for (unsigned int b = 0u; b < PCIE_ECAM_BARS; b++) {
				if (f->bars[b].present) {
					LOG_INFO("pcie:   bar%u 0x%08lx%s\n",
						 b,
						 (unsigned long)f->bars[b].base,
						 f->bars[b].is_64 ? " (64-bit)" : "");
				}
			}
		} else if (f->secondary_bus != 0u) {
			LOG_INFO("pcie:   sec bus %u\n", (unsigned)f->secondary_bus);
		}
	}
}

/*
 * @file   mmu.c
 * @brief  Identity-mapped MMU bring-up (the board-proven shape, minus
 *         kernel/ITS/PCIe extras).
 *
 * Runs at EL1 with the MMU and both caches OFF (start.S cleared SCTLR_EL1).
 * After this function: Normal cacheable memory for the image (unaligned
 * access must work once C library code runs) and Device-nGnRE for every
 * peripheral frame.
 *
 * Identity mapping is deliberate: physical equals virtual, so bare-metal
 * MMIO pointers stay valid and there is no translation layer to debug.
 *
 * Layout: 4KiB granule, 48-bit VA (T0SZ=16), walk starts at L0, so L0
 * entries are always table descriptors, L1 blocks are 1GiB and L2 blocks
 * 2MiB:
 *
 *   L1[0]  0x00000000-0x3fffffff -> L2: 2MiB blocks, Device below the image
 *          RAM base (BOARD_MMU_IMAGE_RAM_BASE), Normal above it; the one
 *          block carrying the non-cacheable DMA window is refined into 4KiB
 *          pages (l3_nocache, mmu_map_noncacheable) so only that window
 *          loses cacheability
 *   L1[1]  0x40000000-0x7fffffff -> 1GiB Normal block
 *   L1[2]  0x80000000-0xbfffffff -> 1GiB Normal block
 *   L1[3]  0xc0000000-0xffffffff -> L2: 2MiB blocks, Device everywhere
 *          (an ITS LPI window would return with that round; this image has
 *          no ITS)
 *   L1[15] 0x3c0000000-0x3ffffffff -> L2: the two DesignWare PCIe DBI frames
 *          (4MiB Device each, BOARD_MMU_PCIE_DBI*); the rest of the region
 *          stays unmapped and faults
 */

#include <stdint.h>

#include "board_conf.h"
#include "cache.h"
#include "arch_asm.h"
#include "mmu.h"

/* MAIR attribute indices, referenced from the table descriptors below. */
#define ATTR_IDX_NORMAL		0	/* MAIR byte 0: Normal write-back */
#define ATTR_IDX_DEVICE		1	/* MAIR byte 1: Device-nGnRE */
#define ATTR_IDX_NOCACHE	2	/* MAIR byte 2: Normal non-cacheable */

#define DESC_TABLE		(3ULL << 0)	/* points at a next-level table */
#define DESC_BLOCK		(1ULL << 0)	/* 1GiB (L1) or 2MiB (L2) block */
#define DESC_PAGE		(3ULL << 0)	/* 4KiB (L3) page */
#define DESC_AF			(1ULL << 10)	/* Access Flag: set, else first
						 * access faults */
#define DESC_SH_INNER		(3ULL << 8)	/* inner shareable */
#define DESC_SH_NONE		(0ULL << 8)	/* non-shareable: Device only */
#define DESC_ATTR_NORMAL	((uint64_t)ATTR_IDX_NORMAL << 2)
#define DESC_ATTR_DEVICE	((uint64_t)ATTR_IDX_DEVICE << 2)
#define DESC_ATTR_NOCACHE	((uint64_t)ATTR_IDX_NOCACHE << 2)
#define DESC_XN			(1ULL << 54)	/* execute-never */

#define PAGE_GRANULE		4096U
#define L2_BLOCK_SIZE		(2ULL * 1024 * 1024)
#define L1_BLOCK_SIZE		(1ULL * 1024 * 1024 * 1024)

/* Image RAM base (board_conf.h): below this is firmware and must stay Device
 * (never cached, never speculated, never executed). */
#define IMAGE_RAM_BASE		BOARD_MMU_IMAGE_RAM_BASE

/* TCR_EL1 geometry. T0SZ=16 gives 48-bit VA. IRGN0/ORGN0 = write-back
 * write-allocate, SH0 = inner shareable. The physical address size is the
 * board's (BOARD_MMU_TCR_PS): a larger IPS than the hardware supports is
 * UNPREDICTABLE. */
#define TCR_T0SZ		(16ULL)
#define TCR_IRGN0_WBWA		(3ULL << 8)
#define TCR_ORGN0_WBWA		(3ULL << 10)
#define TCR_SH0_INNER		(3ULL << 12)
#define TCR_PS_40BIT		BOARD_MMU_TCR_PS
#define TCR_EPD1_DISABLE	(1ULL << 23)	/* we only use TTBR0 */

/* Four tables: one per level of the walk.
 *
 * WALK GEOMETRY - getting this wrong makes the board go silent.
 *
 * 4KiB granule with T0SZ=16 is a 48-bit VA, which means a FOUR-level walk
 * starting at L0:
 *
 *   L0  bits [47:39]   only TABLE descriptors are legal
 *   L1  bits [38:30]   1GiB blocks allowed
 *   L2  bits [29:21]   2MiB blocks allowed
 *   L3  bits [20:12]   4KiB pages allowed
 *
 * Omitting L0 and placing 1GiB blocks in the L0 table makes every descriptor
 * invalid (L0 admits no blocks): the first instruction fetched after
 * enabling the MMU raises a translation fault and jumps to the fault vector,
 * which parks before the console exists. The image loads, runs, and prints
 * nothing at all - the characteristic symptom of a bad page table. */
static uint64_t l0_table[512] __attribute__((aligned(PAGE_GRANULE)));
static uint64_t l1_table[512] __attribute__((aligned(PAGE_GRANULE)));
static uint64_t l2_low[512]  __attribute__((aligned(PAGE_GRANULE)));
static uint64_t l2_top[512]  __attribute__((aligned(PAGE_GRANULE)));

/* Page table for L1[15] (0x3c0000000-0x3ffffffff): the two DesignWare PCIe DBI
 * frames sit in this otherwise-unmapped 1GiB region. Only the frames' own
 * 2MiB blocks are filled in as Device; every other entry stays zero, so an
 * access anywhere else in the region raises a translation fault rather than
 * reaching a peripheral by accident. */
static uint64_t l2_pcie[512] __attribute__((aligned(PAGE_GRANULE)));

/* Page table refining ONE 2MiB block of the image's RAM into 4KiB pages, so
 * the non-cacheable DMA window inside it can carry a different attribute
 * from its neighbours. Reserved statically (and only for this) because the
 * linker script caps the window at one block: a dynamically sized mapping
 * would need a page-table allocator that nothing else in this image wants.
 * The window's own extent comes from the linker (__noncacheable_start /
 * __noncacheable_end); an image without any non-cacheable data leaves this
 * table unused. */
static uint64_t l3_nocache[512] __attribute__((aligned(PAGE_GRANULE)));

extern char __noncacheable_start[];
extern char __noncacheable_end[];

/* Normal memory: inner-shareable, cacheable (MAIR byte 0). */
static uint64_t block_desc(uint64_t base, uint64_t attrs)
{
	return base | DESC_BLOCK | DESC_AF | DESC_SH_INNER | attrs;
}

/* Device memory: NON-shareable (MAIR byte 1), execute-never.
 *
 * The shareability field is not decoration. The ARM ARM states that Device
 * memory is Non-shareable, and that a shareable Device mapping is
 * UNPREDICTABLE. Cortex-A55 tolerates it often enough to boot, which is what
 * makes it dangerous: a peripheral register whose status read comes back
 * stale is indistinguishable from a peripheral that has stopped responding,
 * and a polled console that never sees its "transmit room" bit hangs the
 * whole system silently. Every peripheral frame below is mapped through
 * here. */
static uint64_t device_block_desc(uint64_t base)
{
	return base | DESC_BLOCK | DESC_AF | DESC_SH_NONE |
	       DESC_ATTR_DEVICE | DESC_XN;
}

/* Fill the 2MiB blocks of an L2 table that fall inside [start, end) with
 * Device block descriptors, leaving the rest of the table as it was. The table
 * is the one L1 entry covering the window's own 1GiB, derived from the
 * window's base, so a caller only hands over the window's own extent. The PCIe
 * DBI frames are the only windows mapped this way: their region is above 4GiB,
 * where the flat L1 map stops, and an unmapped entry there faults instead of
 * reaching the frame. */
static void mmu_map_device_window(uint64_t *l2, uint64_t start, uint64_t end)
{
	uint64_t base = start & ~(L1_BLOCK_SIZE - 1ULL);
	uint32_t i;

	for (i = 0; i < 512U; i++) {
		uint64_t block = base + ((uint64_t)i * L2_BLOCK_SIZE);

		if ((block >= start) && (block < end)) {
			l2[i] = device_block_desc(block);
		}
	}
}

/* Refine the 2MiB block holding the non-cacheable DMA window into 4KiB
 * pages: the window's pages take the non-cacheable attribute, every other
 * page of that block keeps the cacheable one, and the neighbouring blocks
 * keep their block descriptor. This sub-block precision is the reason the
 * window exists at all - a whole 2MiB block of image RAM marked
 * non-cacheable would slow down everything that shares it.
 *
 * Preconditions come from the linker script, which enforces them rather
 * than this code re-deriving them: the window is 2MiB aligned, it fits
 * inside one 2MiB block (the ASSERT in baremetal.ld), and it lies in the
 * image's RAM inside the low 1GiB (the RAM window top is far below 1GiB),
 * which is the L2 table this function refines. An image with no such data
 * skips the work entirely and every entry stays a block. */
static void mmu_map_noncacheable(void)
{
	uint64_t start = (uint64_t)(uintptr_t)__noncacheable_start;
	uint64_t end = (uint64_t)(uintptr_t)__noncacheable_end;
	uint64_t block;
	uint32_t i;

	if (end <= start) {
		return;
	}

	block = start & ~(L2_BLOCK_SIZE - 1ULL);

	for (i = 0U; i < 512U; i++) {
		uint64_t page = block + ((uint64_t)i * PAGE_GRANULE);

		if ((page >= start) && (page < end)) {
			l3_nocache[i] = page | DESC_PAGE | DESC_AF |
					DESC_SH_INNER | DESC_ATTR_NOCACHE;
		} else {
			l3_nocache[i] = page | DESC_PAGE | DESC_AF |
					DESC_SH_INNER | DESC_ATTR_NORMAL;
		}
	}

	l2_low[block / L2_BLOCK_SIZE] = DESC_TABLE |
					(uint64_t)(uintptr_t)l3_nocache;
}

void mmu_enable(void)
{
	uint64_t value;
	uint32_t i;

	/* L2 covering the low 1GiB, one 2MiB block per entry. */
	for (i = 0; i < 512U; i++) {
		uint64_t base = (uint64_t)i * L2_BLOCK_SIZE;

		if (base >= IMAGE_RAM_BASE) {
			l2_low[i] = block_desc(base, DESC_ATTR_NORMAL);
		} else {
			l2_low[i] = device_block_desc(base);
		}
	}

	/* Refine the one block that carries the non-cacheable DMA window (a
	 * no-op when the image has none). Runs after the block pass because
	 * it replaces one of the entries just written. */
	mmu_map_noncacheable();

	/* L2 covering the top 1GiB: the peripheral/Device window (GICD,
	 * GICR, UART2 all live in this 1GiB). */
	for (i = 0; i < 512U; i++) {
		uint64_t base = L1_BLOCK_SIZE * 3ULL + (uint64_t)i * L2_BLOCK_SIZE;

		l2_top[i] = device_block_desc(base);
	}

	/* L2 covering the PCIe DBI frames' 1GiB (L1[15]): the two frames are the
	 * only mapped part of that region, each a 4MiB Device window. Everything
	 * else in the table is left zero (unmapped, so it faults). The
	 * APB/CFG/MEM windows all sit in L1[3] above and need nothing here. */
	for (i = 0; i < 512U; i++) {
		l2_pcie[i] = 0;
	}
	mmu_map_device_window(l2_pcie, BOARD_MMU_PCIE_DBI0_BASE,
			      BOARD_MMU_PCIE_DBI0_END);
	mmu_map_device_window(l2_pcie, BOARD_MMU_PCIE_DBI1_BASE,
			      BOARD_MMU_PCIE_DBI1_END);

	/* L1 covering the low 4GiB. */
	for (i = 0; i < 512U; i++) {
		l1_table[i] = 0;
	}
	l1_table[0] = DESC_TABLE | (uint64_t)(uintptr_t)l2_low;
	l1_table[1] = block_desc(L1_BLOCK_SIZE,
				 DESC_ATTR_NORMAL);
	l1_table[2] = block_desc(L1_BLOCK_SIZE * 2ULL,
				 DESC_ATTR_NORMAL);
	l1_table[3] = DESC_TABLE | (uint64_t)(uintptr_t)l2_top;
	/* The PCIe DBI frames above 4GiB: L1[15] is the 1GiB region
	 * 0x3c0000000-0x3ffffffff, mapped only where the frames are. */
	l1_table[BOARD_MMU_PCIE_DBI0_BASE / L1_BLOCK_SIZE] =
		DESC_TABLE | (uint64_t)(uintptr_t)l2_pcie;

	/* L0: where the walk actually starts for a 48-bit VA. Only table
	 * descriptors are valid at this level. */
	for (i = 0; i < 512U; i++) {
		l0_table[i] = 0;
	}
	l0_table[0] = DESC_TABLE | (uint64_t)(uintptr_t)l1_table;

	/* MAIR: byte 0 Normal write-back read/write-allocate, byte 1
	 * Device-nGnRE, byte 2 Normal non-cacheable (the attribute the DMA
	 * window's pages are mapped with - ATTR_IDX_NOCACHE). Values match
	 * the 4-bit encodings in the ARM ARM. */
	value = 0xffULL | (0x04ULL << 8) | (0x44ULL << 16);
	__asm__ __volatile__("msr mair_el1, %0" : : "r"(value) : "memory");

	value = TCR_T0SZ | TCR_IRGN0_WBWA | TCR_ORGN0_WBWA |
		TCR_SH0_INNER | TCR_PS_40BIT | TCR_EPD1_DISABLE;
	__asm__ __volatile__("msr tcr_el1, %0" : : "r"(value) : "memory");

	/* TTBR0 points at L0, not L1: with T0SZ=16 the table walk base is the
	 * level-0 table. Pointing it one level short makes every access
	 * translate through invalid descriptors. */
	value = (uint64_t)(uintptr_t)l0_table;
	__asm__ __volatile__("msr ttbr0_el1, %0" : : "r"(value) : "memory");

	/* TTBR0 bits[2:0] must be zero for a 4KiB granule; the table is
	 * aligned, so this holds. A mismatch here faults on the first
	 * instruction fetch after enabling the MMU. */

	/* Last chance to drop the loader's dirty lines over our own memory:
	 * this runs with caching off, so nothing here is cached yet, and any
	 * line the loader left behind would otherwise be written back over
	 * the tables we just built (and the code, and the stack) once the
	 * caches come on. */
	cache_init();

	__asm__ __volatile__("dsb sy" ::: "memory");
	__asm__ __volatile__("isb" ::: "memory");

	/* Turn on MMU (M), data cache (C) and instruction cache (I). */
	ARCH_MRS(sctlr_el1, value);
	value |= (1ULL << 0) | (1ULL << 2) | (1ULL << 12);
	__asm__ __volatile__("msr sctlr_el1, %0" : : "r"(value) : "memory");
	__asm__ __volatile__("isb" ::: "memory");
}

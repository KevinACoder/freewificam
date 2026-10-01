/*
 * @file   mmu.c
 * @brief  Identity-mapped MMU bring-up ("same as before": the prior
 *         ThreadX carrier's proven shape, minus kernel/ITS/PCIe extras).
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
 *          RAM base (BOARD_MMU_IMAGE_RAM_BASE), Normal above it
 *   L1[1]  0x40000000-0x7fffffff -> 1GiB Normal block
 *   L1[2]  0x80000000-0xbfffffff -> 1GiB Normal block
 *   L1[3]  0xc0000000-0xffffffff -> L2: 2MiB blocks, Device everywhere
 *          (the prior image kept a Normal window here for ITS LPI tables;
 *          this milestone has no ITS - the window returns with that round)
 */

#include <stdint.h>

#include "board_conf.h"
#include "cache.h"
#include "mmu.h"

/* MAIR attribute indices, referenced from the table descriptors below. */
#define ATTR_IDX_NORMAL		0	/* MAIR byte 0: Normal write-back */
#define ATTR_IDX_DEVICE		1	/* MAIR byte 1: Device-nGnRE */
#define ATTR_IDX_NORMAL_NC	2	/* MAIR byte 2: Normal non-cacheable */

/* The DMA-descriptor window: one dedicated 2MiB block mapped Normal
 * non-cacheable, so DMA writebacks are visible to the CPU and CPU stores to
 * the DMA with no cache maintenance. The link script pins the .dma_noc
 * section to this address (baremetal.ld); the two must move together. */
#define DMA_NOC_BASE		0x0c000000ULL

#define DESC_TABLE		(3ULL << 0)	/* points at a next-level table */
#define DESC_BLOCK		(1ULL << 0)	/* 1GiB (L1) or 2MiB (L2) block */
#define DESC_AF			(1ULL << 10)	/* Access Flag: set, else first
						 * access faults */
#define DESC_SH_INNER		(3ULL << 8)	/* inner shareable */
#define DESC_SH_NONE		(0ULL << 8)	/* non-shareable: Device only */
#define DESC_ATTR_NORMAL	((uint64_t)ATTR_IDX_NORMAL << 2)
#define DESC_ATTR_DEVICE	((uint64_t)ATTR_IDX_DEVICE << 2)
#define DESC_ATTR_NORMAL_NC	((uint64_t)ATTR_IDX_NORMAL_NC << 2)
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
	/* Override the one block that hosts the DMA descriptor window
	 * (baremetal.ld pins .dma_noc inside it): non-cacheable, so descriptor
	 * writebacks cross the CPU/DMA boundary without cache maintenance -
	 * the coherent-memory equivalent the upstream Linux driver gets from
	 * dma_alloc_coherent. */
	l2_low[DMA_NOC_BASE / L2_BLOCK_SIZE] =
		block_desc(DMA_NOC_BASE, DESC_ATTR_NORMAL_NC);

	/* L2 covering the top 1GiB: the peripheral/Device window (GICD,
	 * GICR, UART2 all live in this 1GiB). */
	for (i = 0; i < 512U; i++) {
		uint64_t base = L1_BLOCK_SIZE * 3ULL + (uint64_t)i * L2_BLOCK_SIZE;

		l2_top[i] = device_block_desc(base);
	}

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

	/* L0: where the walk actually starts for a 48-bit VA. Only table
	 * descriptors are valid at this level. */
	for (i = 0; i < 512U; i++) {
		l0_table[i] = 0;
	}
	l0_table[0] = DESC_TABLE | (uint64_t)(uintptr_t)l1_table;

	/* MAIR: byte 0 Normal write-back read/write-allocate, byte 1
	 * Device-nGnRE, byte 2 Normal non-cacheable. Values match the 4-bit
	 * encodings in the ARM ARM. */
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
	__asm__ __volatile__("mrs %0, sctlr_el1" : "=r"(value));
	value |= (1ULL << 0) | (1ULL << 2) | (1ULL << 12);
	__asm__ __volatile__("msr sctlr_el1, %0" : : "r"(value) : "memory");
	__asm__ __volatile__("isb" ::: "memory");
}

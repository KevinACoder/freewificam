/*
 * @file   gicv3.c
 * @brief  GIC-600 (GICv3) bring-up + simplified IRQ registry.
 *
 * Two shapes here are specific to this image:
 *
 *  - diagnostics print through the polled console (uart_early_*);
 *  - the dispatch entry takes the acknowledged INTID as a parameter and
 *    never touches ICC_IAR1_EL1 / ICC_EOIR1_EL1: the acknowledge/end pair
 *    is exposed as gic_iar_read()/gic_irq_eoi() and owned by the exception
 *    entry that holds the interrupt frame (the FreeRTOS port writes EOIR1
 *    after vApplicationIRQHandler returns; the ThreadX entry runs the same
 *    read/dispatch/eoi trio in tx_vectors.S). Ending the interrupt here as
 *    well would EOI twice.
 *
 * Board-proven discipline (every one of these was established by experiment
 * on this board and getting it wrong produces silence, not an error):
 *
 *  1. GICD_CTLR is only ever read-modify-written, and only its EnableGrp1NS
 *     bit is ever set. Firmware hands over with DS=1 (single security state)
 *     and ARE set (measured 0x12). A plain store clears ARE, and once ARE is
 *     clear the system-register CPU interface stops working: every
 *     ICC_IAR1_EL1 read returns the spurious INTID 1023 while the interrupt
 *     lines keep asserting - an interrupt storm.
 *  2. The redistributor needs the WAKER handshake: clear ProcessorSleep,
 *     wait for ChildrenAsleep to clear. Skipping it leaves the redistributor
 *     asleep and PPIs/SPIs never arrive.
 *  3. Nothing is written to GICR_CTLR here (EnableLPIs and the LPI property/
 *     pending base registers belong to the ITS's own setup, arch/aarch64/
 *     gicv3_its.c) and the SGI/PPI bank gets no blanket
 *     writes - it covers all 32 SGIs and PPIs including firmware's secure
 *     lines (the EL1 secure physical timer PPI 29 is OP-TEE's, Group 0); a
 *     bank-wide rewrite is a non-secure write to secure state and RESETS
 *     this board. Per-INTID single-bit RMW only.
 *  4. ICC_IGRPEN1_EL1 is written conditionally (read first, set bit 0 only
 *     if clear). Skipping it means no Group 1 interrupt is ever delivered -
 *     distributor and redistributor read back correct, which is exactly what
 *     makes that failure mode quiet.
 *  5. Enable/disable outcomes are verified by behaviour (a handler counter),
 *     not readback: the group status registers read as zero on this silicon.
 *
 * Single core: this core's redistributor frame sits at BOARD_GICR_BASE.
 */

#include <stdbool.h>
#include <stdint.h>

#include "board_conf.h"
#include "gicv3.h"
#include "regs.h"
#include "uart.h"

/* --- distributor ---------------------------------------------------------- */
#define GICD_CTLR		0x0000
#define GICD_TYPER		0x0004
#define GICD_IGROUPR(n)		(0x0080 + 4 * (n))
#define GICD_ISENABLER(n)	(0x0100 + 4 * (n))
#define GICD_ICENABLER(n)	(0x0180 + 4 * (n))
#define GICD_ISPENDR(n)		(0x0200 + 4 * (n))
#define GICD_ICPENDR(n)		(0x0280 + 4 * (n))
/* Per-interrupt priority is ONE BYTE per INTID at 0x400 + id - no 4-byte
 * stride here. A 4x stride makes the default-priority loop walk into the
 * reserved region at 0x800+, and on this SoC a write to that hole does not
 * fault cleanly: the system resets. */
#define GICD_IPRIORITYR(id)	(0x0400 + (id))
#define GICD_ICFGR(n)		(0x0c00 + 4 * (n))
#define GICD_IGRPMODR(n)	(0x0d00 + 4 * (n))
#define GICD_IROUTER(n)		(0x6000 + 8 * (n))

#define GICD_CTLR_RWP		(1u << 31)
#define GICD_CTLR_ENABLEGRP1NS	(1u << 1)

/* --- redistributor (the boot core's frame) ---------------------------------
 *
 * A redistributor is two 64KiB frames: RD_base at offset 0 and the SGI_base
 * frame at +0x10000. ALL SGI/PPI register offsets below are relative to the
 * RD_base frame - the +0x10000 is folded in exactly once here. A second
 * pointer with the offset already folded in, added again, lands every
 * SGI/PPI access one frame too high: the timer pends, the enable write hits
 * unused space, and no interrupt EVER arrives - with every readback looking
 * correct. */
#define GICR_CTLR		0x0000
#define GICR_WAKER		0x0014
#define GICR_SGI_OFFSET		0x10000	/* start of the SGI_base frame */
#define GICR_IGROUPR0		(GICR_SGI_OFFSET + 0x0080)
#define GICR_ISENABLER0		(GICR_SGI_OFFSET + 0x0100)
#define GICR_ICENABLER0		(GICR_SGI_OFFSET + 0x0180)
#define GICR_ISPENDR0		(GICR_SGI_OFFSET + 0x0200)
#define GICR_ICPENDR0		(GICR_SGI_OFFSET + 0x0280)
#define GICR_IPRIORITYR0	(GICR_SGI_OFFSET + 0x0400)
#define GICR_IGRPMODR0		(GICR_SGI_OFFSET + 0x0d00)

#define GICR_CTLR_RWP		(1u << 3)
#define GICR_WAKER_PROCESSORSLEEP	(1u << 1)
#define GICR_WAKER_CHILDRENASLEEP	(1u << 2)

/* --- INTID ranges --------------------------------------------------------- */
#define IRQ_INTID_SGI_PPI_MAX	31u
#define IRQ_INTID_SPI_FIRST	32u
#define IRQ_INTID_SPI_MAX	1019u
#define IRQ_INTID_SPURIOUS	1023u

/* An INTID is 24 bits on this architecture. The acknowledged value must be
 * masked with this and NOT with the 10-bit SPI ceiling: an LPI's INTID starts
 * at 8192, so a 10-bit mask turns LPI 8192 into INTID 0 and the interrupt is
 * dispatched to nothing - silently. */
#define IRQ_INTID_MASK		0xffffffu

/* The registry covers SGI/PPI + the low SPIs this milestone will ever name
 * (UART 150 reserved); hardware paths below cover the full SPI range. */
#define IRQ_TABLE_SIZE		256u

/* LPIs are numbered from 8192, far outside the table above, so they get their
 * own table indexed by slot. It is still ONE registry - irq_register and
 * gic_irq_dispatch both route on the INTID - so a driver arms an LPI exactly as
 * it arms an SPI. Sized for the ITS's LPI window (ITS_LPI_QUANTITY) plus
 * headroom; an LPI outside it is rejected rather than aliased onto a slot. */
#define IRQ_LPI_TABLE_SIZE	64u

/* Bounded waits: a stuck RWP must surface as a visible failure, not an
 * unbounded hang. */
#define GIC_WAIT_LIMIT		50000000u

static volatile uint32_t *gicd;
static bool gic_ready;

static struct {
	irq_handler_t fn;
	void *arg;
} irq_table[IRQ_TABLE_SIZE];

static struct {
	irq_handler_t fn;
	void *arg;
} irq_lpi_table[IRQ_LPI_TABLE_SIZE];

static uint8_t irq_mode_level[IRQ_TABLE_SIZE];

/* --- register paths -------------------------------------------------------- */

static uint32_t gicd_read(uint32_t off)
{
	return reg_rd32((uintptr_t)gicd + off);
}

static void gicd_write(uint32_t off, uint32_t v)
{
	reg_wr32((uintptr_t)gicd + off, v);
}

/* The frame this core owns. BOARD_GICR_BASE names the first frame of the
 * bank; a core whose MPIDR affinity differs (a boot loader may hand over on
 * any of them) would otherwise arm its timer PPI in a redistributor that is
 * not its own - the line pends in someone else's frame and the interrupt is
 * never delivered, with every readback in the frame we wrote looking
 * correct. Resolved once from MPIDR by redistributor_select(). */
static uintptr_t gicr_base;

static uint32_t gicr_read(uint32_t off)
{
	return reg_rd32(gicr_base + off);
}

static void gicr_write(uint32_t off, uint32_t v)
{
	reg_wr32(gicr_base + off, v);
}

/* GICR_TYPER: the affinity value occupies bits 63:32, and bit 4 marks the
 * LAST frame in the bank - the walk stops there, so a core that is somehow
 * not represented falls back to the first frame instead of reading past the
 * bank. */
#define GICR_TYPER		0x0008
#define GICR_TYPER_LAST		(1ULL << 4)

static uint64_t mpidr_affinity(void)
{
	uint64_t mpidr;

	__asm__ __volatile__("mrs %0, mpidr_el1" : "=r"(mpidr));
	return mpidr & 0xff00ffffffULL;
}

/* Where an SPI is routed. The IROUTER encoding is Aff3.Aff2.Aff1.Aff0 in the
 * low 32 bits (bit 31 is the "IRM" any-core flag). The redistributor match
 * above needs the MT (multithreading) bit masked out, the router does not, so
 * the two use different views of the same register.
 *
 * A board that supplies BOARD_GIC_SPI_TARGET_AFFINITY names the core that runs
 * its images; otherwise the running core's own affinity is the safe answer. The
 * failing case this replaced was a hard 0: the line went to a core the firmware
 * never handed control to, stayed asserted unacknowledged, and showed up as a
 * fault at a PC in the firmware's own address space. */
static uint64_t spi_target_affinity(void)
{
#if defined(BOARD_GIC_SPI_TARGET_AFFINITY)
	return (uint64_t)BOARD_GIC_SPI_TARGET_AFFINITY;
#else
	uint64_t mpidr;

	__asm__ __volatile__("mrs %0, mpidr_el1" : "=r"(mpidr));
	return mpidr & 0xffff00ffffffULL;
#endif
}

static void redistributor_select(void)
{
	uint64_t want = mpidr_affinity();
	uintptr_t frame = (uintptr_t)BOARD_GICR_BASE;

	for (;;) {
		uint64_t typer = reg_rd64(frame + GICR_TYPER);

		if ((typer >> 32) == want) {
			break;
		}
		if ((typer & GICR_TYPER_LAST) != 0ULL) {
			/* Not found: keep the first frame. */
			frame = (uintptr_t)BOARD_GICR_BASE;
			break;
		}
		frame += (uintptr_t)BOARD_GICR_STRIDE;
	}

	gicr_base = frame;
	uart_early_puts("gicv3: rd frame off=");
	uart_early_put_hex32((uint32_t)(frame - (uintptr_t)BOARD_GICR_BASE));
	uart_early_puts("\n");
}

static void gicd_wait_rwp(void)
{
	uint32_t i;

	for (i = 0; i < GIC_WAIT_LIMIT; i++) {
		if ((gicd_read(GICD_CTLR) & GICD_CTLR_RWP) == 0u) {
			return;
		}
	}
	uart_early_puts("gicv3: distributor RWP stuck\n");
}

static void gicr_wait_rwp(void)
{
	uint32_t i;

	for (i = 0; i < GIC_WAIT_LIMIT; i++) {
		if ((gicr_read(GICR_CTLR) & GICR_CTLR_RWP) == 0u) {
			return;
		}
	}
	uart_early_puts("gicv3: redistributor RWP stuck\n");
}

/* ICC_SRE_EL1 (S3_0_C12_C12_5): system-register access must be enabled
 * before anything touches the ICC_* registers, and it must happen first. */
static void cpu_interface_sre_enable(void)
{
	uint32_t sre;

	__asm__ __volatile__("mrs %0, s3_0_c12_c12_5" : "=r"(sre));
	if ((sre & 0x1u) != 0u) {
		return;
	}

	sre |= 0x7u;	/* SRE | DFB | DIB */
	__asm__ __volatile__("msr s3_0_c12_c12_5, %0" ::"r"(sre));
	__asm__ __volatile__("isb" ::: "memory");

	sre = 0;
	__asm__ __volatile__("mrs %0, s3_0_c12_c12_5" : "=r"(sre));
	if ((sre & 0x1u) == 0u) {
		uart_early_puts("gicv3: ICC_SRE will not set\n");
	}
}

static void distributor_init(void)
{
	uint32_t ctlr;
	uint32_t typer;
	uint32_t lines;
	uint32_t i;

	/* Read-modify-write, only-set-if-needed: never touch ARE/DS. */
	ctlr = gicd_read(GICD_CTLR);
	if ((ctlr & GICD_CTLR_ENABLEGRP1NS) == 0u) {
		gicd_write(GICD_CTLR, ctlr | GICD_CTLR_ENABLEGRP1NS);
		gicd_wait_rwp();
	}

	typer = gicd_read(GICD_TYPER);
	lines = ((typer & 0x1fu) + 1u) * 32u;
	if (lines > (IRQ_INTID_SPI_MAX + 1u)) {
		lines = IRQ_INTID_SPI_MAX + 1u;
	}

	/* Every SPI: Group 1 non-secure, masked, level-triggered, routed to
	 * this core. Routing must be explicit - with ARE set, an SPI whose
	 * IROUTER is left at reset is never delivered. Nothing is ENABLED
	 * here; callers enable the lines they own. */
	for (i = IRQ_INTID_SPI_FIRST; i < lines; i++) {
		uint32_t word = i / 32u;
		uint32_t bit = i % 32u;

		gicd_write(GICD_IGROUPR(word),
			   gicd_read(GICD_IGROUPR(word)) | (1u << bit));
		gicd_write(GICD_IGRPMODR(word),
			   gicd_read(GICD_IGRPMODR(word)) & ~(1u << bit));
		gicd_write(GICD_ICENABLER(word), 1u << bit);
		gicd_write(GICD_ICPENDR(word), 1u << bit);

		/* Level-triggered: the ICFGR pair is 2 bits, 0 = level. */
		gicd_write(GICD_ICFGR(i / 16u),
			   gicd_read(GICD_ICFGR(i / 16u)) &
			   ~(3u << (2u * (i % 16u))));

		/* Route to a core that actually runs this image. Routing to 0
		 * sent every SPI to a core the firmware never handed control to:
		 * the line was asserted, nobody acknowledged it, and the failure
		 * surfaced as a fault at a PC in the firmware's own address
		 * space. The board names its target when it has one, otherwise
		 * the running core's own affinity is the safe answer. */
		reg_wr64((uintptr_t)gicd + GICD_IROUTER(i), spi_target_affinity());
	}
	gicd_wait_rwp();

	/* Default mid priority; callers override per interrupt. BYTE writes:
	 * see the IPRIORITYR note above. */
	for (i = IRQ_INTID_SPI_FIRST; i < lines; i++) {
		reg_wr8((uintptr_t)gicd + GICD_IPRIORITYR(i),
			BOARD_SPI_DEFAULT_PRIORITY_RAW);
	}
	gicd_wait_rwp();
}

static void redistributor_init(void)
{
	uint32_t waker;
	uint32_t i;

	/* Wake the redistributor: clear ProcessorSleep, wait for
	 * ChildrenAsleep. NOTHING ELSE touches this frame - see the file
	 * header for why a bank-wide SGI/PPI rewrite resets the board. */
	waker = gicr_read(GICR_WAKER);
	gicr_write(GICR_WAKER, waker & ~GICR_WAKER_PROCESSORSLEEP);

	for (i = 0; i < GIC_WAIT_LIMIT; i++) {
		if ((gicr_read(GICR_WAKER) & GICR_WAKER_CHILDRENASLEEP) == 0u) {
			break;
		}
	}
	if (i == GIC_WAIT_LIMIT) {
		uart_early_puts("gicv3: redistributor never woke\n");
	}

	gicr_wait_rwp();
}

static void cpu_interface_init(void)
{
	/* Accept every priority (the tick narrows its own line instead). */
	__asm__ __volatile__("msr s3_0_c4_c6_0, %0" ::"r"(0xffu));

	/* Enable Group 1 signalling at the CPU interface (ICC_IGRPEN1_EL1,
	 * S3_0_C12_C12_7) - conditionally. Not writing it means no Group 1
	 * interrupt is ever delivered while every readback looks fine. */
	{
		uint32_t grpen = 0;

		__asm__ __volatile__("mrs %0, s3_0_c12_c12_7" : "=r"(grpen));
		if ((grpen & 0x1u) == 0u) {
			grpen |= 0x1u;
			__asm__ __volatile__("msr s3_0_c12_c12_7, %0"
					     ::"r"(grpen));
			__asm__ __volatile__("isb" ::: "memory");
		}
	}
}

void gicv3_init(void)
{
	if (gic_ready) {
		return;
	}

	gicd = (volatile uint32_t *)(uintptr_t)BOARD_GICD_BASE;
	redistributor_select();

	/* Order matters: SRE first, then distributor, then redistributor +
	 * CPU interface (system-register access must exist before any ICC_*
	 * access, and before the interface is programmed at all). */
	cpu_interface_sre_enable();
	distributor_init();
	redistributor_init();
	cpu_interface_init();

	gic_ready = true;

	uart_early_puts("gicv3: ready ctlr=");
	uart_early_put_hex32(gicd_read(GICD_CTLR));
	uart_early_puts(" waker=");
	uart_early_put_hex32(gicr_read(GICR_WAKER));
	uart_early_puts("\n");
}

/* --- registry -------------------------------------------------------------- */

int32_t irq_register(uint32_t intid, irq_handler_t handler, void *arg)
{
	if (intid >= IRQ_LPI_INTID_FIRST) {
		uint32_t slot = intid - IRQ_LPI_INTID_FIRST;

		if (slot >= IRQ_LPI_TABLE_SIZE) {
			return -1;
		}
		irq_lpi_table[slot].fn = handler;
		irq_lpi_table[slot].arg = arg;
		return 0;
	}

	if (intid >= IRQ_TABLE_SIZE) {
		return -1;
	}
	irq_table[intid].fn = handler;
	irq_table[intid].arg = arg;
	return 0;
}

int32_t irq_enable(uint32_t intid)
{
	uint32_t word = intid / 32u;
	uint32_t bit = intid % 32u;

	if (intid > IRQ_INTID_SPI_MAX) {
		return -1;
	}

	if (intid <= IRQ_INTID_SGI_PPI_MAX) {
		/* Single-bit RMW of this one line - the bank also holds
		 * firmware's secure lines (see file header). IGRPMODR0 must be
		 * cleared along with IGROUPR0 being set: the pair selects the
		 * group, and IGROUPR0=1 with IGRPMODR0=1 is Group 1 SECURE,
		 * which a non-secure handler never receives - it looks correct
		 * in every readback and still delivers nothing. */
		gicr_write(GICR_IGROUPR0,
			   gicr_read(GICR_IGROUPR0) | (1u << bit));
		gicr_write(GICR_IGRPMODR0,
			   gicr_read(GICR_IGRPMODR0) & ~(1u << bit));
		gicr_write(GICR_ISENABLER0, 1u << bit);
	} else {
		gicd_write(GICD_ISENABLER(word), 1u << bit);
	}
	__asm__ __volatile__("dsb sy" ::: "memory");
	return 0;
}

int32_t irq_disable(uint32_t intid)
{
	uint32_t word = intid / 32u;
	uint32_t bit = intid % 32u;

	if (intid > IRQ_INTID_SPI_MAX) {
		return -1;
	}

	if (intid <= IRQ_INTID_SGI_PPI_MAX) {
		gicr_write(GICR_ICENABLER0, 1u << bit);
	} else {
		gicd_write(GICD_ICENABLER(word), 1u << bit);
	}
	__asm__ __volatile__("dsb sy" ::: "memory");
	return 0;
}

int32_t irq_clear_pending(uint32_t intid)
{
	uint32_t word = intid / 32u;
	uint32_t bit = intid % 32u;

	if (intid > IRQ_INTID_SPI_MAX) {
		return -1;
	}

	if (intid <= IRQ_INTID_SGI_PPI_MAX) {
		gicr_write(GICR_ICPENDR0, 1u << bit);
	} else {
		gicd_write(GICD_ICPENDR(word), 1u << bit);
	}
	__asm__ __volatile__("dsb sy" ::: "memory");
	return 0;
}

int32_t irq_set_priority(uint32_t intid, uint8_t raw_priority)
{
	if (intid > IRQ_INTID_SPI_MAX) {
		return -1;
	}

	if (intid <= IRQ_INTID_SGI_PPI_MAX) {
		reg_wr8(gicr_base + GICR_IPRIORITYR0 + intid, raw_priority);
	} else {
		reg_wr8((uintptr_t)gicd + GICD_IPRIORITYR(intid),
			raw_priority);
	}
	__asm__ __volatile__("dsb sy" ::: "memory");
	return 0;
}

int32_t irq_set_mode_level(uint32_t intid)
{
	if (intid >= IRQ_TABLE_SIZE) {
		return -1;
	}
	irq_mode_level[intid] = 1u;
	return 0;
}

/* --- read-only state queries (bring-up diagnostics) ------------------------ */

/* The running interrupt priority (ICC_RPR_EL1): 0xff while no interrupt is
 * being serviced. This is the A-profile answer to "am I inside an
 * interrupt?" - the CMSIS-RTOS2 adapter's IS_IRQ_MODE() probe asks exactly
 * that, and AArch64 has no IPSR to read. */
uint32_t gic_running_priority(void)
{
	uint32_t value;

	__asm__ __volatile__("mrs %0, s3_0_c12_c11_3" : "=r"(value));
	return value;
}

/* Pending (ISPENDR reads the live pending state; ICPENDR is the W1C write
 * side and would read back useless). Safe from any context. */
int32_t gic_irq_is_pending(uint32_t intid)
{
	uint32_t word = intid / 32u;
	uint32_t bit = intid % 32u;

	if (intid > IRQ_INTID_SPI_MAX) {
		return -1;
	}
	if (intid <= IRQ_INTID_SGI_PPI_MAX) {
		return (gicr_read(GICR_ISPENDR0) & (1u << bit)) ? 1 : 0;
	}
	return (gicd_read(GICD_ISPENDR(word)) & (1u << bit)) ? 1 : 0;
}

int32_t gic_irq_is_enabled(uint32_t intid)
{
	uint32_t word = intid / 32u;
	uint32_t bit = intid % 32u;

	if (intid > IRQ_INTID_SPI_MAX) {
		return -1;
	}
	if (intid <= IRQ_INTID_SGI_PPI_MAX) {
		return (gicr_read(GICR_ISENABLER0) & (1u << bit)) ? 1 : 0;
	}
	return (gicd_read(GICD_ISENABLER(word)) & (1u << bit)) ? 1 : 0;
}

/* --- interrupt entry -------------------------------------------------------- */

/* ICC_IAR1_EL1 (S3_0_C12_C12_0): acknowledge one Group 1 interrupt. Called
 * by the exception entry, which passes the returned value to the dispatcher
 * and later back into gic_irq_eoi(). Reading it outside an IRQ entry (e.g.
 * with nothing pending) returns the spurious INTID 1023 - the dispatcher
 * drops that. */
uint32_t gic_iar_read(void)
{
	uint32_t value;

	__asm__ __volatile__("mrs %0, s3_0_c12_c12_0" : "=r"(value));
	return value;
}

/* ICC_EOIR1_EL1 (S3_0_C12_C12_1): priority drop and deactivation in one
 * write. The value must be the one gic_iar_read() returned - writing
 * anything else deactivates the wrong state and wedges the CPU interface. */
void gic_irq_eoi(uint32_t iar)
{
	__asm__ __volatile__("msr s3_0_c12_c12_1, %0" :: "r"(iar) : "memory");
}

/* Dispatch one already-acknowledged interrupt. The INTID arrives as the
 * value read from ICC_IAR1_EL1 by the exception entry (the FreeRTOS port
 * reads it and passes it to vApplicationIRQHandler; the ThreadX entry calls
 * gic_iar_read() first), so this function never touches the acknowledge
 * register. It does NOT end the interrupt either: the entry owns EOIR1
 * (see the file header) and gets the value back through the return. */
uint32_t gic_irq_dispatch(uint32_t iar)
{
	uint32_t intid = iar & IRQ_INTID_MASK;
	irq_handler_t fn;
	void *arg;

	if (intid == IRQ_INTID_SPURIOUS) {
		return iar;
	}

	if (intid >= IRQ_LPI_INTID_FIRST) {
		uint32_t slot = intid - IRQ_LPI_INTID_FIRST;

		fn = (slot < IRQ_LPI_TABLE_SIZE) ? irq_lpi_table[slot].fn : 0;
		arg = (slot < IRQ_LPI_TABLE_SIZE) ? irq_lpi_table[slot].arg : 0;
	} else {
		fn = (intid < IRQ_TABLE_SIZE) ? irq_table[intid].fn : 0;
		arg = (intid < IRQ_TABLE_SIZE) ? irq_table[intid].arg : 0;
	}

	if (fn != 0) {
		fn(arg);
	}

	return iar;
}

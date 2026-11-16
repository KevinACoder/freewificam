/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * fmc_sdram.c - STM32F429 FMC SDRAM bring-up for the 32 MB W9825G6 on the
 *               挑战者 V2 core board.
 *
 * Facts (V2 core-board specification and the vendor example
 * A-3-FMC-扩展外部SDRAM/User/sdram/bsp_sdram.c):
 *   SDRAM bank 2 at 0xD0000000, 13 row bits, 9 column bits, 16-bit data,
 *   4 internal banks, CAS latency 3, SDCLK = HCLK/2 (FMC clock = HCLK; the
 *   image runs at 168 MHz, so SDCLK = 84 MHz).
 *
 * Two board facts are load-bearing:
 *   - the 128 MB NAND on the core board shares the FMC data bus.  Its control
 *     lines are driven to the deselected state before the SDRAM pins are
 *     claimed, or a floating NAND can drive D0-D15 under the SDRAM (the
 *     vendor example runs the same sequence as SDRAM_Enable_NAND_Disable());
 *   - the SDRAM is not touched by the startup code: its section is NOLOAD, so
 *     the .bss zeroing in Reset_Handler never writes to uninitialised SDRAM.
 *     main() calls f429_memory_boot() after the console is up and before the
 *     first FreeRTOS allocation.
 *
 * The self-test is bounded (three 64 KiB windows, uniqueness words, 8/16-bit
 * accesses).  The vendor example scans all 32 MB; that costs seconds and buys
 * nothing for a bring-up whose failure modes are address aliasing and pin
 * conflicts, both of which the windows catch.
 */

#include <stdint.h>

#include "board.h"
#include "uart.h"

#include "fmc_sdram.h"

/* --- RCC ------------------------------------------------------------------ */

#define RCC_BASE		0x40023800UL
#define RCC_AHB1ENR		(*(volatile uint32_t *)(RCC_BASE + 0x30UL))
#define RCC_AHB3ENR		(*(volatile uint32_t *)(RCC_BASE + 0x38UL))

#define RCC_AHB1ENR_GPIOB	(1UL << 1)
#define RCC_AHB1ENR_GPIOC	(1UL << 2)
#define RCC_AHB1ENR_GPIOD	(1UL << 3)
#define RCC_AHB1ENR_GPIOE	(1UL << 4)
#define RCC_AHB1ENR_GPIOF	(1UL << 5)
#define RCC_AHB1ENR_GPIOG	(1UL << 6)
#define RCC_AHB1ENR_GPIOH	(1UL << 7)
#define RCC_AHB3ENR_FMCEN	(1UL << 0)

/* --- GPIO (same register layout on every F4 GPIO port) -------------------- */

#define GPIOB_BASE		0x40020400UL
#define GPIOC_BASE		0x40020800UL
#define GPIOD_BASE		0x40020C00UL
#define GPIOE_BASE		0x40021000UL
#define GPIOF_BASE		0x40021400UL
#define GPIOG_BASE		0x40021800UL
#define GPIOH_BASE		0x40021C00UL

#define GPIO_MODER(base)	(*(volatile uint32_t *)((base) + 0x00UL))
#define GPIO_OSPEEDR(base)	(*(volatile uint32_t *)((base) + 0x08UL))
#define GPIO_PUPDR(base)	(*(volatile uint32_t *)((base) + 0x0CUL))
#define GPIO_BSRR(base)		(*(volatile uint32_t *)((base) + 0x18UL))
#define GPIO_AFR(base)		((volatile uint32_t *)((base) + 0x20UL))

#define GPIO_AF12		12UL

/* --- FMC SDRAM registers -------------------------------------------------- */

#define FMC_BASE		0xA0000000UL
#define FMC_SDCR1		(*(volatile uint32_t *)(FMC_BASE + 0x140UL))
#define FMC_SDCR2		(*(volatile uint32_t *)(FMC_BASE + 0x144UL))
#define FMC_SDTR1		(*(volatile uint32_t *)(FMC_BASE + 0x148UL))
#define FMC_SDTR2		(*(volatile uint32_t *)(FMC_BASE + 0x14CUL))
#define FMC_SDCMR		(*(volatile uint32_t *)(FMC_BASE + 0x150UL))
#define FMC_SDRTR		(*(volatile uint32_t *)(FMC_BASE + 0x154UL))
#define FMC_SDSR		(*(volatile uint32_t *)(FMC_BASE + 0x158UL))

#define FMC_SDSR_BUSY		(1UL << 0)
/* SDCMR command-target bits: CTB2 is bit 3, CTB1 is bit 4 (stm32f429xx.h
 * FMC_SDCMR_CTB2_Pos=3 / FMC_SDCMR_CTB1_Pos=4).  Getting this pair the other
 * way round sends every init command to the unconfigured bank 1; the clock
 * enable never reaches bank 2 and the first SDRAM access faults. */
#define FMC_SDCMR_CTB2		(1UL << 3)
#define FMC_SDCMR_NRFS_POS	5U
#define FMC_SDCMR_MRD_POS	9U

/* SDCMR MODE field values (RM0090 "SDRAM command mode"). */
#define SDCMR_CLK_ENABLE	1UL
#define SDCMR_PALL		2UL
#define SDCMR_AUTOREFRESH	3UL
#define SDCMR_LOAD_MODE		4UL

/*
 * Bank 2's registers are split, not duplicated (RM0090 37.7.3): the common
 * clock/read features live in bank 1's control register and the common TRP/
 * TRC timing in bank 1's timing register, while bank 2's own registers carry
 * the layout and the per-bank timings:
 *
 *   SDCR1: SDCLK = HCLK/2 (10), RBURST off, RPIPE = 1 (01)
 *   SDTR1: TRC = 6 (7 cycles), TRP = 1 (2 cycles)
 *   SDCR2: NC=9 (01), NR=13 (10), 16-bit (01), 4 banks (1), CAS=3 (11),
 *          write protect off
 *   SDTR2: TMRD=1 (2), TXSR=6 (7), TRAS=3 (4), TWR=1 (2), TRCD=1 (2)
 *
 * At 84 MHz (11.9 ns/cycle) that is RCD/RP 23.8 ns >= 18 ns, TRAS 47.6 ns
 * >= 42 ns, TRC/TXSR 83 ns >= 60/70 ns: the W9825G6's -6AC timings with
 * margin, and the vendor example's values at 90 MHz.
 */
/* SDCR1's SDCLK encoding is NOT the intuitive one: 00 disables the SDCLK
 * output, 10 gives HCLK/2 (RM0090 SDCR bit 11:10).  Getting this field wrong
 * leaves the SDRAM without a clock; the FMC then reports an AHB error on the
 * first bank-2 access, which reads as a precise BusFault. */
#define F429_SDCR1_VALUE	0x00002800UL
#define F429_SDTR1_VALUE	0x00106000UL
#define F429_SDCR2_VALUE	0x000001D9UL
#define F429_SDTR2_VALUE	0x01010361UL

/* SDRAM mode register: burst length 2, sequential, CAS 3, standard, single
 * write burst (the vendor example's SDRAM_MODEREG_* combination). */
#define F429_SDRAM_MR_VALUE	0x00000231UL

/* 64 ms / 8192 rows = 7.8125 us; the FMC wants that in SDCLK cycles minus 20. */
#define F429_SDRAM_REFRESH_DIV	128000UL
#define F429_SDRAM_REFRESH_BIAS	20UL

static void
sdram_delay(volatile uint32_t loops)
{
	while (loops-- != 0U)
		__asm__ __volatile__("nop");
}

/* Configure `pins` of one port for alternate function 12 (FMC), push-pull,
 * high speed, pull-up (the vendor example's SDRAM pin settings). */
static void
gpio_af12(uint32_t base, uint32_t pins)
{
	uint32_t pin;

	for (pin = 0U; pin < 16U; pin++) {
		uint32_t bit = 1UL << pin;
		uint32_t sh = pin * 2U;

		if ((pins & bit) == 0U)
			continue;

		GPIO_MODER(base) = (GPIO_MODER(base) & ~(3UL << sh)) |
				   (2UL << sh);
		GPIO_OSPEEDR(base) |= (3UL << sh);
		GPIO_PUPDR(base) = (GPIO_PUPDR(base) & ~(3UL << sh)) |
				   (1UL << sh);
		if (pin < 8U) {
			GPIO_AFR(base)[0] =
			    (GPIO_AFR(base)[0] & ~(0xFUL << (pin * 4U))) |
			    (GPIO_AF12 << (pin * 4U));
		} else {
			GPIO_AFR(base)[1] =
			    (GPIO_AFR(base)[1] &
			     ~(0xFUL << ((pin - 8U) * 4U))) |
			    (GPIO_AF12 << ((pin - 8U) * 4U));
		}
	}
}

/* Plain push-pull output at a fixed level (medium speed). */
static void
gpio_output(uint32_t base, uint32_t pin, int level)
{
	uint32_t bit = 1UL << pin;
	uint32_t sh = pin * 2U;

	GPIO_BSRR(base) = (level != 0) ? bit : (bit << 16);
	GPIO_MODER(base) = (GPIO_MODER(base) & ~(3UL << sh)) | (1UL << sh);
	GPIO_OSPEEDR(base) = (GPIO_OSPEEDR(base) & ~(3UL << sh)) | (2UL << sh);
	GPIO_PUPDR(base) &= ~(3UL << sh);
}

/*
 * Keep the core board's NAND off the shared FMC data bus.  The vendor example
 * (SDRAM_Enable_NAND_Disable) drives the same six lines: CE#/NOE#/NWE# high
 * and ALE/CLE low, so the NAND neither drives nor latches while the SDRAM
 * owns the bus.
 */
static void
nand_hold_off(void)
{
	RCC_AHB1ENR |= RCC_AHB1ENR_GPIOB | RCC_AHB1ENR_GPIOD |
		       RCC_AHB1ENR_GPIOG;
	(void)RCC_AHB1ENR;

	gpio_output(GPIOG_BASE, 9U, 1);		/* FMC_NCE2# deasserted */
	/*
	 * PB13 is NOT a NAND select on this board: it is WIFI_EN (the AP6181's
	 * enable, with R72 pulling it low).  The SDIO glue owns it and holds it
	 * low for the 8189FTV line; driving it high here would put the other
	 * card back on the shared SDIO bus.
	 */
	gpio_output(GPIOD_BASE, 4U, 1);		/* FMC_NOE# deasserted */
	gpio_output(GPIOD_BASE, 5U, 1);		/* FMC_NWE# deasserted */
	gpio_output(GPIOD_BASE, 11U, 0);	/* FMC_A16 / NAND ALE low */
	gpio_output(GPIOD_BASE, 12U, 0);	/* FMC_A17 / NAND CLE low */
}

/* Claim the SDRAM pins: the vendor example's pin map, all AF12. */
static void
sdram_pins(void)
{
	RCC_AHB1ENR |= RCC_AHB1ENR_GPIOC | RCC_AHB1ENR_GPIOD |
		       RCC_AHB1ENR_GPIOE | RCC_AHB1ENR_GPIOF |
		       RCC_AHB1ENR_GPIOG | RCC_AHB1ENR_GPIOH;
	(void)RCC_AHB1ENR;

	/* A0-A5, A6-A9, RAS */
	gpio_af12(GPIOF_BASE, 0x0000F83FUL);
	/* A10-A12, BA0-BA1, CLK (PG8), CAS (PG15); CS/CKE are PH6/PH7 */
	gpio_af12(GPIOG_BASE, 0x00008137UL);
	/* D0-D1, D13-D15 (D2-D3 via PD0/PD1) */
	gpio_af12(GPIOD_BASE, 0x0000C703UL);
	/* D4-D12, NBL0/NBL1 */
	gpio_af12(GPIOE_BASE, 0x0000FF83UL);
	/* NCE0 (bank 2 chip select), CKE0 */
	gpio_af12(GPIOH_BASE, 0x000000C0UL);
	/* SDNWE */
	gpio_af12(GPIOC_BASE, 0x00000001UL);
}

/* Bring-up instrument: one line per command, naming whether the command
 * engine moved (SDSR BUSY/MODES2) and what the command register kept.  These
 * lines are the SDRAM bring-up evidence for the command sequence; they stay until the
 * SDRAM init is stable. */
static void
sdram_dbg(const char *tag)
{
	uart_early_puts("f429: sdram cmd ");
	uart_early_puts(tag);
	uart_early_puts(" sdsr=");
	uart_early_put_hex32(FMC_SDSR);
	uart_early_puts(" sdcmr=");
	uart_early_put_hex32(FMC_SDCMR);
	uart_early_puts(" sdrtr=");
	uart_early_put_hex32(FMC_SDRTR);
	uart_early_puts("\n");
}

/* Issue one SDRAM command and wait for the controller to finish it. */
static int
sdram_command(uint32_t mode, uint32_t nrfs, uint32_t mrd)
{
	uint32_t spin = 2000000UL;

	FMC_SDCMR = (mode & 0x7UL) | FMC_SDCMR_CTB2 |
		    ((nrfs & 0xFUL) << FMC_SDCMR_NRFS_POS) |
		    ((mrd & 0x1FFFUL) << FMC_SDCMR_MRD_POS);

	while ((FMC_SDSR & FMC_SDSR_BUSY) != 0U) {
		if (--spin == 0U)
			return -1;
	}
	return 0;
}

int
f429_sdram_init(void)
{
	uint32_t sdclk;
	uint32_t count;

	nand_hold_off();

	RCC_AHB3ENR |= RCC_AHB3ENR_FMCEN;
	(void)RCC_AHB3ENR;
	if ((RCC_AHB3ENR & RCC_AHB3ENR_FMCEN) == 0U)
		return -5;

	sdram_pins();

	FMC_SDCR1 = F429_SDCR1_VALUE;
	FMC_SDTR1 = F429_SDTR1_VALUE;
	FMC_SDCR2 = F429_SDCR2_VALUE;
	FMC_SDTR2 = F429_SDTR2_VALUE;

	/* JEDEC sequence: clock enable, 100 us+, precharge all, 8 refreshes,
	 * then the mode register. */
	if (sdram_command(SDCMR_CLK_ENABLE, 1U, 0U) != 0)
		return -1;
	sdram_dbg("clk-enable");
	sdram_delay(400000UL);
	if (sdram_command(SDCMR_PALL, 1U, 0U) != 0)
		return -2;
	sdram_dbg("pall");
	if (sdram_command(SDCMR_AUTOREFRESH, 7U, 0U) != 0)
		return -3;
	sdram_dbg("refresh");
	if (sdram_command(SDCMR_LOAD_MODE, 1U, F429_SDRAM_MR_VALUE) != 0)
		return -4;
	sdram_dbg("load-mode");

	sdclk = (SystemCoreClock + 1U) / 2U;
	count = (sdclk / F429_SDRAM_REFRESH_DIV);
	count = (count > F429_SDRAM_REFRESH_BIAS) ?
		(count - F429_SDRAM_REFRESH_BIAS) : 1UL;
	if (count > 0x1FFFUL)
		count = 0x1FFFUL;
	FMC_SDRTR = count;
	sdram_dbg("sdrtr");

	return 0;
}

/*
 * Bounded self-test.  Returns the stage number on the first failure:
 *   1 uniqueness, 2 increment pattern, 3 byte/halfword access.
 * The failing 32-bit offset is printed by the caller's stage plus this
 * function's own line (kept to one line per failure so the 115200 console
 * stays short).
 */
static int
sdram_test(void)
{
	static const uint32_t windows[] = {
		0x00000000UL,	/* start */
		0x00800000UL,	/* 8 MiB */
		(F429_SDRAM_SIZE - 0x00010000UL),	/* last 64 KiB */
	};
	uint32_t want;
	uint32_t k;

	/* Address uniqueness: three words far apart, all written before any
	 * is read back, so an aliased address line shows up. */
	*(volatile uint32_t *)(F429_SDRAM_BASE + 0x00000000UL) = 0x11111111UL;
	*(volatile uint32_t *)(F429_SDRAM_BASE + 0x00100000UL) = 0x22222222UL;
	*(volatile uint32_t *)(F429_SDRAM_BASE + 0x01000000UL) = 0x33333333UL;
	if (*(volatile uint32_t *)(F429_SDRAM_BASE + 0x00000000UL) !=
		0x11111111UL ||
	    *(volatile uint32_t *)(F429_SDRAM_BASE + 0x00100000UL) !=
		0x22222222UL ||
	    *(volatile uint32_t *)(F429_SDRAM_BASE + 0x01000000UL) !=
		0x33333333UL)
		return 1;

	/* 64 KiB increment pattern in each window: catches stuck/aliased
	 * data and address lines, and a refresh rate that is too low only if
	 * the window is long enough to notice (it is not; the refresh count is
	 * checked by the later soak, not by this boot). */
	for (k = 0U; k < sizeof(windows) / sizeof(windows[0]); k++) {
		volatile uint32_t *p =
		    (volatile uint32_t *)(F429_SDRAM_BASE + windows[k]);
		uint32_t i;

		for (i = 0U; i < 16384U; i++)
			p[i] = 0xA5A50000UL + i;
		for (i = 0U; i < 16384U; i++) {
			want = 0xA5A50000UL + i;
			if (p[i] != want)
				return 2;
		}
	}

	/* 8- and 16-bit accesses: the FMC is configured 16-bit, so byte
	 * accesses are the path a mis-set MWID would break. */
	{
		volatile uint8_t *b = (volatile uint8_t *)F429_SDRAM_BASE;
		volatile uint16_t *h = (volatile uint16_t *)F429_SDRAM_BASE;

		b[0x2000U] = 0x5AU;
		b[0x2001U] = 0xA5U;
		if (b[0x2000U] != 0x5AU || b[0x2001U] != 0xA5U)
			return 3;
		h[0x1000U] = 0xC3D4U;
		if (h[0x1000U] != 0xC3D4U)
			return 3;
	}

	return 0;
}

int
f429_memory_boot(void)
{
	int rc = f429_sdram_init();

	if (rc != 0) {
		uart_early_puts("f429: sdram FAIL init rc=");
		uart_early_put_hex32((uint32_t)rc);
		uart_early_puts(" ahb3enr=");
		uart_early_put_hex32(RCC_AHB3ENR);
		uart_early_puts(" sdcr2=");
		uart_early_put_hex32(FMC_SDCR2);
		uart_early_puts(" sdtr2=");
		uart_early_put_hex32(FMC_SDTR2);
		uart_early_puts(" sdsr=");
		uart_early_put_hex32(FMC_SDSR);
		uart_early_puts("\n");
		return rc;
	}
	uart_early_puts("f429: sdram init ok\n");

	/* Register readback: the FMC clock, the bank-1/2 control and timing
	 * registers and the status at the moment the self-test starts.  These
	 * are the first fields to look at when the first SDRAM access faults
	 * (an unset AHB3ENR or an unprogrammed SDCR1/SDTR1 names a config
	 * problem, not a wiring one). */
	uart_early_puts("f429: sdram regs ahb3enr=");
	uart_early_put_hex32(RCC_AHB3ENR);
	uart_early_puts(" sdcr1=");
	uart_early_put_hex32(FMC_SDCR1);
	uart_early_puts(" sdtr1=");
	uart_early_put_hex32(FMC_SDTR1);
	uart_early_puts(" sdcr2=");
	uart_early_put_hex32(FMC_SDCR2);
	uart_early_puts(" sdtr2=");
	uart_early_put_hex32(FMC_SDTR2);
	uart_early_puts(" sdsr=");
	uart_early_put_hex32(FMC_SDSR);
	uart_early_puts(" sdrtr=");
	uart_early_put_hex32(FMC_SDRTR);
	uart_early_puts("\n");

	rc = sdram_test();
	if (rc != 0) {
		uart_early_puts("f429: sdram FAIL test stage=");
		uart_early_put_hex32((uint32_t)rc);
		uart_early_puts("\n");
		return rc;
	}

	uart_early_puts("f429: sdram test ok size=33554432\n");
	return 0;
}

/*
 * @file   cpu_boost.c
 * @brief  One-shot CPU cluster clock + core-voltage raise (N7 R5a).
 *
 * U-Boot leaves the little cluster at APLL 816 MHz (its CLK dump is the
 * state the RTOS inherits) and never touches vdd_cpu - the rail sits
 * wherever the DDR blobs parked it. Linux runs the same silicon to
 * 1.992 GHz by pairing every opp with its voltage (rk3568.dtsi cpu_opp_table:
 * 1.8 GHz <-> 1.15 V). This driver performs the same pairing once, as early
 * as main() allows:
 *
 *   1. No PMIC write: vdd_cpu is inherited at the buck1 table top (see
 *      the note in rk3568_cpu_boost).
 *   2. Core child dividers BEFORE the PLL moves (the up-scaling order of
 *      Linux rockchip_cpuclk): the row for the target rate from
 *      rk3568_cpuclk_rates - CLKSEL_CON(3) atclk/gic and (4) pclk/periph go
 *      to /8, CLKSEL_CON(5) aclk_core stays /2.
 *   3. APLL relock with the slow-mode dance (rockchip_rk3036_pll_set_params):
 *      MODE_CON0 -> slow, CON0/CON1 for 24 MHz * fbdiv (refdiv=1, postdiv
 *      1*1, integer mode), poll CON1 bit10 LOCK, MODE_CON0 -> normal.
 *
 * Every CRU register is hiword-write: bits [31:16] select which bits of
 * [15:0] change. Facts were cross-checked against three live PLLs dumped at
 * the U-Boot prompt (APLL 0x2044/0x1441 = 816 MHz, DPLL = 390 MHz, GPLL =
 * 1188 MHz - all decode exactly) - runs/20261002-lwip-gigabit R5 ledger.
 * i2c0 registers follow drivers/i2c/busses/i2c-rk3x.c; the bootloader's
 * CLKDIV (0x3e003e) is reused, the PMU CRU gates are all open (board dump).
 *
 * @author zhugengyu
 * @date   02.10.2026
 */

#include <stdint.h>
#include <stddef.h>

#include "log.h"
#include "mmio.h"

#include "uart.h"

#include "cpu_boost.h"

/* --- blocks --------------------------------------------------------------- */

#define CRU_BASE			0xfdd20000u
#define PMUCRU_BASE			0xfdd00000u
#define I2C0_BASE			0xfdd40000u

#define CRU_APLL_CON0			(CRU_BASE + 0x00u)
#define CRU_APLL_CON1			(CRU_BASE + 0x04u)
#define CRU_MODE_CON0			(CRU_BASE + 0x90u)
#define CRU_CLKSEL_CON(n)		(CRU_BASE + 0x100u + (n) * 4u)

/* The PMU CRU is its own compact layout (drivers/clk/rockchip/clk.h):
 * gates at +0x180, NOT the main CRU's +0x340 - reading 0x344 says nothing
 * about the i2c0 clocks. GATE_CON(1) bit0 = pclk_i2c0, bit1 = clk_i2c0
 * (hiword-write, 0 = ungate). */
#define PMUCRU_GATE_CON(n)		(PMUCRU_BASE + 0x180u + (n) * 4u)

/* MODE_CON0[0]: 0 = normal (PLL), 1 = slow (xin24m). */
#define APLL_MODE_SLOW			1u
#define APLL_MODE_NORM			0u

/* PLL CON fields (rk3036/3328 layout; all rows here run refdiv=1,
 * postdiv2=1, integer mode - VCO = 24 MHz * fbdiv, out = VCO / postdiv1.
 * refdiv=0 or postdiv2=0 is an ILLEGAL divide-by-zero configuration: the
 * PLL never locks and (board-observed) returning MODE_CON to normal on a
 * dead PLL wedges the core stone silent. The 816 MHz live config is
 * CON0=0x2044 (fbdiv 68, pd1 2) / CON1=0x1441. */
#define APLL_CON0(fbdiv, pd1) \
	((0x7fffu << 16) | (((pd1) & 0x7u) << 12) | ((fbdiv) & 0xfffu))
#define APLL_CON1(dsmpd, pd2, refdiv) \
	((0x173fu << 16) | (((dsmpd) & 0x1u) << 12) | \
	 (((pd2) & 0x7u) << 6) | ((refdiv) & 0x3fu))
#define APLL_CON1_LOCK			(1u << 10)

/* Core child dividers, 5-bit fields [4:0] and [12:8] (value = div - 1). */
#define CLKSEL_FIELDS(lo, hi)	((0x1f1fu << 16) | ((hi) << 8) | (lo))

/* --- i2c0 (rk3x controller, polled) --------------------------------------- *
 * The sequence is the bootloader's own polled recipe (u-boot rk_i2c.c),
 * board-proven against this PMIC: every phase arms IEN FIRST (an IPD bit
 * does not latch without its IEN gate - cost one debug round), CON is a
 * plain peripheral register (NOT hiword), and the mode field is
 * TX=0/REGISTER_TX=1/RX=2 << 1. */

#define I2C_REG_CON			(I2C0_BASE + 0x00u)
#define I2C_REG_MTXCNT			(I2C0_BASE + 0x10u)
#define I2C_REG_MRXCNT			(I2C0_BASE + 0x14u)
#define I2C_REG_IEN			(I2C0_BASE + 0x18u)
#define I2C_REG_IPD			(I2C0_BASE + 0x1cu)
#define I2C_TXBUFFER0			(I2C0_BASE + 0x100u)
#define I2C_RXBUFFER0			(I2C0_BASE + 0x200u)

#define I2C_CON_EN			(1u << 0)
#define I2C_CON_MOD_TX			(0u << 1)
#define I2C_CON_MOD_RX			(2u << 1)
#define I2C_CON_START			(1u << 3)
#define I2C_CON_STOP			(1u << 4)
#define I2C_CON_LASTACK			(1u << 5)

#define I2C_MBTFIPD			(1u << 2)
#define I2C_MBRFIPD			(1u << 3)
#define I2C_STARTIPD			(1u << 4)
#define I2C_STOPIPD			(1u << 5)
#define I2C_NAKRCVIPD			(1u << 6)
#define I2C_IPD_ALL			0x7fu

#define I2C_STEP_SPINS			200000u

#define RK809_ADDR			0x20u
#define RK809_BUCK1_ON_VSEL		0xbbu
#define RK809_BUCK1_UV_BASE		712500u
#define RK809_BUCK1_UV_STEP		12500u

/* Arm the interrupt-enable gates BEFORE the event can fire: an IPD bit
 * does not latch unless its IEN bit was already set when the event
 * happened (two board rounds taught this the hard way - arming after the
 * trigger write loses the event every time). */
__attribute__((unused)) static void i2c0_arm(uint32_t ien)
{
	mmio_wr32(I2C_REG_IPD, I2C_IPD_ALL);
	mmio_wr32(I2C_REG_IEN, ien);
}

__attribute__((unused)) static int i2c0_poll(uint32_t ipd_bit)
{
	unsigned int t;

	for (t = 0; t < I2C_STEP_SPINS; t++) {
		uint32_t ipd = mmio_rd32(I2C_REG_IPD);

		if (ipd & I2C_NAKRCVIPD) {
			return -1;	/* device did not ack */
		}
		if (ipd & ipd_bit) {
			return 0;
		}
	}
	return -1;
}

__attribute__((unused)) static void i2c0_stop(void)
{
	i2c0_arm(I2C_STOPIPD);
	mmio_wr32(I2C_REG_CON, I2C_CON_EN | I2C_CON_STOP);
	(void)i2c0_poll(I2C_STOPIPD);
	mmio_wr32(I2C_REG_IPD, I2C_IPD_ALL);
	mmio_wr32(I2C_REG_CON, 0);	/* disable: the bootloader shape */
}

/* Single-shot write of up to 4 bytes: [addr<<1 | W] [b1] [b2] ... */
__attribute__((unused)) static int i2c0_write(uint32_t payload, unsigned int cnt)
{
	if ((cnt == 0u) || (cnt > 4u)) {
		return -1;
	}

	/* START (armed first - see i2c0_arm) */
	i2c0_arm(I2C_STARTIPD);
	mmio_wr32(I2C_REG_CON, I2C_CON_EN | I2C_CON_MOD_TX | I2C_CON_START);
	if (i2c0_poll(I2C_STARTIPD) != 0) {
		goto fail;
	}
	mmio_wr32(I2C_REG_CON, I2C_CON_EN | I2C_CON_MOD_TX);

	/* DATA */
	mmio_wr32(I2C_TXBUFFER0, payload);
	i2c0_arm(I2C_MBTFIPD | I2C_NAKRCVIPD);
	mmio_wr32(I2C_REG_MTXCNT, cnt);
	if (i2c0_poll(I2C_MBTFIPD) != 0) {
		goto fail;
	}

	i2c0_stop();
	return 0;

fail:
	i2c0_stop();
	return -1;
}

/* One-byte register read via repeated start (probe diagnostic only). */
__attribute__((unused)) static int i2c0_read8(uint8_t reg, uint8_t *val)
{
	uint32_t payload = ((uint32_t)(RK809_ADDR << 1)) |
			   ((uint32_t)reg << 8);

	/* addr+reg write, no STOP (keeps the bus owned) */
	i2c0_arm(I2C_STARTIPD);
	mmio_wr32(I2C_REG_CON, I2C_CON_EN | I2C_CON_MOD_TX | I2C_CON_START);
	if (i2c0_poll(I2C_STARTIPD) != 0) {
		goto fail;
	}
	mmio_wr32(I2C_REG_CON, I2C_CON_EN | I2C_CON_MOD_TX);
	mmio_wr32(I2C_TXBUFFER0, payload);
	i2c0_arm(I2C_MBTFIPD | I2C_NAKRCVIPD);
	mmio_wr32(I2C_REG_MTXCNT, 2);
	if (i2c0_poll(I2C_MBTFIPD) != 0) {
		goto fail;
	}

	/* repeated START in RX mode, single byte with LASTACK */
	i2c0_arm(I2C_STARTIPD);
	mmio_wr32(I2C_REG_CON, I2C_CON_EN | I2C_CON_MOD_RX | I2C_CON_START);
	if (i2c0_poll(I2C_STARTIPD) != 0) {
		goto fail;
	}
	mmio_wr32(I2C_REG_CON, I2C_CON_EN | I2C_CON_MOD_RX | I2C_CON_LASTACK);
	i2c0_arm(I2C_MBRFIPD | I2C_NAKRCVIPD);
	mmio_wr32(I2C_REG_MRXCNT, 1);
	if (i2c0_poll(I2C_MBRFIPD) != 0) {
		goto fail;
	}
	*val = (uint8_t)(mmio_rd32(I2C_RXBUFFER0) & 0xffu);
	mmio_wr32(I2C_REG_IPD, I2C_IPD_ALL);

	i2c0_stop();
	return 0;

fail:
	i2c0_stop();
	return -1;
}

__attribute__((unused)) static int rk809_buck1_set_uv(unsigned int uv)
{
	uint32_t n = (uv - RK809_BUCK1_UV_BASE) / RK809_BUCK1_UV_STEP;
	/* payload = [addr<<1 | W] [reg] [vsel] */
	uint32_t payload = ((uint32_t)(RK809_ADDR << 1)) |
			   ((uint32_t)RK809_BUCK1_ON_VSEL << 8) | (n << 16);

	if ((n == 0u) || (n > 0x3fu)) {
		return -1;
	}
	return i2c0_write(payload, 3);
}

/* --- the boost ladder ------------------------------------------------------ */

/* Rows are the Linux opp pairs (rk3568.dtsi) plus the PLL factor from
 * rk3568_pll_rates (fbdiv = MHz/24 at refdiv=1, postdiv 1*1) and the cpuclk
 * child dividers of rk3568_cpuclk_rates (atclk/gic/pclk/periph field). */
static const struct cpu_boost_row {
	unsigned int mhz;
	unsigned int fbdiv;
	unsigned int uv;
	unsigned int child_div;		/* CLKSEL_CON(3)/(4) field value */
} boost_rows[] = {
	{ 1104u, 46u,  900000u, 3u },
	{ 1416u, 59u, 1025000u, 5u },
	{ 1608u, 67u, 1100000u, 5u },
	{ 1800u, 75u, 1150000u, 7u },
};

int rk3568_cpu_boost(unsigned int mhz)
{
	const struct cpu_boost_row *row = NULL;
	unsigned int i;
#ifdef CPU_BOOST_ENABLE_DANGEROUS_CRU_DANCE
	int t;
	uint32_t con0;
#endif

	if (mhz == 0u) {
		return 0;
	}
	for (i = 0; i < (sizeof(boost_rows) / sizeof(boost_rows[0])); i++) {
		if (boost_rows[i].mhz == mhz) {
			row = &boost_rows[i];
			break;
		}
	}
	if (row == NULL) {
		board_log("cpu: boost %u MHz is not a ladder row\n", mhz);
		return -1;
	}

#ifdef CPU_BOOST_ENABLE_DANGEROUS_CRU_DANCE
	/*
	 * BOARD-MEASURED STOP (2026-10-02, R5 ledger): every runtime APLL
	 * move wedges the core stone-silent at ANY privilege level -
	 *   - MODE_CON0 -> slow (xin24m feed): dead on the write, U-Boot
	 *     included; the Linux pll_rk3328 slow-mode dance assumes a feed
	 *     this board does not wire for the core.
	 *   - armclk mux -> GPLL (CLKSEL_CON(0) bit6, hiword 0x400040):
	 *     SURVIVES - core runs 1188 MHz.
	 *   - APLL relock at 1800 while parked on GPLL: locks fine
	 *     (CON1 0x1441), but muxing back to APLL kills the core within
	 *     the write - the undervolt signature (the 0xFF readback of
	 *     RK809 register 0xBB is evidently NOT the buck1 vsel table
	 *     top; vdd_cpu is most likely still at the SPL's 816 MHz
	 *     setting). Raising the rail first needs a working RTOS-side
	 *     i2c0, which times out in the RTOS context while the IDENTICAL
	 *     register sequence driven from the U-Boot prompt completes
	 *     (context delta unresolved, see issues/).
	 * Until the voltage rail can be moved from the RTOS, this whole
	 * path stays dark.
	 */

	/* The DDR blobs leave vdd_cpu parked at the TOP of the RK809 buck1
	 * table (hand-driven i2c0 read at the U-Boot prompt: vsel 0xFF,
	 * 2026-10-02 R5 ledger) - comfortably above every opp voltage this
	 * ladder targets, so no PMIC write is needed. (The RTOS-side i2c0
	 * driver is left out of the boot path: its transfers time out in the
	 * RTOS context while the identical register sequence driven from the
	 * U-Boot prompt completes - context delta under investigation,
	 * issues/2026-10-02.) */

	/* Core child dividers before the PLL (up-scaling order): the
	 * target row's atclk/gic and pclk/periph fields; aclk_core stays
	 * at /2 (field 1, every row above 816 MHz keeps it). */
	mmio_wr32(CRU_CLKSEL_CON(3), CLKSEL_FIELDS(row->child_div,
						   row->child_div));
	mmio_wr32(CRU_CLKSEL_CON(4), CLKSEL_FIELDS(row->child_div,
						   row->child_div));
	uart_early_putc('D');

	/* 3. APLL relock: slow mode, new factors, wait lock, back to normal.
	 * The core runs on xin24m for the ~microseconds this takes.
	 * Single-char breadcrumbs (uart_early_putc) bracket every step:
	 * board_log would work too, but a dead core leaves only whatever
	 * byte already hit the wire. */
	mmio_wr32(CRU_MODE_CON0, (1u << 16) | APLL_MODE_SLOW);
	uart_early_putc('S');
	mmio_wr32(CRU_APLL_CON0, APLL_CON0(row->fbdiv, 1u));
	uart_early_putc('0');
	mmio_wr32(CRU_APLL_CON1, APLL_CON1(1u, 1u, 1u));
	uart_early_putc('1');
	for (t = 0; t < 100000; t++) {
		if (mmio_rd32(CRU_APLL_CON1) & APLL_CON1_LOCK) {
			break;
		}
	}
	uart_early_putc('L');
	if (t >= 100000) {
		board_log("cpu: APLL lock TIMEOUT\n");
		/* Best effort: back to normal mode with whatever the PLL now
		 * produces is unsafe; restore 816 MHz factors instead. */
		mmio_wr32(CRU_APLL_CON0, APLL_CON0(68u, 2u));
		mmio_wr32(CRU_APLL_CON1, APLL_CON1(1u, 1u, 1u));
		for (t = 0; t < 100000; t++) {
			if (mmio_rd32(CRU_APLL_CON1) & APLL_CON1_LOCK) {
				break;
			}
		}
		mmio_wr32(CRU_MODE_CON0, (1u << 16) | APLL_MODE_NORM);
		return -1;
	}
	uart_early_putc('N');
	mmio_wr32(CRU_MODE_CON0, (1u << 16) | APLL_MODE_NORM);
	uart_early_putc('n');

	con0 = mmio_rd32(CRU_APLL_CON0);
	board_log("cpu: boost %u MHz ok (apll con0 0x%04x, was 0x2044@816)\n",
		  row->mhz, (unsigned)(con0 & 0xffffu));

	return 0;
#else
	return -1;
#endif
}

/*
 * @file   eqos_rk3568.c
 * @brief  RK3568 SoC glue + the two GMAC ports of this board as data.
 *
 * Order matters here and is the verified one: clocks, iomux, RGMII delays,
 * then the PHY reset pulse (a hard reset re-latches the PHY's straps, so
 * anything the PHY driver wants to undo has to happen after this returns).
 *
 * The port tables are board data: register coordinates, pin multiplexing
 * tables, PHY reset lines and MAC addresses, taken from the runs that
 * verified them (prior-work docs/evidence/20260916-m1a-dual-gmac-lwip.md).
 * The driver itself knows none of it. Both ports are described; the
 * single-port build rule means only the selected plat is ever handed to the
 * driver, so the unselected port sees no clock, no register write and no
 * anchor line.
 *
 *              gmac0                    gmac1
 *   base       0xFE2A0000               0xFE010000
 *   irq        59                       64               (INTID = GIC SPI + 32)
 *   MAC        02:e4:a5:35:68:00        02:e4:a5:35:68:01
 *   clocks     CLKGATE_CON15 mask 0x1F70  CLKGATE_CON17 mask 0x07DC
 *   reset      SOFTRST_CON13 bit 7       SOFTRST_CON14 bit 12
 *   mux        CLKSEL_CON31              CLKSEL_CON33
 *   rgmii      GMAC0_CON0 0x264F         GMAC1_CON0 0x033C  (tx/rx delays)
 *   iomux      GPIO2 group (bank 0x020)  GPIO4 group (bank 0x040) + io route
 *   PHY reset  GPIO2_B1 (bank 2, pin 9)  GPIO3_C2 (bank 3, pin 18)
 *
 * The clock-gate masks and the RGMII delay values are not interchangeable:
 * gmac1's receive delay differs from gmac0's, and carrying gmac0's value over
 * is one of the failures recorded in the lab notes. gmac1's GRF values here
 * are the golden-dump measured ones (CON0 = 0x3c tx-only, rgmii-rxid: the rx
 * delay belongs to the PHY) - the dts text's tx_delay=0x4f contradicts the
 * measured golden and is NOT what this table carries; the golden dump is
 * authoritative.
 *
 * @author zhugengyu
 * @date   16.09.2026 (board-verified) / 02.10.2026 (port re-arrangement)
 */

#include <stdint.h>

#include "os/cmsis2/cmsis2.h"
#include "log.h"

#include "mmio.h"

#include "dwc_eqos.h"
#include "eqos_rk3568.h"
#include "rtl8211f.h"

/* --- GRF/CRU plumbing ------------------------------------------------------ */

/* GRF iomux register for one pin of a bank: four bits per pin, four pins per
 * 32-bit word, two words per eight-pin group. */
static uintptr_t grf_iomux_reg(unsigned int bank_off, unsigned int pin,
			       unsigned int *shift)
{
	*shift = (pin % 4) * 4;

	return RK3568_GRF_BASE + bank_off + (pin / 8) * 8 + ((pin % 8) / 4) * 4;
}

/* GRF/CRU field write with the hiword write-enable mask. */
static void grf_write(unsigned int off, uint32_t mask, uint32_t val)
{
	mmio_wr32(RK3568_GRF_BASE + off, ((mask & 0xffffu) << 16) | (val & mask));
}

static void rk3568_gmac_clocks_enable(const struct rk3568_gmac_soc *soc)
{
	/* Gate bits are active-low: write 0 into the hiword-masked field to
	 * enable the clock. */
	mmio_wr32(RK3568_CRU_BASE + soc->clkgate_con_off,
		  (uint32_t)soc->clkgate_mask << 16);
	/* Soft reset bits are active-low too: write 0 to deassert. */
	mmio_wr32(RK3568_CRU_BASE + soc->softrst_con_off,
		  (uint32_t)soc->softrst_mask << 16);
	/* GMAC clock muxes: all-zero selector = 125 MHz RGMII output mode
	 * (rx/tx from the 125M source, ptp ref 62.5M). */
	mmio_wr32(RK3568_CRU_BASE + soc->clksel_con_off, 0xffffu << 16);
}

static void rk3568_gmac_iomux(const struct rk3568_gmac_soc *soc)
{
	int i;

	if (soc->grf_route_off != 0) {
		/* Full register word: write-enable mask in bits[31:16]. */
		mmio_wr32(RK3568_GRF_BASE + soc->grf_route_off, soc->grf_route_val);
	}

	for (i = 0; i < soc->pin_num; i++) {
		const struct rk3568_gmac_pin *p = &soc->pins[i];
		unsigned int shift;
		uintptr_t reg = grf_iomux_reg(soc->grf_iomux_off, p->pin, &shift);

		mmio_wr32(reg, ((0xfu << shift) << 16) | ((uint32_t)p->func << shift));
	}
}

static void rk3568_gmac_rgmii_setup(const struct rk3568_gmac_soc *soc)
{
	/* CON1 (interface select and delay enables) before CON0 (the delay
	 * values): the verified order. */
	grf_write(soc->grf_maccon1_off, soc->grf_maccon1_mask, soc->grf_maccon1_val);
	grf_write(soc->grf_maccon0_off, soc->grf_maccon0_mask, soc->grf_maccon0_val);
}

/* GPIO bank, version 2 layout: data at 0x00/0x04, direction at 0x08/0x0c,
 * value in bits[15:0] and write-enable in bits[31:16]. */
static void gpio_write(unsigned long bank, unsigned int pin, int output, int value)
{
	unsigned long off = (pin < 16) ? 0x0 : 0x4;
	uint32_t bit = 1u << (pin & 15);
	uint32_t we = bit << 16;

	/* Data first, then the direction. */
	mmio_wr32(bank + off, we | (value ? bit : 0));
	mmio_wr32(bank + off + 0x8, we | (output ? bit : 0));
}

static void rk3568_gmac_phy_reset_pulse(const struct rk3568_gmac_soc *soc)
{
	/* Active low: assert for 20 ms, release, then wait 100 ms for the PHY
	 * to boot and re-latch its straps. Writing the polarity backwards
	 * leaves MDIO reading 0xffff from every address, which looks like a
	 * dead bus rather than a held reset. */
	gpio_write(soc->rst_gpio_base, soc->rst_gpio_pin, 1, 0);
	osDelay(20);
	gpio_write(soc->rst_gpio_base, soc->rst_gpio_pin, 1, 1);
	osDelay(100);
}

int rk3568_gmac_soc_init(const struct dwc_eqos_plat *plat)
{
	const struct rk3568_gmac_soc *soc = plat->soc_data;

	rk3568_gmac_clocks_enable(soc);
	rk3568_gmac_iomux(soc);
	rk3568_gmac_rgmii_setup(soc);
	rk3568_gmac_phy_reset_pulse(soc);

	return 0;
}

void rk3568_gmac_soc_swr_quirk(const struct dwc_eqos_plat *plat, int enter)
{
	const struct rk3568_gmac_soc *soc = plat->soc_data;
	uint32_t val = (enter != 0) ? 0x1u : 0x0u;

	/* While DMA_MODE.SWR is asserted the GMAC clock mux has to take the
	 * RMII (62.5M) path or the reset never clears: on gmac0 the 125M direct
	 * path is not active at cold boot, and gmac1 needs the same walk on
	 * some boots. Leaving it in RMII afterwards would keep the MAC at the
	 * wrong rate, hence the exit call. */
	mmio_wr32(RK3568_CRU_BASE + soc->clksel_con_off, (0x3u << 16) | val);
}

/* --- the two ports ---------------------------------------------------------- */

/* gmac0: rgmii bus + clock, tx_bus2/rx_bus2, miim. */
static const struct rk3568_gmac_pin gmac0_pins[] = {
	{ 3, 2 }, { 4, 2 }, { 5, 2 }, { 6, 2 }, { 7, 2 }, { 8, 2 },
	{ 11, 1 }, { 12, 1 }, { 13, 1 }, { 14, 1 }, { 15, 2 }, { 16, 2 },
	{ 19, 2 }, { 20, 2 },
};

static const struct rk3568_gmac_soc gmac0_soc = {
	.clkgate_con_off = 0x33C,
	.clkgate_mask    = 0x1F70,
	.softrst_con_off = 0x434,
	.softrst_mask    = 0x0080,
	.clksel_con_off  = 0x17C,

	.grf_maccon0_off  = 0x380,
	.grf_maccon0_val  = 0x264F,	/* rx delay 0x26, tx delay 0x4f */
	.grf_maccon0_mask = 0x7F7F,
	.grf_maccon1_off  = 0x384,
	.grf_maccon1_val  = 0x0013,	/* RGMII + both clock delay enables */
	.grf_maccon1_mask = 0x0073,

	.grf_iomux_off   = 0x020,
	.pins            = gmac0_pins,
	.pin_num         = sizeof(gmac0_pins) / sizeof(gmac0_pins[0]),
	.grf_route_off   = 0,
	.grf_route_val   = 0,

	.rst_gpio_base   = 0xFE750000UL,
	.rst_gpio_pin    = 9,
};

static const struct dwc_eqos_plat gmac0_plat = {
	.index     = 0,
	.base_addr = 0xFE2A0000UL,
	.irq_num   = 59,
	.mac_addr  = { 0x02, 0xe4, 0xa5, 0x35, 0x68, 0x00 },

	.soc_init      = rk3568_gmac_soc_init,
	.soc_swr_quirk = rk3568_gmac_soc_swr_quirk,
	.soc_data      = &gmac0_soc,
};

/* gmac1m0: tx_bus2/rx_bus2, rgmii clock, clkinout and miim. */
static const struct rk3568_gmac_pin gmac1_pins[] = {
	{ 13, 3 }, { 14, 3 }, { 2, 3 }, { 3, 3 }, { 15, 3 }, { 9, 3 },
	{ 10, 3 }, { 4, 3 }, { 5, 3 }, { 11, 3 }, { 6, 3 }, { 7, 3 },
	{ 16, 3 }, { 20, 3 }, { 21, 3 },
};

static const struct rk3568_gmac_soc gmac1_soc = {
	.clkgate_con_off = 0x344,
	.clkgate_mask    = 0x07DC,
	.softrst_con_off = 0x438,
	.softrst_mask    = 0x1000,
	.clksel_con_off  = 0x184,

	.grf_maccon0_off  = 0x388,
	.grf_maccon0_val  = 0x033C,	/* rx delay 0x03, tx delay 0x3c (golden) */
	.grf_maccon0_mask = 0x7F7F,
	.grf_maccon1_off  = 0x38C,
	.grf_maccon1_val  = 0x0013,
	.grf_maccon1_mask = 0x0073,

	.grf_iomux_off   = 0x040,
	.pins            = gmac1_pins,
	.pin_num         = sizeof(gmac1_pins) / sizeof(gmac1_pins[0]),
	.grf_route_off   = 0x300,
	.grf_route_val   = 0x0100,

	.rst_gpio_base   = 0xFE760000UL,
	.rst_gpio_pin    = 18,
};

static const struct dwc_eqos_plat gmac1_plat = {
	.index     = 1,
	.base_addr = 0xFE010000UL,
	.irq_num   = 64,
	.mac_addr  = { 0x02, 0xe4, 0xa5, 0x35, 0x68, 0x01 },

	.soc_init      = rk3568_gmac_soc_init,
	.soc_swr_quirk = rk3568_gmac_soc_swr_quirk,
	.soc_data      = &gmac1_soc,
};

static const struct dwc_eqos_plat *const gmac_plats[DWC_EQOS_PORT_COUNT] = {
	&gmac0_plat, &gmac1_plat,
};

const struct dwc_eqos_plat *eqos_board_plat(unsigned int index)
{
	if (index >= DWC_EQOS_PORT_COUNT) {
		return NULL;
	}
	return gmac_plats[index];
}

void eqos_board_golden_dump(unsigned int index)
{
	const struct rk3568_gmac_soc *soc;
	unsigned int clksel_off, clkgate_off;

	if (index >= DWC_EQOS_PORT_COUNT) {
		return;
	}
	soc = gmac_plats[index]->soc_data;

	/* The CRU registers are the port's own halves of shared CON registers:
	 * read the whole word and print it - the golden comparison looks at
	 * this port's bits (con31/con33 must read all-zero, the gate bits of
	 * clkgate_con15/17 must read 0 = unguarded). */
	clksel_off = soc->clksel_con_off;
	clkgate_off = soc->clkgate_con_off;
	LOG_INFO("golden: gmac%u clksel[0x%x]=0x%08x clkgate[0x%x]=0x%08x"
		  " grf[0x%x]=0x%08x grf[0x%x]=0x%08x\n",
		  index, clksel_off,
		  (unsigned)mmio_rd32(RK3568_CRU_BASE + clksel_off),
		  clkgate_off,
		  (unsigned)mmio_rd32(RK3568_CRU_BASE + clkgate_off),
		  soc->grf_maccon0_off,
		  (unsigned)mmio_rd32(RK3568_GRF_BASE + soc->grf_maccon0_off),
		  soc->grf_maccon1_off,
		  (unsigned)mmio_rd32(RK3568_GRF_BASE + soc->grf_maccon1_off));
}

/* --- MDIO borrow ------------------------------------------------------------ */

/* The PHY driver rides the MAC's management interface: ctx is the port index
 * as a value, forwarded to the core's MDIO entry points. */
int32_t eqos_board_mdio_read(void *ctx, uint8_t phy_addr, uint8_t reg_addr,
			     uint16_t *data)
{
	return dwc_eqos_phy_read((unsigned int)(uintptr_t)ctx, phy_addr,
				 reg_addr, data);
}

int32_t eqos_board_mdio_write(void *ctx, uint8_t phy_addr, uint8_t reg_addr,
			      uint16_t data)
{
	return dwc_eqos_phy_write((unsigned int)(uintptr_t)ctx, phy_addr,
				  reg_addr, data);
}

/*
 * @file   eqos_rk3568.h
 * @brief  RK3568 SoC glue for the DesignWare Ethernet QoS MAC core.
 *
 * The glue owns everything outside the IP: CRU clock gates, soft resets and
 * the clock mux, GRF interface mode and RGMII delay programming, pin iomux
 * with io routing, and the PHY hard reset GPIO. A port is described by filling
 * a struct rk3568_gmac_soc with its coordinates and hooking
 * rk3568_gmac_soc_init / rk3568_gmac_soc_swr_quirk into struct dwc_eqos_plat
 * (the port tables in eqos_rk3568.c do that for both ports).
 *
 * This board is configured statically - there is no device tree or pinctrl
 * layer in the image - so all of the above is per-port data, not lookup.
 *
 * The CRU, GRF and GPIO v2 register blocks all use the Rockchip hiword
 * write-enable protocol: value in bits[15:0], write-enable mask in bits[31:16].
 * The iomux is the odd one out: it is per-pin, four bits per pin, four pins per
 * 32-bit word, with the write-enable nibble in the same shape. Mixing the two
 * protocols writes the wrong pin.
 *
 * @author zhugengyu
 * @date   16.09.2026 (board-verified) / 02.10.2026 (port re-arrangement)
 */

#ifndef FREEWEBCAM_EQOS_RK3568_H
#define FREEWEBCAM_EQOS_RK3568_H

#include <stdint.h>

#include "dwc_eqos.h"

/* Register bases of the two blocks the glue touches. */
#define RK3568_CRU_BASE	0xfdd20000UL
#define RK3568_GRF_BASE	0xfdc60000UL

/* GRF iomux entry: one pin of a bank with its function selector. */
struct rk3568_gmac_pin {
	uint8_t pin;	/* pin number inside the GPIO bank */
	uint8_t func;	/* iomux function selector */
};

struct rk3568_gmac_soc {
	/* CRU: clock gates, soft reset and the GMAC clock mux */
	unsigned int clkgate_con_off;	/* CRU_CLKGATE_CON[n] offset */
	uint32_t     clkgate_mask;	/* bits to ungate (write 0) */
	unsigned int softrst_con_off;	/* CRU_SOFTRST_CON[n] offset */
	uint32_t     softrst_mask;	/* bits to deassert (write 0) */
	unsigned int clksel_con_off;	/* CRU_CLKSEL_CON[n] offset */

	/* GRF: interface mode, RGMII delays, iomux and io route */
	unsigned int grf_maccon0_off;	/* tx/rx delay value register */
	uint32_t     grf_maccon0_val;	/* (rx_delay << 8) | tx_delay */
	uint32_t     grf_maccon0_mask;
	unsigned int grf_maccon1_off;	/* interface mode + delay enables */
	uint32_t     grf_maccon1_val;
	uint32_t     grf_maccon1_mask;
	unsigned int grf_iomux_off;	/* GRF iomux bank base offset */
	const struct rk3568_gmac_pin *pins;
	int          pin_num;
	unsigned int grf_route_off;	/* io route register, 0 if unused */
	uint32_t     grf_route_val;

	/* PHY hard reset GPIO (DW-APB GPIO bank, version 2 layout) */
	unsigned long rst_gpio_base;
	unsigned int  rst_gpio_pin;	/* pin inside the bank */
};

/* The two hooks plugged into struct dwc_eqos_plat. */
int  rk3568_gmac_soc_init(const struct dwc_eqos_plat *plat);
void rk3568_gmac_soc_swr_quirk(const struct dwc_eqos_plat *plat, int enter);

/* The board's port table: CONFIG_GMAC_PORT (0/1) indexes it; the unselected
 * port's plat is never handed to the driver, so nothing touches it. */
const struct dwc_eqos_plat *eqos_board_plat(unsigned int index);

/* Post-init readback of the port's CRU/GRF configuration, one log line:
 * the P7-1b golden dump (spec FR-4/判据 1) - the values must equal the
 * U-Boot-leftover golden measured in the lab ([LAB] 事实 2), proving the
 * explicit re-initialization is idempotent with the verified state. */
void eqos_board_golden_dump(unsigned int index);

/* The MDIO pair the PHY driver borrows (ctx = the port index as a value). */
int32_t eqos_board_mdio_read(void *ctx, uint8_t phy_addr, uint8_t reg_addr,
			     uint16_t *data);
int32_t eqos_board_mdio_write(void *ctx, uint8_t phy_addr, uint8_t reg_addr,
			      uint16_t data);

#endif /* FREEWEBCAM_EQOS_RK3568_H */

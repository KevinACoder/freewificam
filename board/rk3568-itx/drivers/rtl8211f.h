/*
 * @file   rtl8211f.h
 * @brief  Realtek RTL8211F gigabit PHY, minimal clause-22 driver.
 *
 * One PHY per GMAC port, reached over that port's MDIO: the shape for this is
 * a PHY driver that borrows the MAC's management interface - initialize()
 * receives the MAC's MDIO read/write pair and uses those for every register
 * access. The PHY driver therefore knows nothing about GMAC, DMA or clocks,
 * and the MAC driver knows nothing about PHY models.
 *
 * Two things are RTL8211F-specific and worth knowing before reading the code:
 *
 *  - The chip straps its own RGMII TX delay and a HARD reset re-latches the
 *    strap. This board's GRF delays are calibrated with the PHY-side delay
 *    OFF, so after every hard reset the strapped delay must be cleared again
 *    (page 0xd08, register 0x11, bit 8). Leaving it set double-delays the
 *    transmit path and long frames die while ARP and short traffic keep
 *    working - the misleading combination recorded in the lab notes.
 *
 *  - Its register page is selected through register 0x1f (write 0x0d08 for the
 *    page holding that delay bit, write 0 to return to page 0), not through a
 *    separate page register.
 *
 * Port note: the CMSIS ARM_DRIVER_ETH_PHY vtable and its instance macro are
 * gone (specs/lwip-adapt §3.4); the entry points take the port index and the
 * shared eqos_types.h shapes. The clause-22 flow and the strap discipline are
 * the board-verified ones, unchanged.
 *
 * @author zhugengyu
 * @date   16.09.2026 (board-verified core) / 02.10.2026 (port re-arrangement)
 */

#ifndef FREEWEBCAM_RTL8211F_H
#define FREEWEBCAM_RTL8211F_H

#include <stdint.h>

#include "eqos_types.h"

#define RTL8211F_PHY_COUNT	2

/* Identification, from the standard PHY ID registers. */
#define RTL8211F_PHYSID1	0x001c
#define RTL8211F_PHYSID2	0xc916

/* PHY mode bits (set_mode argument; bit positions follow the CMSIS masks). */
#define EQOS_PHY_SPEED_Msk		0x3u
#define EQOS_PHY_SPEED_10M		0x0u
#define EQOS_PHY_SPEED_100M		0x1u
#define EQOS_PHY_SPEED_1G		0x2u
#define EQOS_PHY_AUTO_NEGOTIATE		0x8u
#define EQOS_PHY_LOOPBACK		0x10u
#define EQOS_PHY_ISOLATE		0x20u

/* What the boot log and the heartbeat want to report about a port's PHY. */
struct rtl8211f_info {
	uint8_t  addr;	/* MDIO address the scan settled on */
	uint16_t id1;
	uint16_t id2;
	uint8_t  found;	/* 0 when no PHY answered on the bus */
};

int32_t rtl8211f_initialize(unsigned int index, void *mdio_ctx,
			    eqos_mdio_read_t fn_read, eqos_mdio_write_t fn_write);
int32_t rtl8211f_uninitialize(unsigned int index);
int32_t rtl8211f_power_control(unsigned int index, eqos_power_state_t state);
int32_t rtl8211f_set_interface(unsigned int index, uint32_t interface);
int32_t rtl8211f_set_mode(unsigned int index, uint32_t mode);
eqos_link_state_t rtl8211f_get_link_state(unsigned int index);
eqos_link_info_t rtl8211f_get_link_info(unsigned int index);
void rtl8211f_get_info(unsigned int index, struct rtl8211f_info *info);

#endif /* FREEWEBCAM_RTL8211F_H */

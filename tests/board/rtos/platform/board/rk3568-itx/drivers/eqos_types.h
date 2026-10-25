/*
 * @file   eqos_types.h
 * @brief  Shared types of the GMAC driver family (MAC core, PHY, SoC glue).
 *
 * The register behaviour is carried over from the board-verified core, but
 * not the CMSIS-Driver API shell (ARM_DRIVER_ETH_MAC / ARM_ETH_PHY) it once
 * lived behind. The encodings
 * below keep the CMSIS bit positions the verified code reasoned in - speed in
 * bits[1:0] with 10M=0, DUPLEX_HALF = bit 2 - so the carried-over switch
 * statements stay line-identical.
 *
 * @author zhugengyu
 * @date   02.10.2026
 */

#ifndef FREEWIFICAM_EQOS_TYPES_H
#define FREEWIFICAM_EQOS_TYPES_H

#include <stdint.h>

/* --- return codes (CMSIS ARM_DRIVER_ERROR_* shape) ------------------------- */

#define EQOS_OK			0
#define EQOS_ERROR		-1
#define EQOS_ERROR_PARAMETER	-2
#define EQOS_ERROR_TIMEOUT	-3
#define EQOS_ERROR_BUSY		-4
#define EQOS_ERROR_UNSUPPORTED	-5

/* --- power states ------------------------------------------------------------ */

typedef enum {
	EQOS_POWER_OFF = 0,
	EQOS_POWER_FULL = 1,
} eqos_power_state_t;

/* --- speed / duplex (bit positions follow the CMSIS masks) ------------------- */

#define EQOS_SPEED_Msk		0x3u
#define EQOS_SPEED_10M		0x0u
#define EQOS_SPEED_100M		0x1u
#define EQOS_SPEED_1G		0x2u
#define EQOS_DUPLEX_Msk		0x4u
#define EQOS_DUPLEX_HALF	0x4u
#define EQOS_DUPLEX_FULL	0x0u

/* MAC/PHY media interface (strapped on this board, not register-selected). */
#define EQOS_INTERFACE_MII	0u
#define EQOS_INTERFACE_RGMII	1u

/* --- link --------------------------------------------------------------------- */

typedef enum {
	EQOS_LINK_DOWN = 0,
	EQOS_LINK_UP = 1,
} eqos_link_state_t;

typedef struct {
	uint32_t speed;		/* EQOS_SPEED_* */
	uint32_t duplex;	/* EQOS_DUPLEX_* */
} eqos_link_info_t;

/* --- MDIO borrow (the PHY rides the MAC's management interface) ---------------- */

typedef int32_t (*eqos_mdio_read_t)(void *ctx, uint8_t phy_addr,
				    uint8_t reg_addr, uint16_t *data);
typedef int32_t (*eqos_mdio_write_t)(void *ctx, uint8_t phy_addr,
				     uint8_t reg_addr, uint16_t data);

#endif /* FREEWIFICAM_EQOS_TYPES_H */

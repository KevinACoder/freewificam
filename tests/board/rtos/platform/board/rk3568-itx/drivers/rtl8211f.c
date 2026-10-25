/*
 * @file   rtl8211f.c
 * @brief  Realtek RTL8211F PHY over a borrowed MAC management interface.
 *
 * The standard clause-22 register set does the work: BMCR/BMSR for reset,
 * autonegotiation and link state, the 1000BASE-T control/status pair for
 * gigabit, and the 10/100 advertisement and link-partner registers for the
 * fallback. Everything is reached through the MDIO pair handed to
 * initialize(), so this driver is usable over any MAC's MDIO.
 *
 * Ordering, and why PowerControl is where the strap clear lives: the MAC's
 * power-up pulses the PHY's hard reset line, and that pulse re-latches the
 * strapped RGMII TX delay (see rtl8211f.h). The adapter therefore powers the
 * MAC up first and this driver up second, which is also the order the ports
 * were verified in. A soft reset here does not re-latch straps.
 *
 * @author zhugengyu
 * @date   16.09.2026 (board-verified core) / 02.10.2026 (port re-arrangement)
 */

#include <stdint.h>

#include "os/cmsis2/cmsis2.h"
#include "log.h"

#include "rtl8211f.h"

/* --- clause 22 registers and bits ----------------------------------------- */

#define MII_BMCR		0x00
#define  BMCR_RESET		0x8000
#define  BMCR_LOOPBACK		0x4000
#define  BMCR_ANENABLE		0x1000
#define  BMCR_PDOWN		0x0800
#define  BMCR_ISOLATE		0x0400
#define  BMCR_ANRESTART		0x0200

#define MII_BMSR		0x01
#define  BMSR_LSTATUS		0x0004
#define  BMSR_ANEGCOMPLETE	0x0020

#define MII_PHYSID1		0x02
#define MII_PHYSID2		0x03

#define MII_ADVERTISE		0x04
#define  ADVERTISE_100FULL	0x0100
#define  ADVERTISE_100HALF	0x0080
#define  ADVERTISE_10FULL	0x0040
#define  ADVERTISE_10HALF	0x0020
#define  ADVERTISE_ALL		(ADVERTISE_100FULL | ADVERTISE_100HALF | \
				 ADVERTISE_10FULL | ADVERTISE_10HALF)

#define MII_LPA			0x05
#define  LPA_100FULL		0x0100
#define  LPA_100HALF		0x0080
#define  LPA_10FULL		0x0040
#define  LPA_10HALF		0x0020

#define MII_CTRL1000		0x09
#define  ADVERTISE_1000FULL	0x0200
#define  ADVERTISE_1000HALF	0x0100

#define MII_STAT1000		0x0a
#define  LPA_1000FULL		0x0800
#define  LPA_1000HALF		0x0400

/* RTL8211F extension registers: the page selector and, on page 0x0d08, the
 * register holding the strapped RGMII TX delay enable. */
#define RTL8211F_REG_PAGESEL	0x1f
#define RTL8211F_PAGE_D08	0x0d08
#define RTL8211F_REG_TXDELAY	0x11
#define RTL8211F_TXDELAY_EN	0x0100

/* Autonegotation and reset timeouts, in 1 ms steps.
 *
 * The strap-clear read/write below is the reason a PHY that has never had its
 * hard reset released shows up as a bus of 0xffff: nothing answers while the
 * reset line is asserted, and the scan simply runs out of addresses. */
#define RTL8211F_RESET_TRIES	50
/* Autonegotiation wait, in 1 ms steps. Measured on this board: a cold start
 * usually links within a second, and both ports were observed needing a moment
 * longer than the emBox port's 2 s wait - after which the link monitor picked
 * them up anyway. 5 s makes the bring-up log say "link up" instead of "no link
 * yet" in the normal case. */
#define RTL8211F_AUTONEG_TRIES	5000
#define RTL8211F_PHY_SCAN_MAX	32

struct rtl8211f_dev {
	void *mdio_ctx;
	eqos_mdio_read_t  fn_read;
	eqos_mdio_write_t fn_write;
	uint8_t  initialized;
	uint8_t  powered;
	uint8_t  found;
	uint8_t  addr;
	uint16_t id1;
	uint16_t id2;
};

static struct rtl8211f_dev phys[RTL8211F_PHY_COUNT];

/* --- register access ------------------------------------------------------ */

static int32_t phy_reg_read(struct rtl8211f_dev *dev, uint8_t reg, uint16_t *val)
{
	if (dev->fn_read == NULL) {
		return EQOS_ERROR;
	}

	return dev->fn_read(dev->mdio_ctx, dev->addr, reg, val);
}

static int32_t phy_reg_write(struct rtl8211f_dev *dev, uint8_t reg, uint16_t val)
{
	if (dev->fn_write == NULL) {
		return EQOS_ERROR;
	}

	return dev->fn_write(dev->mdio_ctx, dev->addr, reg, val);
}

/* --- RTL8211F specifics --------------------------------------------------- */

/* Find the PHY on this port's MDIO bus. The dts puts it at address 1 and the
 * prior workspace's run found it at 0; the scan is what makes the actual
 * address visible instead of mysterious (spec open question 1 settles on the
 * measured value). */
static int rtl8211f_detect(struct rtl8211f_dev *dev)
{
	uint8_t addr;

	dev->found = 0;
	for (addr = 0; addr < RTL8211F_PHY_SCAN_MAX; addr++) {
		uint16_t id1 = 0xffff;
		uint16_t id2 = 0xffff;

		dev->addr = addr;
		if (phy_reg_read(dev, MII_PHYSID1, &id1) != EQOS_OK) {
			continue;
		}
		if (id1 == 0xffff) {
			continue;
		}
		(void)phy_reg_read(dev, MII_PHYSID2, &id2);

		dev->id1 = id1;
		dev->id2 = id2;
		dev->found = 1;
		return 0;
	}

	return -1;
}

/* Log prefix: the driver instance (which port's PHY this is), not the MDIO
 * address - both PHYs answer at the same address on their own buses, so the
 * address alone would print "phy0" for both ports. */
static const char *rtl8211f_name(unsigned int index)
{
	return (index == 0U) ? "phy0" : "phy1";
}

/* Undo the strapped RGMII TX delay. Must run after every reset of the PHY -
 * including the soft reset below, which re-latches the strap exactly as a hard
 * reset does. See the file header of rtl8211f.h for what breaks without it. */
static void rtl8211f_clear_tx_delay(unsigned int index, struct rtl8211f_dev *dev)
{
	uint16_t val = 0;

	if (!dev->found) {
		return;
	}
	if (phy_reg_write(dev, RTL8211F_REG_PAGESEL, RTL8211F_PAGE_D08) != EQOS_OK) {
		return;
	}
	if (phy_reg_read(dev, RTL8211F_REG_TXDELAY, &val) == EQOS_OK) {
		if ((val & RTL8211F_TXDELAY_EN) != 0) {
			(void)phy_reg_write(dev, RTL8211F_REG_TXDELAY,
					    val & (uint16_t)~RTL8211F_TXDELAY_EN);
			LOG_INFO("%s: cleared strapped RGMII tx delay"
				  " (mdio %u, d08.0x11 0x%04x -> 0x%04x)\n",
				  rtl8211f_name(index), dev->addr, val,
				  val & (uint16_t)~RTL8211F_TXDELAY_EN);
		} else {
			/* Say so when it is already clear: the value that comes
			 * back here is how the re-latch behaviour was caught. */
			LOG_INFO("%s: strapped RGMII tx delay already clear"
				  " (mdio %u, d08.0x11 0x%04x)\n",
				  rtl8211f_name(index), dev->addr, val);
		}
	}
	/* Back to page 0 before anyone else touches the standard registers. */
	(void)phy_reg_write(dev, RTL8211F_REG_PAGESEL, 0);
}

static int rtl8211f_softreset(struct rtl8211f_dev *dev)
{
	uint16_t bmcr = 0;
	int tries;

	if (phy_reg_read(dev, MII_BMCR, &bmcr) != EQOS_OK) {
		return EQOS_ERROR;
	}
	if (phy_reg_write(dev, MII_BMCR, (uint16_t)(bmcr | BMCR_RESET)) != EQOS_OK) {
		return EQOS_ERROR;
	}

	for (tries = 0; tries < RTL8211F_RESET_TRIES; tries++) {
		if (phy_reg_read(dev, MII_BMCR, &bmcr) != EQOS_OK) {
			return EQOS_ERROR;
		}
		if ((bmcr & BMCR_RESET) == 0) {
			return EQOS_OK;
		}
		osDelay(1);
	}

	return EQOS_ERROR_TIMEOUT;
}

/* Resolve speed and duplex from what the link partner advertised. 1000BASE-T
 * abilities live in the 1000 status register; the 10/100 ones in the link
 * partner register. */
static eqos_link_info_t rtl8211f_link_info(struct rtl8211f_dev *dev)
{
	eqos_link_info_t info = { .speed = EQOS_SPEED_10M, .duplex = EQOS_DUPLEX_HALF };
	uint16_t stat1000 = 0;
	uint16_t lpa = 0;

	if (!dev->powered) {
		return info;
	}
	if (phy_reg_read(dev, MII_STAT1000, &stat1000) != EQOS_OK) {
		return info;
	}
	if (phy_reg_read(dev, MII_LPA, &lpa) != EQOS_OK) {
		return info;
	}

	if (stat1000 & LPA_1000FULL) {
		info.speed = EQOS_SPEED_1G;
		info.duplex = EQOS_DUPLEX_FULL;
	} else if (stat1000 & LPA_1000HALF) {
		info.speed = EQOS_SPEED_1G;
		info.duplex = EQOS_DUPLEX_HALF;
	} else if (lpa & LPA_100FULL) {
		info.speed = EQOS_SPEED_100M;
		info.duplex = EQOS_DUPLEX_FULL;
	} else if (lpa & LPA_100HALF) {
		info.speed = EQOS_SPEED_100M;
		info.duplex = EQOS_DUPLEX_HALF;
	} else if (lpa & LPA_10FULL) {
		info.speed = EQOS_SPEED_10M;
		info.duplex = EQOS_DUPLEX_FULL;
	} else if (lpa & LPA_10HALF) {
		info.speed = EQOS_SPEED_10M;
		info.duplex = EQOS_DUPLEX_HALF;
	}

	return info;
}

/* --- driver entry points --------------------------------------------------- */

int32_t rtl8211f_initialize(unsigned int index, void *mdio_ctx,
			    eqos_mdio_read_t fn_read, eqos_mdio_write_t fn_write)
{
	struct rtl8211f_dev *dev;

	if ((index >= RTL8211F_PHY_COUNT) || (fn_read == NULL) || (fn_write == NULL)) {
		return EQOS_ERROR_PARAMETER;
	}
	dev = &phys[index];
	if (dev->initialized) {
		return EQOS_ERROR;
	}

	dev->mdio_ctx = mdio_ctx;
	dev->fn_read = fn_read;
	dev->fn_write = fn_write;
	dev->initialized = 1;

	return EQOS_OK;
}

int32_t rtl8211f_uninitialize(unsigned int index)
{
	struct rtl8211f_dev *dev;

	if (index >= RTL8211F_PHY_COUNT) {
		return EQOS_ERROR_PARAMETER;
	}
	dev = &phys[index];

	(void)rtl8211f_power_control(index, EQOS_POWER_OFF);
	dev->fn_read = NULL;
	dev->fn_write = NULL;
	dev->initialized = 0;

	return EQOS_OK;
}

int32_t rtl8211f_power_control(unsigned int index, eqos_power_state_t state)
{
	struct rtl8211f_dev *dev;
	uint16_t bmcr = 0;

	if (index >= RTL8211F_PHY_COUNT) {
		return EQOS_ERROR_PARAMETER;
	}
	dev = &phys[index];
	if (!dev->initialized) {
		return EQOS_ERROR;
	}

	switch (state) {
	case EQOS_POWER_FULL:
		if (dev->powered) {
			return EQOS_OK;
		}
		if (rtl8211f_detect(dev) != 0) {
			LOG_INFO("%s: no phy answered on mdio\n", rtl8211f_name(index));
			return EQOS_ERROR;
		}
		LOG_INFO("%s: id 0x%04x:0x%04x at mdio %u\n", rtl8211f_name(index),
			  dev->id1, dev->id2, dev->addr);
		if (dev->id1 != RTL8211F_PHYSID1 || dev->id2 != RTL8211F_PHYSID2) {
			/* Keep going - the clause-22 flow below is standard -
			 * but say so, because every RTL8211F-specific step in
			 * this file is then a guess. */
			LOG_INFO("%s: not an RTL8211F (expected 0x001c:0xc916)\n",
				  rtl8211f_name(index));
		}

		/* Soft reset first, strap clear last: every reset of this PHY
		 * re-latches its strapped RGMII TX delay, so the clear has to be
		 * the last reset-adjacent thing that happens. Measured both
		 * ways on this board: clearing before the soft reset leaves
		 * gmac0 unable to pass host-to-board unicast (broadcast still
		 * arrives), clearing after it makes both ports work - the
		 * documented double-delay failure, on the port with the known
		 * narrower margin. */
		if (rtl8211f_softreset(dev) != EQOS_OK) {
			LOG_WARNING("%s: soft reset timeout\n", rtl8211f_name(index));
			return EQOS_ERROR;
		}
		rtl8211f_clear_tx_delay(index, dev);

		dev->powered = 1;
		return EQOS_OK;

	case EQOS_POWER_OFF:
		if (!dev->powered) {
			return EQOS_OK;
		}
		if (phy_reg_read(dev, MII_BMCR, &bmcr) == EQOS_OK) {
			(void)phy_reg_write(dev, MII_BMCR,
					    (uint16_t)(bmcr | BMCR_PDOWN));
		}
		dev->powered = 0;
		return EQOS_OK;

	default:
		return EQOS_ERROR_UNSUPPORTED;
	}
}

int32_t rtl8211f_set_interface(unsigned int index, uint32_t interface)
{
	if (index >= RTL8211F_PHY_COUNT) {
		return EQOS_ERROR_PARAMETER;
	}

	/* The MAC/PHY interface on this board is RGMII and is strapped, not
	 * register-selected: there is nothing to program, so anything else is
	 * refused rather than silently accepted. */
	return (interface == EQOS_INTERFACE_RGMII) ? EQOS_OK : EQOS_ERROR_UNSUPPORTED;
}

int32_t rtl8211f_set_mode(unsigned int index, uint32_t mode)
{
	struct rtl8211f_dev *dev;
	eqos_link_info_t info;
	uint16_t bmcr = 0;
	uint16_t ctrl1000 = 0;
	uint16_t advertise = ADVERTISE_ALL;
	uint32_t speed = mode & EQOS_PHY_SPEED_Msk;
	int tries;

	if (index >= RTL8211F_PHY_COUNT) {
		return EQOS_ERROR_PARAMETER;
	}
	dev = &phys[index];
	if (!dev->powered) {
		return EQOS_ERROR;
	}
	if (phy_reg_read(dev, MII_BMCR, &bmcr) != EQOS_OK) {
		return EQOS_ERROR;
	}

	/* Loopback and isolate are plain BMCR bits; apply them and return. */
	if (mode & EQOS_PHY_LOOPBACK) {
		bmcr |= BMCR_LOOPBACK;
	} else {
		bmcr &= ~BMCR_LOOPBACK;
	}
	if (mode & EQOS_PHY_ISOLATE) {
		bmcr |= BMCR_ISOLATE;
	} else {
		bmcr &= ~BMCR_ISOLATE;
	}

	if ((mode & EQOS_PHY_AUTO_NEGOTIATE) == 0) {
		/* Fixed speed: advertise only that, which is what a
		 * non-autonegotiating partner needs to see. */
		switch (speed) {
		case EQOS_PHY_SPEED_1G:
			ctrl1000 = ADVERTISE_1000FULL | ADVERTISE_1000HALF;
			advertise = 0;
			break;
		case EQOS_PHY_SPEED_100M:
			advertise = ADVERTISE_100FULL | ADVERTISE_100HALF;
			break;
		case EQOS_PHY_SPEED_10M:
			advertise = ADVERTISE_10FULL | ADVERTISE_10HALF;
			break;
		default:
			return EQOS_ERROR_PARAMETER;
		}
	} else {
		/* Gigabit plus the 10/100 fallback, all duplexes: this is the
		 * advertisement the port was verified with. */
		ctrl1000 = ADVERTISE_1000FULL | ADVERTISE_1000HALF;
	}

	if (phy_reg_write(dev, MII_CTRL1000, ctrl1000) != EQOS_OK) {
		return EQOS_ERROR;
	}
	if (phy_reg_write(dev, MII_ADVERTISE, advertise) != EQOS_OK) {
		return EQOS_ERROR;
	}
	if (phy_reg_write(dev, MII_BMCR,
			  (uint16_t)(bmcr | BMCR_ANENABLE | BMCR_ANRESTART)) != EQOS_OK) {
		return EQOS_ERROR;
	}

	/* Wait for the link: autonegotation completion is what BMSR's link
	 * status bit reports, and it is a latching bit - one read is the
	 * current state. */
	for (tries = 0; tries < RTL8211F_AUTONEG_TRIES; tries++) {
		uint16_t bmsr = 0;

		if (phy_reg_read(dev, MII_BMSR, &bmsr) != EQOS_OK) {
			return EQOS_ERROR;
		}
		if (bmsr & BMSR_LSTATUS) {
			info = rtl8211f_link_info(dev);
			LOG_INFO("%s: link up, %s Mbps %s duplex\n",
				  rtl8211f_name(index),
				  (info.speed == EQOS_SPEED_1G) ? "1000"
				  : (info.speed == EQOS_SPEED_100M) ? "100" : "10",
				  (info.duplex == EQOS_DUPLEX_FULL) ? "full" : "half");
			return EQOS_OK;
		}
		osDelay(1);
	}

	LOG_WARNING("%s: autonegotiation timeout (no link)\n", rtl8211f_name(index));

	return EQOS_ERROR_TIMEOUT;
}

eqos_link_state_t rtl8211f_get_link_state(unsigned int index)
{
	struct rtl8211f_dev *dev;
	uint16_t bmsr = 0;

	if (index >= RTL8211F_PHY_COUNT) {
		return EQOS_LINK_DOWN;
	}
	dev = &phys[index];
	if (!dev->powered) {
		return EQOS_LINK_DOWN;
	}
	if (phy_reg_read(dev, MII_BMSR, &bmsr) != EQOS_OK) {
		return EQOS_LINK_DOWN;
	}

	return (bmsr & BMSR_LSTATUS) ? EQOS_LINK_UP : EQOS_LINK_DOWN;
}

eqos_link_info_t rtl8211f_get_link_info(unsigned int index)
{
	eqos_link_info_t info = { .speed = EQOS_SPEED_10M, .duplex = EQOS_DUPLEX_HALF };

	if (index >= RTL8211F_PHY_COUNT) {
		return info;
	}
	if (rtl8211f_get_link_state(index) != EQOS_LINK_UP) {
		return info;
	}

	return rtl8211f_link_info(&phys[index]);
}

void rtl8211f_get_info(unsigned int index, struct rtl8211f_info *info)
{
	struct rtl8211f_dev *dev;

	if ((index >= RTL8211F_PHY_COUNT) || (info == NULL)) {
		return;
	}
	dev = &phys[index];
	info->addr = dev->addr;
	info->id1 = dev->id1;
	info->id2 = dev->id2;
	info->found = dev->found;
}

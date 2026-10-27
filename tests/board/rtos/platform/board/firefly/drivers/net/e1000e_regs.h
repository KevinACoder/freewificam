/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   e1000e_regs.h
 * @brief  Intel 82574L (PCI 8086:10d3) register facts: offsets and bit
 *         definitions of the BAR0 CSR block this driver touches.
 *
 * DATA ONLY - no declarations, no inline functions (the board/firefly
 * convention; the same shape as board/firefly/v1/board_conf.h). Every value
 * is a datasheet fact (Intel 82574L datasheet, register summary chapter),
 * cross-checked against the register use the reference standalone SDK's
 * driver makes of the same silicon (board evidence: the iperf rounds that
 * driver ran on this NIC).
 */

#ifndef FREEWIFICAM_E1000E_REGS_H
#define FREEWIFICAM_E1000E_REGS_H

/* --- device control --------------------------------------------------------- */

#define E1000_CTRL				0x00000u
#define E1000_CTRL_FD				(1u << 0)	/* full duplex */
#define E1000_CTRL_LRST				(1u << 3)	/* link reset */
#define E1000_CTRL_SLU				(1u << 6)	/* set link up */
#define E1000_CTRL_RST				(1u << 26)	/* device reset */

#define E1000_STATUS				0x00008u
#define E1000_STATUS_LU				(1u << 1)	/* link established */
#define E1000_STATUS_FD				(1u << 0)	/* full duplex */
#define E1000_STATUS_SPEED_SHIFT		6u
#define E1000_STATUS_SPEED_MASK		(3u << E1000_STATUS_SPEED_SHIFT)
/* 00=10M 01=100M 10=1G 11=rsvd - the encoded speed of the link partner */

/* --- EEPROM ------------------------------------------------------------------ */

#define E1000_EERD				0x00014u
#define E1000_EERD_START			(1u << 0)
#define E1000_EERD_DONE				(1u << 1)
#define E1000_EERD_ADDR_SHIFT			2u		/* word address field */
#define E1000_EERD_DATA_SHIFT			16u

/* --- MDI management (PHY access, internal PHY at MDI address 1) -------------- */

#define E1000_MDIC				0x00020u
#define E1000_MDIC_DATA_MASK			0xffffu
#define E1000_MDIC_REGADD_SHIFT		16u
#define E1000_MDIC_PHYADD_SHIFT		21u
#define E1000_MDIC_PHY_ADDR			1u		/* the internal PHY */
#define E1000_MDIC_OP_WRITE			(1u << 26)
#define E1000_MDIC_OP_READ			(2u << 26)
#define E1000_MDIC_READY			(1u << 28)
#define E1000_MDIC_ERROR_MASK			(3u << 30)	/* read/write error bits */

/* --- interrupts (kept masked: this driver runs the data plane polled) -------- */

#define E1000_ICR				0x000c0u	/* read-to-clear */
#define E1000_IMS				0x000d0u	/* write-1-to-set */
#define E1000_IMC				0x000d8u	/* write-1-to-clear */
#define E1000_IMC_ALL				0xffffffffu

/* --- receive ----------------------------------------------------------------- */

#define E1000_RCTL				0x00100u
#define E1000_RCTL_EN				(1u << 1)
#define E1000_RCTL_BAM				(1u << 15)	/* accept broadcast (ARP) */
#define E1000_RCTL_BSIZE_SHIFT		16u
#define E1000_RCTL_BSIZE_2048			(0u << E1000_RCTL_BSIZE_SHIFT)
#define E1000_RCTL_SECRC			(1u << 26)	/* MAC strips the FCS */

#define E1000_RDBAL				0x02800u
#define E1000_RDBAH				0x02804u
#define E1000_RDLEN				0x02808u	/* ring bytes, 128-aligned */
#define E1000_RDH				0x02810u
#define E1000_RDT				0x02818u
#define E1000_RXDCTL				0x02828u
#define E1000_RXDCTL_ENABLE			(1u << 25)

/* --- transmit ---------------------------------------------------------------- */

#define E1000_TCTL				0x00400u
#define E1000_TCTL_EN				(1u << 1)
#define E1000_TCTL_PSP				(1u << 3)	/* pad short packets */
#define E1000_TCTL_CT_SHIFT			4u		/* collision threshold */
#define E1000_TCTL_CT_DEF			(0x0fu << E1000_TCTL_CT_SHIFT)
#define E1000_TCTL_COLD_SHIFT		12u		/* collision distance */
#define E1000_TCTL_COLD_FULL_1G		(0x3fu << E1000_TCTL_COLD_SHIFT)

#define E1000_TIPG				0x00410u
#define E1000_TIPG_IPGT_DEF			(10u << 0)	/* IEEE 802.3 gaps */
#define E1000_TIPG_IPGR1_DEF			(10u << 10)
#define E1000_TIPG_IPGR2_DEF			(10u << 20)

#define E1000_TDBAL				0x03800u
#define E1000_TDBAH				0x03804u
#define E1000_TDLEN				0x03808u
#define E1000_TDH				0x03810u
#define E1000_TDT				0x03818u
#define E1000_TXDCTL				0x03828u
#define E1000_TXDCTL_ENABLE			(1u << 25)

/* --- receive address (the station MAC) --------------------------------------- */

#define E1000_RAL				0x05400u
#define E1000_RAH				0x05404u
#define E1000_RAH_AV				(1u << 31)

/* --- legacy descriptor fields -------------------------------------------------- */

#define E1000_RXD_STA_DD			(1u << 0)	/* descriptor done */
#define E1000_RXD_STA_EOP			(1u << 1)	/* end of packet */
#define E1000_RXD_ERR_ANY			0xffu		/* any error bit rejects */

#define E1000_TXD_CMD_EOP			(1u << 0)
#define E1000_TXD_CMD_IFCS			(1u << 1)	/* insert FCS */
#define E1000_TXD_CMD_RS			(1u << 3)	/* report status */
#define E1000_TXD_STA_DD			(1u << 0)

/* --- IEEE PHY registers (through MDIC, the internal PHY's own space) --------- */

#define E1000_PHY_BMCR				0u		/* control */
#define E1000_PHY_BMCR_ANEG_ENABLE		(1u << 12)
#define E1000_PHY_BMCR_ANEG_RESTART		(1u << 9)
#define E1000_PHY_BMSR				1u		/* status */
#define E1000_PHY_BMSR_ANEG_COMPLETE	(1u << 5)
#define E1000_PHY_PSS				17u		/* PHY specific status */
#define E1000_PHY_PSS_SPEED_SHIFT		14u
#define E1000_PHY_PSS_SPEED_MASK		(3u << E1000_PHY_PSS_SPEED_SHIFT)
#define E1000_PHY_PSS_SPEED_10		(0u << E1000_PHY_PSS_SPEED_SHIFT)
#define E1000_PHY_PSS_SPEED_100		(1u << E1000_PHY_PSS_SPEED_SHIFT)
#define E1000_PHY_PSS_SPEED_1000		(2u << E1000_PHY_PSS_SPEED_SHIFT)
#define E1000_PHY_PSS_FULL_DUPLEX		(1u << 13)
#define E1000_PHY_PSS_RT_LINK			(1u << 10)	/* real-time link */

#endif /* FREEWIFICAM_E1000E_REGS_H */

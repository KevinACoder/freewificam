/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/ssb/ssb_regs.h
 * @brief   The SSB (Sonics Silicon Backplane) register offsets and field bits a
 *          Broadcom chip's core bring-up reads and writes.
 *
 * @design declaration-only compatibility face. The interface
 *         (include/linux/ssb/ssb_regs.h) carries the whole backplane map: every
 *         core's register window, the PCI/SPI wrappers and the ID registers of
 *         each core. What brcmfmac's chip.c names is the target-state half of
 *         the backplane - the reset/clock/reject handshake a core is brought out
 *         of reset with (SSB_TMSLOW, SSB_TMSHIGH, SSB_IMSTATE) and the core-id
 *         decode bits (SSB_IDLOW, SSB_IDHIGH) - so that is the subset carried
 *         here, with the interface's values. A number arrives when a compiled
 *         consumer names it.
 * @date    19.11.2026
 */

#ifndef _LINUXKPI_LINUX_SSB_SSB_REGS_H_
#define _LINUXKPI_LINUX_SSB_SSB_REGS_H_

#include "../../../linuxkpi_core.h"

/* [LNX] include/linux/ssb/ssb_regs.h:138: the core-id low register's initiator
 * bit, which tells whether a core drives the backplane (the reset handshake
 * waits for it). */
#define	SSB_IDLOW_INITIATOR	0x00000080

/* [LNX] include/linux/ssb/ssb_regs.h:80-90: the initiator-agent state register
 * and the bits the reset/clock handshake samples. */
#define	SSB_IMSTATE		0x0F90		/* SB Initiator Agent State */
#define	SSB_IMSTATE_IBE	0x00020000	/* In Band Error */
#define	SSB_IMSTATE_TO		0x00040000	/* Timeout */
#define	SSB_IMSTATE_BUSY	0x01800000	/* Busy (backplane rev >= 2.3) */
#define	SSB_IMSTATE_REJECT	0x02000000	/* Reject (backplane rev >= 2.3) */

/* [LNX] include/linux/ssb/ssb_regs.h:99-106: the target-state low register and
 * its control bits. A core is reset by writing RESET, clocked by CLOCK, and a
 * core that rejects the request sets REJECT. */
#define	SSB_TMSLOW		0x0F98		/* SB Target State Low */
#define	SSB_TMSLOW_RESET	0x00000001	/* Reset */
#define	SSB_TMSLOW_REJECT	0x00000002	/* Reject (standard backplane) */
#define	SSB_TMSLOW_CLOCK	0x00010000	/* Clock Enable */
#define	SSB_TMSLOW_FGC		0x00020000	/* Force Gated Clocks On */

/* [LNX] include/linux/ssb/ssb_regs.h:107-117: the target-state high register and
 * the error/busy bits that end the handshake. */
#define	SSB_TMSHIGH		0x0F9C		/* SB Target State High */
#define	SSB_TMSHIGH_SERR	0x00000001	/* S-error */
#define	SSB_TMSHIGH_BUSY	0x00000004	/* Busy */

/* [LNX] include/linux/ssb/ssb_regs.h:158-162: the core-id high register's
 * revision-code field, which chip.c decodes into a chip revision. */
#define	SSB_IDHIGH_RCLO	0x0000000F	/* Revision Code (low part) */
#define	SSB_IDHIGH_RCHI	0x00007000	/* Revision Code (high part) */
#define	SSB_IDHIGH_RCHI_SHIFT	8		/* yes, shift 8 is right */

#endif /* _LINUXKPI_LINUX_SSB_SSB_REGS_H_ */

/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/if_arp.h
 * @brief   The kernel-side ARP face: the interface's kernel header, which is the
 *          include of the uapi one.
 *
 * @design the interface splits the ARP vocabulary in two files: the numbers and
 *         the wire header live in include/uapi/linux/if_arp.h, and
 *         include/linux/if_arp.h includes that file and adds the in-kernel
 *         pieces. This port carries the numbers once, in
 *         include/uapi/linux/if_arp.h, because both spellings have compiled
 *         consumers (mac80211/cfg80211 include this header; brcmfmac's
 *         cfg80211.c includes the uapi name). Keeping the definitions in the
 *         uapi file and this file as its include is the interface's own shape,
 *         so a name reached by either spelling has exactly one definition.
 *
 *         The in-kernel additions upstream are the neigh/statistics helpers and
 *         the ARPHRD_* names beyond the network-device set; no compiled unit
 *         here names one (the stack's arp.h face carries the neighbour
 *         record), so this face is the uapi include and nothing else.
 * @date    19.11.2026
 */

#ifndef _LINUXKPI_LINUX_IF_ARP_H_
#define _LINUXKPI_LINUX_IF_ARP_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/if_arp.h: the whole file is the uapi include plus the
 * in-kernel helpers; the numbers and the wire header are the uapi file's. */
#include <uapi/linux/if_arp.h>

#endif /* _LINUXKPI_LINUX_IF_ARP_H_ */

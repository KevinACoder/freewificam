/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/ethtool.h
 * @brief   The ethtool interface a network driver's configuration path needs: the
 *          driver-information record it fills, the statistics record it is handed, the
 *          string-set identifier its counters are addressed by, and the operation
 *          table it installs.
 *
 * @design declaration-only compatibility face, grown per consumer. The interface
 *         splits this surface between include/uapi/linux/ethtool.h (the records that
 *         cross to user space) and include/linux/ethtool.h (the kernel-side operation
 *         table and entry points); the face states both parts in one header under the
 *         name the compiled sources include, transcribed field for field and value for
 *         value. What is carried is what the compiled set names - the driver-information
 *         record, the statistics record, the string-set enumeration, the counter-name
 *         length and the five callbacks the driver installs in its operation table. The
 *         interface's operation table has some seventy members and its records number in
 *         the dozens; none of the rest is named here, so none of it is carried (an
 *         undeclared member cannot be assigned by accident), and they arrive with a
 *         caller that names them.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_ETHTOOL_H_
#define _LINUXKPI_LINUX_ETHTOOL_H_

#include "../../linuxkpi_core.h"

struct net_device;

/* [LNX] include/uapi/linux/ethtool.h:148-150 */
#define	ETHTOOL_FWVERS_LEN	32
#define	ETHTOOL_BUSINFO_LEN	32
#define	ETHTOOL_EROMVERS_LEN	32

/* [LNX] include/uapi/linux/ethtool.h:659 */
#define	ETH_GSTRING_LEN		32

/* [LNX] include/uapi/linux/ethtool.h (enum ethtool_stringset): the string-set
 * identifiers. The numbering is a user-space contract (it is passed through the
 * interface's ioctl records), so the enumeration is carried whole and in order rather
 * than trimmed to the one member this face names. */
enum ethtool_stringset {
	ETH_SS_TEST		= 0,
	ETH_SS_STATS,
	ETH_SS_PRIV_FLAGS,
	ETH_SS_NTUPLE_FILTERS,
	ETH_SS_FEATURES,
	ETH_SS_RSS_HASH_FUNCS,
	ETH_SS_TUNABLES,
	ETH_SS_PHY_STATS,
	ETH_SS_PHY_TUNABLES,
	ETH_SS_LINK_MODES,
	ETH_SS_MSG_CLASSES,
	ETH_SS_WOL_MODES,
	ETH_SS_SOF_TIMESTAMPING,
	ETH_SS_TS_TX_TYPES,
	ETH_SS_TS_RX_FILTERS,
	ETH_SS_UDP_TUNNEL_TYPES,
	ETH_SS_STATS_STD,
	ETH_SS_STATS_ETH_PHY,
	ETH_SS_STATS_ETH_MAC,
	ETH_SS_STATS_ETH_CTRL,
	ETH_SS_STATS_RMON,
	ETH_SS_STATS_PHY,
	ETH_SS_TS_FLAGS,

	/* add new constants above here */
	ETH_SS_COUNT
};

/* [LNX] include/uapi/linux/ethtool.h (struct ethtool_drvinfo): the driver-information
 * record. The fixed-size name fields are the interface's sizes, and a driver fills them
 * with bounded copies, so the sizes are contract. */
struct ethtool_drvinfo {
	__u32	cmd;
	char	driver[32];
	char	version[32];
	char	fw_version[ETHTOOL_FWVERS_LEN];
	char	bus_info[ETHTOOL_BUSINFO_LEN];
	char	erom_version[ETHTOOL_EROMVERS_LEN];
	char	reserved2[12];
	__u32	n_priv_flags;
	__u32	n_stats;
	__u32	testinfo_len;
	__u32	eedump_len;
	__u32	regdump_len;
};

/* [LNX] include/uapi/linux/ethtool.h (struct ethtool_stats): the statistics-request
 * header; the counter values follow it as the flexible array member. */
struct ethtool_stats {
	__u32	cmd;
	__u32	n_stats;
	__u64	data[];
};

/*
 * [LNX] include/linux/ethtool.h:1206-1462 (struct ethtool_ops): the operation table.
 * The interface's table opens with a block of capability words and continues with some
 * seventy callbacks; the five carried here are the ones the compiled set installs, at
 * the interface's exact signatures and in the interface's relative order. Nothing else
 * is named, so nothing else is declared.
 */
struct ethtool_ops {
	void	(*get_drvinfo)(struct net_device *, struct ethtool_drvinfo *);
	u32	(*get_link)(struct net_device *);
	void	(*get_strings)(struct net_device *, u32 stringset, u8 *);
	void	(*get_ethtool_stats)(struct net_device *,
				     struct ethtool_stats *, u64 *);
	int	(*get_sset_count)(struct net_device *, int);
};

/* [LNX] include/linux/ethtool.h:1470: the generic link-state callback a driver without
 * its own link query installs. Declared as an entry point: its body reads the device's
 * carrier state, which on this port is the net-device seam's business, like the other
 * device entry points this face declares. */
u32 ethtool_op_get_link(struct net_device *dev);

#endif /* _LINUXKPI_LINUX_ETHTOOL_H_ */

/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/if.h
 * @brief   The interface-name size the wireless request records are built on.
 *
 * @design declaration-only compatibility face, grown per consumer. The interface
 *         (include/uapi/linux/if.h, included through include/linux/if.h) carries the
 *         whole interface-configuration surface - the request records, the flags, the
 *         name size. What the compiled set reaches is the name size: this project
 *         presents its interfaces through lwIP rather than through Linux net devices, so
 *         the records that carry one are described by the wireless face
 *         (components/linuxkpi/include/linux/wireless.h), not by a net-device face here.
 *         The value is the interface's. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_IF_H_
#define _LINUXKPI_LINUX_IF_H_

#include "../../linuxkpi_core.h"

#include <linux/socket.h>

/* [LNX] include/uapi/linux/if.h:33 */
#ifndef IFNAMSIZ
#define	IFNAMSIZ	16
#endif

/*
 * [LNX] include/uapi/linux/if.h:85-105 (enum net_device_flags): the interface's flag
 * word, which is `struct net_device::flags`. The driver reads IFF_UP to ask whether the
 * interface was brought up; the rest of the enumeration is carried with it because
 * these bits go on the wire through the ioctl request records and a partial copy of a
 * flag space is an invitation to a wrong bit. The interface re-#defines each enumerator
 * to itself; that indirection exists for its uapi/libc boundary and is not carried.
 */
enum net_device_flags {
	IFF_UP				= 1 << 0,	/* sysfs */
	IFF_BROADCAST			= 1 << 1,	/* volatile */
	IFF_DEBUG			= 1 << 2,	/* sysfs */
	IFF_LOOPBACK			= 1 << 3,	/* volatile */
	IFF_POINTOPOINT			= 1 << 4,	/* volatile */
	IFF_NOTRAILERS			= 1 << 5,	/* sysfs */
	IFF_RUNNING			= 1 << 6,	/* volatile */
	IFF_NOARP			= 1 << 7,	/* sysfs */
	IFF_PROMISC			= 1 << 8,	/* sysfs */
	IFF_ALLMULTI			= 1 << 9,	/* sysfs */
	IFF_MASTER			= 1 << 10,	/* volatile */
	IFF_SLAVE			= 1 << 11,	/* volatile */
	IFF_MULTICAST			= 1 << 12,	/* sysfs */
	IFF_PORTSEL			= 1 << 13,	/* sysfs */
	IFF_AUTOMEDIA			= 1 << 14,	/* sysfs */
	IFF_DYNAMIC			= 1 << 15,	/* sysfs */
	IFF_LOWER_UP			= 1 << 16,	/* volatile */
	IFF_DORMANT			= 1 << 17,	/* volatile */
	IFF_ECHO			= 1 << 18,	/* volatile */
};

/* [LNX] include/uapi/linux/if.h: the hardware-parameter record. */
struct ifmap {
	unsigned long	mem_start;
	unsigned long	mem_end;
	unsigned short	base_addr;
	unsigned char	irq;
	unsigned char	dma;
	unsigned char	port;
	/* 3 bytes spare */
};

/*
 * The protocol-specific records struct if_settings points at live in the interface's
 * hdlc ioctl header (include/uapi/linux/hdlc/ioctl.h) and are only ever reached through
 * pointers, so this face declares the tags without their bodies: the pointer union below
 * has the interface's size either way, and a caller that dereferences one of them must
 * bring the record.
 */
struct raw_hdlc_proto;
struct cisco_proto;
struct fr_proto;
struct fr_proto_pvc;
struct fr_proto_pvc_info;
struct x25_hdlc_proto;
struct sync_serial_settings;
struct te1_settings;

/* [LNX] include/uapi/linux/if.h: the interface-settings record. */
struct if_settings {
	unsigned int	type;	/* Type of physical device or protocol */
	unsigned int	size;	/* Size of the data allocated by the caller */
	union {
		struct raw_hdlc_proto __user		*raw_hdlc;
		struct cisco_proto __user		*cisco;
		struct fr_proto __user			*fr;
		struct fr_proto_pvc __user		*fr_pvc;
		struct fr_proto_pvc_info __user		*fr_pvc_info;
		struct x25_hdlc_proto __user		*x25;

		/* interface settings */
		struct sync_serial_settings __user	*sync;
		struct te1_settings __user		*te1;
	} ifs_ifsu;
};

/* [LNX] include/uapi/linux/if.h: the interface-configuration request record, the same
 * size as `struct iwreq` (16 octets of name plus a 24-octet union). */
struct ifreq {
#define	IFHWADDRLEN	6
	union {
		char	ifrn_name[IFNAMSIZ];		/* if name, e.g. "en0" */
	} ifr_ifrn;

	union {
		struct sockaddr	ifru_addr;
		struct sockaddr	ifru_dstaddr;
		struct sockaddr	ifru_broadaddr;
		struct sockaddr	ifru_netmask;
		struct sockaddr	ifru_hwaddr;
		short		ifru_flags;
		int		ifru_ivalue;
		int		ifru_mtu;
		struct ifmap	ifru_map;
		char		ifru_slave[IFNAMSIZ];	/* Just fits the size */
		char		ifru_newname[IFNAMSIZ];
		void __user	*ifru_data;
		struct if_settings ifru_settings;
	} ifr_ifru;
};

#endif /* _LINUXKPI_LINUX_IF_H_ */

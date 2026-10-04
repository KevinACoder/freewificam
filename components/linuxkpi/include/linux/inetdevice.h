#ifndef _LINUXKPI_LINUX_INETDEVICE_H_
#define _LINUXKPI_LINUX_INETDEVICE_H_
/* [S5b-W1] rtl8851bu funnel header (osdep_service_linux.h:32): the IPv4
 * address-notifier compile face (os_intfs.c rtw_inetaddr_notifier_call).
 * The notifiers are never armed (no-op registers, like the netdevice pair
 * in linuxkpi_core.h), so the struct face only needs to parse. */
struct in_device {
	struct net_device	*dev;
};

struct in_ifaddr {
	struct in_device	*ifa_dev;
	unsigned int		ifa_address;	/* __be32 */
	const char		*ifa_label;
};

#define	register_inetaddr_notifier(nb)		((void)(nb), 0)
#define	unregister_inetaddr_notifier(nb)	((void)(nb), 0)
#endif

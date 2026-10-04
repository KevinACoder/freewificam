#ifndef _LINUXKPI_NET_ADDRCONF_H_
#define _LINUXKPI_NET_ADDRCONF_H_
/* [S5b-W1] rtl8851bu funnel header (osdep_service_linux.h:49): the IPv6
 * address-notifier compile face (os_intfs.c rtw_inet6addr_notifier_call).
 * The notifiers are never armed (no-op registers); the struct face only
 * needs to parse (idev->dev walk + 16-byte addr copy). */
struct in6_addr {
	unsigned char	s6_addr[16];
};

struct inet6_dev {
	struct net_device	*dev;
};

struct inet6_ifaddr {
	struct inet6_dev	*idev;
	struct in6_addr		addr;
};

#define	register_inet6addr_notifier(nb)		((void)(nb), 0)
#define	unregister_inet6addr_notifier(nb)	((void)(nb), 0)
#endif

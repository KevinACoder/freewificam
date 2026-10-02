#ifndef _LINUXKPI_LINUX_IF_H_
#define _LINUXKPI_LINUX_IF_H_
/* [S6] B4: the ifreq/sockaddr faces the ndo_do_ioctl signature and the
 * mac-address path need; ioctl commands are stamps (no ioctl layer). */
#include "../../linuxkpi_core.h"

typedef unsigned short	sa_family_t;

struct sockaddr {
	sa_family_t	sa_family;
	char		sa_data[14];
};

struct ifreq {
	char	ifr_name[IFNAMSIZ];
	union {
		struct sockaddr	ifru_addr;
		struct sockaddr	ifru_dstaddr;
		char		ifru_data[24];
	} ifr_ifru;
};

#define	ifr_addr	ifr_ifru.ifru_addr
#define	ifr_dstaddr	ifr_ifru.ifru_dstaddr
#define	ifr_data	ifr_ifru.ifru_data

#define	SIOCGIFFLAGS	0x8913
#define	SIOCSIFFLAGS	0x8914
#define	SIOCGIFADDR	0x8915
#define	SIOCSIFADDR	0x8916
#define	SIOCGIFMTU	0x8921
#define	SIOCSIFMTU	0x8922
#define	SIOCSIFHWADDR	0x8924
#define	SIOCGIFHWADDR	0x8927
#define	SIOCGIFINDEX	0x8933
#define	SIOCGIFNAME	0x8910
#endif

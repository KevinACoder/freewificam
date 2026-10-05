/*
 * @file
 * @brief Interface ioctl numbers used by the imported code.
 */

#ifndef _COMPAT_SYS_SOCKIO_H_
#define _COMPAT_SYS_SOCKIO_H_

#include <sys/ioccom.h>

#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

#define SIOCGIFADDR _IOWR('i', 33, struct ifreq)
#define SIOCSIFADDR _IOW('i', 12, struct ifreq)
#define SIOCGIFFLAGS _IOWR('i', 17, struct ifreq)
#define SIOCSIFFLAGS _IOW('i', 16, struct ifreq)
#define SIOCGIFMTU _IOWR('i', 51, struct ifreq)
#define SIOCSIFMTU _IOW('i', 127, struct ifreq)
#define SIOCGIFMEDIA _IOWR('i', 56, struct ifmediareq)
#define SIOCADDMULTI _IOW('i', 49, struct ifreq)
#define SIOCDELMULTI _IOW('i', 50, struct ifreq)
#define SIOCSIFMEDIA _IOWR('i', 55, struct ifreq)

struct ifmediareq {
	char ifm_name[IFNAMSIZ];
	int ifm_current;
	int ifm_mask;
	int ifm_status;
	int ifm_active;
	int ifm_count;
	int ifm_pad;
};

struct ifreq {
	char ifr_name[IFNAMSIZ];
	union {
		void *ifru_data;
		unsigned int ifru_flags;
	} ifr_ifru;
};
#define ifr_flags ifr_ifru.ifru_flags
#define ifr_data ifr_ifru.ifru_data

#endif /* _COMPAT_SYS_SOCKIO_H_ */

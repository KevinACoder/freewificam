/*
 * @file
 * @brief port_config.h plus the BSD ifnet pre-emption.
 *
 * Forced-include for the translation units that live in the BSD world
 * (net80211 core, chip drivers, the BSD shim layer). The embox include
 * tree owns <net/if.h>; defining its guard first and pulling in the
 * compat version keeps those units on the BSD ifnet layout.
 */

#ifndef _NET80211_PORT_CONFIG_BSD_H_
#define _NET80211_PORT_CONFIG_BSD_H_

#include "port_config.h"

#include "../compat/netbsd/sys/cdefs.h"

/* Pre-empt the embox headers that would shadow or clash with the
 * compat ones; claims must precede every include below. */
#ifndef NET_IF_H_
#define NET_IF_H_
#endif
#ifndef COMPAT_POSIX_SYS_PARAM_H_
#define COMPAT_POSIX_SYS_PARAM_H_
#endif
#ifndef COMPAT_POSIX_NETINET_IN_H_
#define COMPAT_POSIX_NETINET_IN_H_
#endif
#ifndef COMPAT_POSIX_ENDIAN_H_
#define COMPAT_POSIX_ENDIAN_H_
#endif
#ifndef SRC_COMPAT_BSD_INCLUDE_SYS_ENDIAN_H_
#define SRC_COMPAT_BSD_INCLUDE_SYS_ENDIAN_H_
#endif
#ifndef COMPAT_LINUX_SYS_IOCTL_H_
#define COMPAT_LINUX_SYS_IOCTL_H_
#endif

#define NET_IF_H_
#include "../compat/netbsd/net/if.h"

#include "../compat/netbsd/sys/param.h"
#include "../compat/netbsd/netinet/in.h"

/* The compat layer implements the NetBSD kernel semantics the imported
 * sources select with __NetBSD__; claim it before any of them loads. */
#ifndef __NetBSD__
#define __NetBSD__ 1
#endif

/* the embox <sys/sysctl.h> is an empty stub; provide the NetBSD
 * sysctl vocabulary so the glue compiles out cleanly. The whole tree
 * still compiles out: sysctl_createv always fails and callers take
 * their error paths. */
struct sysctllog { int unused; };
typedef struct sysctllog sysctllog;
struct sysctlnode {
	unsigned sysctl_num;
	void *sysctl_data;
};
#define SYSCTL_SETUP(name, desc) static void name(void)
#define SYSCTL_DESCR(desc) (desc)
#define SYSCTLFN_PROTO struct sysctlnode *rnode, void *newp
#define SYSCTLFN_ARGS SYSCTLFN_PROTO
#define SYSCTLFN_CALL(nodep) (nodep), NULL
#define CTL_EOL 0
#define CTL_CREATE 0
#define CTLFLAG_READWRITE 0
#define CTLFLAG_PERMANENT 0
#define CTLTYPE_INT 0
#define CTLTYPE_NODE 0
#define sysctl_createv(...) (-1)
static inline int sysctl_lookup(struct sysctlnode *node, void *newp) {
	(void) node;
	(void) newp;
	return 0;
}

/* the net80211 sysctl configuration tree is compiled out */
#define IEEE80211_PORT_NO_SYSCTL 1

/* network errno set (values match the embox libc errno.h) */
#ifndef ETIMEDOUT
#define ETIMEDOUT 360
#endif
#ifndef ENETRESET
#define ENETRESET 352
#endif
#ifndef ENOBUFS
#define ENOBUFS 355
#endif
#ifndef EIO
#define EIO 5
#endif

void panic(const char *fmt, ...) __attribute__((__format__(__printf__,1,2)));


#endif /* _NET80211_PORT_CONFIG_BSD_H_ */

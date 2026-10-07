/*
 * Host-only preemption for the glibc headers that must not be mixed into the
 * BSD-world units, and for the compat headers those units must get instead.
 *
 * The component's port/port_config_bsd.h preempts the target side by claiming the
 * target's guards and including the compat/netbsd versions first. A glibc host has
 * more headers than the target libc, with different guards, so two things break
 * without this prelude:
 *
 *   * glibc <sys/queue.h> shares the NetBSD guard name (_SYS_QUEUE_H_) but has no
 *     TAILQ_FOREACH_SAFE, which the net80211 core uses. Taking the compat copy first
 *     also keeps its __NetBSD__ branch (it asks for <sys/null.h>, a target-only
 *     header) switched off, because this header runs before port_config_bsd.h.
 *   * glibc <net/if.h>, <sys/param.h> and <netinet/in.h> are reached through their
 *     own guards even though port_config_bsd.h already pulled the compat versions
 *     in; the mix then breaks on duplicate enums/structs. Claiming the glibc guards
 *     makes those includes no-ops.
 *
 * This is host-environment adaptation for the compile contract only; the component
 * itself stays byte-identical and its board build is untouched. Include this header
 * before port/port_config_bsd.h.
 */
#ifndef NET80211_HOST_PREEMPT_H_
#define NET80211_HOST_PREEMPT_H_

#include <stddef.h>
#include <stdint.h>

/* glibc's <endian.h> first: it also defines BYTE_ORDER, which the NetBSD sources
 * test, and it must win that job. The macros compat/netbsd/sys/endian.h redefines
 * with equivalent values are dropped here so the compat include (later, through
 * port_config_bsd.h) is silent instead of a redefinition - the NetBSD copy still
 * wins the macros, which is what the imported sources expect. */
#include <endian.h>
#undef htobe16
#undef htobe32
#undef htobe64
#undef htole16
#undef htole32
#undef htole64
#undef be16toh
#undef be32toh
#undef be64toh
#undef le16toh
#undef le32toh
#undef le64toh
#undef htons
#undef htonl
#undef ntohs
#undef ntohl

/* Same treatment for the one macro compat/netbsd/sys/cdefs.h redefines. */
#include <sys/cdefs.h>
#undef __CONCAT

#include "../../../components/net80211/compat/netbsd/sys/queue.h"
#include "../../../components/net80211/compat/netbsd/net/route.h"

#define _NET_IF_H 1
#define _SYS_PARAM_H 1
#define _NETINET_IN_H 1
#define _NET_ROUTE_H 1

#endif /* NET80211_HOST_PREEMPT_H_ */

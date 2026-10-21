/*
 * @file
 * @brief route(4) message constants; no routing tables here.
 */

#ifndef _COMPAT_NET_ROUTE_H_
#define _COMPAT_NET_ROUTE_H_

#include <sys/socket.h>

#define RTM_IEEE80211_ASSOC 100
#define RTM_IEEE80211_REASSOC 101
#define RTM_IEEE80211_DISASSOC 102
#define RTM_IEEE80211_JOIN 103
#define RTM_IEEE80211_LEAVE 104
#define RTM_IEEE80211_SCAN 105
#define RTM_IEEE80211_REPLAY 106
#define RTM_IEEE80211_MICHAEL 107
#define RTM_IEEE80211_REJOIN 108

struct if_announcemsghdr {
	unsigned short rtm_msglen;
	uint8_t rtm_version;
	uint8_t rtm_type;
	unsigned short rtm_index;
	uint16_t ifan_what;
};

/* route(4) socket emitters: no routing socket on the ports */
struct ifnet;
void rt_ieee80211msg(struct ifnet *ifp, int what, const void *data, size_t len);
#define rt_ifmsg(ifp) ((void) (ifp))
#define rt_ifannouncemsg(ifp, what, which) ((void) (ifp))

#endif /* _COMPAT_NET_ROUTE_H_ */

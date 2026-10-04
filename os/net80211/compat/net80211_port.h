/*
 * net80211_port.h - the first-party seam out of the vendored net80211
 * world (S8 net80211-adapt spec FR-7/FR-12 shape).
 *
 * Everything ieee80211_deliver_data hands up crosses if_percpuq_enqueue
 * (data and EAPOL frames, one channel); everything the protocol state
 * machine wants to report crosses rt_ieee80211msg. The bridge that owns
 * the downstream world - lwip netif in S8-4, the mac80211-side world in
 * S9-W3 - registers these sinks and stays out of the vendored files.
 */

#ifndef NET80211_PORT_H
#define NET80211_PORT_H

#include <stddef.h>
#include <stdint.h>

/* one frame handed up the RX seam; linear bytes, freed by the callee's
 * caller (the mbuf is released right after the sink returns) */
typedef void (*net80211_port_rx_fn)(const void *buf, size_t len,
    void *arg);

enum net80211_port_event {
	NET80211_PORT_SCAN_DONE,
	NET80211_PORT_ASSOC,
	NET80211_PORT_DISASSOC,
};

typedef void (*net80211_port_event_fn)(enum net80211_port_event event,
    const void *mac, void *arg);

void net80211_port_set_data_rx(net80211_port_rx_fn fn, void *arg);
void net80211_port_set_eapol_rx(net80211_port_rx_fn fn, void *arg);
void net80211_port_set_event_handler(net80211_port_event_fn fn, void *arg);

/* health counters for the board rounds */
extern unsigned int net80211_port_rx_drops;
extern unsigned int wlan_mbuf_pool_low;
extern unsigned int wlan_mbuf_pool_short;
extern unsigned int wlan_mbuf_pool_dups;

/* the splnet/IFQ serializer the compat world funnels through */
void net80211_serializer_lock(void);
void net80211_serializer_unlock(void);

#endif /* NET80211_PORT_H */

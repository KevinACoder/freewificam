/*
 * net80211_ifnet.c - ifnet(9)/ifmedia(9) shells for the vendored
 * net80211 world.
 *
 * The port owns the interface presentation; these shells keep the
 * imported protocol core working: counters, flags, the legacy packet
 * queue and the media word list. if_percpuq_enqueue is the RX seam the
 * spec (S8 FR-7) fixes: everything ieee80211_deliver_data hands up
 * arrives here, and the first-party bridge (S8-4 lwip, S9-W3 scan
 * world) registers the sink that consumes it.
 */

#include <string.h>

#include "minilibc.h"

#include "sys/systm.h"
#include "sys/mbuf.h"
#include "sys/endian.h"
#include "sys/mutex.h"
#include "net/if.h"
#include "net/if_ether.h"
#include "net/if_media.h"
#include "net/route.h"

#include "net80211_port.h"

static unsigned int if_index_counter = 1;

int
if_initialize(struct ifnet *ifp)
{
	memset(&ifp->if_data, 0, sizeof(ifp->if_data));
	ifp->if_index = if_index_counter++;
	return 0;
}

void
if_register(struct ifnet *ifp)
{
	(void)ifp;
}

void
if_detach(struct ifnet *ifp)
{
	(void)ifp;
}

void
if_deactivate(struct ifnet *ifp)
{
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
}

void *
if_percpuq_create(struct ifnet *ifp)
{
	(void)ifp;
	/* single-cpu shell: the queue is only a token */
	return ifp;
}

/* --- RX seam (S8 FR-7): the registered sink or a counted drop ------------- */

static net80211_port_rx_fn net80211_port_data_rx;
static void *net80211_port_data_arg;
static net80211_port_rx_fn net80211_port_eapol_rx;
static void *net80211_port_eapol_arg;
static net80211_port_event_fn net80211_port_event;
static void *net80211_port_event_arg;
unsigned int net80211_port_rx_drops;
unsigned int pq_multi_seg;

void
net80211_port_set_data_rx(net80211_port_rx_fn fn, void *arg)
{
	net80211_port_data_arg = arg;
	net80211_port_data_rx = fn;
}

void
net80211_port_set_eapol_rx(net80211_port_rx_fn fn, void *arg)
{
	net80211_port_eapol_arg = arg;
	net80211_port_eapol_rx = fn;
}

void
net80211_port_set_event_handler(net80211_port_event_fn fn, void *arg)
{
	net80211_port_event_arg = arg;
	net80211_port_event = fn;
}

void
if_percpuq_enqueue(void *pq, struct mbuf *m)
{
	const struct ether_header *eh;

	if (m == NULL) {
		return;
	}
	/* the deliver path builds single-segment frames from the pool
	 * mbufs; a chain here would mean an encap/decap bug upstream -
	 * count it and drop instead of handing a truncated frame on */
	if (m->m_next != NULL || (m->m_flags & M_PKTHDR) == 0 ||
	    m->m_pkthdr.len != m->m_len) {
		pq_multi_seg++;
		m_freem(m);
		return;
	}
	eh = mtod(m, const struct ether_header *);

	if (ntohs(eh->ether_type) == ETHERTYPE_PAE &&
	    net80211_port_eapol_rx != NULL) {
		/* whole ethernet frame: the consumer derives src + 802.1X
		 * payload (the wpa l2 face needs the source MAC; the old
		 * payload-only contract carried no address) */
		net80211_port_eapol_rx(mtod(m, const uint8_t *), m->m_len,
		    net80211_port_eapol_arg);
		m_freem(m);
		return;
	}
	if (net80211_port_data_rx != NULL) {
		net80211_port_data_rx(mtod(m, const uint8_t *), m->m_len,
		    net80211_port_data_arg);
	} else {
		net80211_port_rx_drops++;
	}
	m_freem(m);
}

void
rt_ieee80211msg(struct ifnet *ifp, int what, const void *data, size_t len)
{
	enum net80211_port_event event;

	if (net80211_port_event == NULL) {
		return;
	}
	switch (what) {
	case RTM_IEEE80211_SCAN:
		event = NET80211_PORT_SCAN_DONE;
		break;
	case RTM_IEEE80211_ASSOC:
	case RTM_IEEE80211_REASSOC:
		event = NET80211_PORT_ASSOC;
		break;
	case RTM_IEEE80211_DISASSOC:
		event = NET80211_PORT_DISASSOC;
		break;
	default:
		return;
	}
	net80211_port_event(event, len >= ETHER_ADDR_LEN ? data : NULL,
	    net80211_port_event_arg);
}

void
if_start_lock(struct ifnet *ifp)
{
	/* The transmit path is reached from the state-machine worker and
	 * completion contexts alike; the imported code relies on splnet()
	 * serialization for queue mutations, and that discipline is the
	 * compat serializer. */
	net80211_serializer_lock();
	if (ifp->if_start != NULL) {
		ifp->if_start(ifp);
	}
	net80211_serializer_unlock();
}

void
if_link_state_change(struct ifnet *ifp, int link_state)
{
	(void)ifp;
	(void)link_state;
}

const uint8_t etherbroadcastaddr[ETHER_ADDR_LEN] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

const uint8_t etheripaddr[ETHER_ADDR_LEN] = { 0 };

/* --- ifmedia shells -------------------------------------------------------- */

#define IFMEDIA_POOL_N 32
static struct ifmedia_entry ifmedia_pool[IFMEDIA_POOL_N];
static unsigned int ifmedia_pool_used;

static struct ifmedia_entry *
ifmedia_entry_alloc(void)
{
	struct ifmedia_entry *e;

	if (ifmedia_pool_used >= IFMEDIA_POOL_N) {
		return NULL;
	}
	e = &ifmedia_pool[ifmedia_pool_used++];
	memset(e, 0, sizeof(*e));
	return e;
}

void
ifmedia_init_with_lock(struct ifmedia *ifm, int dontcare_mask,
    ifm_change_cb_t change, ifm_stat_cb_t stat, kmutex_t *lock)
{
	(void)dontcare_mask;
	(void)change;
	(void)stat;
	(void)lock;
	memset(ifm, 0, sizeof(*ifm));
	TAILQ_INIT(&ifm->ifm_list);
}

void
ifmedia_add(struct ifmedia *ifm, int mword, int data, void *aux)
{
	struct ifmedia_entry *e = ifmedia_entry_alloc();

	if (e == NULL) {
		return;
	}
	e->ifm_media = (u_int)mword;
	e->ifm_data = (u_int)data;
	e->ifm_aux = aux;
	TAILQ_INSERT_TAIL(&ifm->ifm_list, e, ifm_list);
	if (ifm->ifm_cur == NULL) {
		ifm->ifm_cur = e;
	}
}

void
ifmedia_set(struct ifmedia *ifm, int mword)
{
	struct ifmedia_entry *e;

	TAILQ_FOREACH(e, &ifm->ifm_list, ifm_list) {
		if ((int)e->ifm_media == mword) {
			ifm->ifm_cur = e;
			return;
		}
	}
}

void
ifmedia_fini(struct ifmedia *ifm)
{
	(void)ifm;
	/* static pool: entries are reclaimed with the pool, not per-ifnet */
	TAILQ_INIT(&ifm->ifm_list);
	ifm->ifm_cur = NULL;
}

struct ifmedia_entry *
ifmedia_match_locked(struct ifmedia *ifm, u_int match, u_int mask)
{
	struct ifmedia_entry *e;

	TAILQ_FOREACH(e, &ifm->ifm_list, ifm_list) {
		if ((e->ifm_media & mask) == (match & mask)) {
			return e;
		}
	}
	return NULL;
}

struct ifmedia_entry *
ifmedia_match(struct ifmedia *ifm, u_int match, u_int mask)
{
	struct ifmedia_entry *e;

	net80211_serializer_lock();
	e = ifmedia_match_locked(ifm, match, mask);
	net80211_serializer_unlock();
	return e;
}

int
ifmedia_ioctl(struct ifnet *ifp, struct ifreq *ifr, struct ifmedia *ifm,
    unsigned long cmd)
{
	(void)ifp;
	(void)ifr;
	(void)ifm;
	(void)cmd;
	return ENOTTY;
}

uint64_t
ifmedia_baudrate(int mword)
{
	(void)mword;
	return 54000000;
}

void
if_set_sadl(struct ifnet *ifp, const void *lla, unsigned char salen,
    int is_unix)
{
	struct sockaddr_dl *sdl = ifp->if_sadl;

	(void)is_unix; /* the port only carries sockaddr_dl */
	if (sdl == NULL) {
		sdl = &ifp->if_sadl_storage;
		memset(sdl, 0, sizeof(*sdl));
		sdl->sdl_len = (uint8_t)sizeof(*sdl);
		sdl->sdl_family = 18; /* AF_LINK, kept in sync with socket.h */
		ifp->if_sadl = sdl;
	}
	if (lla != NULL) {
		memcpy(LLADDR(sdl), lla, salen);
		sdl->sdl_alen = salen;
	}
}

void
ether_ifattach(struct ifnet *ifp, const uint8_t *lla)
{
	struct sockaddr_dl *sdl = ifp->if_sadl;

	if (sdl == NULL) {
		sdl = &ifp->if_sadl_storage;
		memset(sdl, 0, sizeof(*sdl));
		sdl->sdl_len = (uint8_t)sizeof(*sdl);
		sdl->sdl_family = 18; /* AF_LINK */
		ifp->if_sadl = sdl;
	}
	if (lla != NULL) {
		memcpy(LLADDR(sdl), lla, ETHER_ADDR_LEN);
		sdl->sdl_alen = ETHER_ADDR_LEN;
	}
	ifp->if_broadcastaddr = etherbroadcastaddr;
	ifp->if_flags |= IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
}

void
ether_ifdetach(struct ifnet *ifp)
{
	(void)ifp;
}

int
ifioctl_common(struct ifnet *ifp, unsigned long cmd, void *data)
{
	(void)ifp;
	(void)cmd;
	(void)data;
	return 0;
}

int
ether_ioctl(struct ifnet *ifp, unsigned long cmd, void *data)
{
	(void)ifp;
	(void)cmd;
	(void)data;
	return 0;
}

/* net80211's ioctl face is compiled out (N8: no ifconfig world); the
 * link-time reference from ifnet glue resolves to this ENOTTY shell. */
struct ieee80211com;
int
ieee80211_ioctl(struct ieee80211com *ic, unsigned long cmd, void *data)
{
	(void)ic;
	(void)cmd;
	(void)data;
	return ENOTTY;
}

const char *
ether_sprintf(const uint8_t *mac)
{
	static char buf[3 * ETHER_ADDR_LEN];

	snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
	    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return buf;
}

char *
ether_snprintf(char *buf, size_t len, const uint8_t *mac)
{
	snprintf(buf, len, "%02x:%02x:%02x:%02x:%02x:%02x",
	    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return buf;
}

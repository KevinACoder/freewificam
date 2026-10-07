/*
 * @file
 * @brief ifnet(9)/ifmedia(9) framework shells.
 *
 * The port owns the real interface presentation; these shells keep the
 * imported code working: counters, flags, the PS queue and the media
 * word list.
 *
 * @date 07.09.2026
 * @author zhugengyu
 */

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <mem/sysmalloc.h>

#undef malloc
#undef free

#include "../../compat/netbsd/net/if.h"
#include "../../compat/netbsd/sys/sockio.h"
#include "../../compat/netbsd/net/if_media.h"
#include "../../compat/netbsd/net/if_ether.h"
#include "../../compat/netbsd/sys/systm.h"
#include "../../compat/netbsd/sys/mbuf.h"
#include "../../compat/netbsd/sys/endian.h"
#include "../../compat/netbsd/net/route.h"

#include "../../port.h"

#undef malloc
#undef free

static unsigned int if_index_counter = 1;

int if_initialize(struct ifnet *ifp) {
	memset(&ifp->if_data, 0, sizeof(ifp->if_data));
	ifp->if_index = if_index_counter++;
	return 0;
}

void if_register(struct ifnet *ifp) {
	(void) ifp;
}

void if_detach(struct ifnet *ifp) {
	(void) ifp;
}

void if_deactivate(struct ifnet *ifp) {
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
}

void *if_percpuq_create(struct ifnet *ifp) {
	(void) ifp;
	/* single-cpu shell: the queue is only a token */
	return ifp;
}

static wlan_eapol_rx_fn wlan_eapol_rx;
static void *wlan_eapol_rx_arg;
static wlan_data_rx_fn wlan_data_rx;
static void *wlan_data_rx_arg;
static wlan_event_fn wlan_event;
static void *wlan_event_arg;

void wlan_port_set_event_handler(wlan_event_fn fn, void *arg) {
	wlan_event_arg = arg;
	wlan_event = fn;
}

void rt_ieee80211msg(struct ifnet *ifp, int what, const void *data, size_t len) {
	enum wlan_port_event event;

	(void) ifp;
	if (wlan_event == NULL) {
		return;
	}
	switch (what) {
	case RTM_IEEE80211_SCAN:
		event = WLAN_PORT_SCAN_DONE;
		break;
	case RTM_IEEE80211_ASSOC:
	case RTM_IEEE80211_REASSOC:
		event = WLAN_PORT_ASSOC;
		break;
	case RTM_IEEE80211_DISASSOC:
		event = WLAN_PORT_DISASSOC;
		break;
	default:
		return;
	}
	wlan_event(event, len >= ETHER_ADDR_LEN ? data : NULL, wlan_event_arg);
}

void wlan_port_set_eapol_rx(wlan_eapol_rx_fn fn, void *arg) {
	wlan_eapol_rx = fn;
	wlan_eapol_rx_arg = arg;
}

void wlan_port_set_data_rx(wlan_data_rx_fn fn, void *arg) {
	wlan_data_rx = fn;
	wlan_data_rx_arg = arg;
}

void if_percpuq_enqueue(void *pq, struct mbuf *m) {
	const struct ether_header *eh;

	(void) pq;

	if (m == NULL) {
		return;
	}
	eh = mtod(m, const struct ether_header *);

	if (ntohs(eh->ether_type) == ETHERTYPE_PAE && wlan_eapol_rx != NULL) {
		/* the payload follows the 14-byte ethernet header */
		wlan_eapol_rx(eh->ether_shost, mtod(m, const uint8_t *) +
		    sizeof(*eh), m->m_len - sizeof(*eh), wlan_eapol_rx_arg);
		m_freem(m);
		return;
	}
	if (wlan_data_rx != NULL) {
		wlan_data_rx(mtod(m, const uint8_t *), m->m_len,
		    wlan_data_rx_arg);
	}
	m_freem(m);
}

void if_start_lock(struct ifnet *ifp) {
	/* The transmit path is reached from the supplicant, the
	 * state-machine worker and the interrupt worker alike; the
	 * imported driver expects splnet() serialization here. */
	wlan_port_serializer_lock();
	if (ifp->if_start != NULL) {
		ifp->if_start(ifp);
	}
	wlan_port_serializer_unlock();
}

void if_link_state_change(struct ifnet *ifp, int link_state) {
	(void) ifp;
	(void) link_state;
}

const uint8_t etherbroadcastaddr[ETHER_ADDR_LEN] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

const uint8_t etheripaddr[ETHER_ADDR_LEN] = { 0 };

/* ------------------------------------------------------------------ */

void ifmedia_init_with_lock(struct ifmedia *ifm, int dontcare_mask,
	ifm_change_cb_t change, ifm_stat_cb_t stat, kmutex_t *lock) {
	(void) dontcare_mask;
	(void) change;
	(void) stat;
	(void) lock;
	memset(ifm, 0, sizeof(*ifm));
	TAILQ_INIT(&ifm->ifm_list);
}

void ifmedia_add(struct ifmedia *ifm, int mword, int data, void *aux) {
	struct ifmedia_entry *e;

	e = sysmalloc(sizeof(struct ifmedia_entry));
	if (e == NULL) {
		return;
	}
	memset(e, 0, sizeof(*e));
	e->ifm_media = (u_int) mword;
	e->ifm_data = (u_int) data;
	e->ifm_aux = aux;
	TAILQ_INSERT_TAIL(&ifm->ifm_list, e, ifm_list);
	if (ifm->ifm_cur == NULL) {
		ifm->ifm_cur = e;
	}
}

void ifmedia_set(struct ifmedia *ifm, int mword) {
	struct ifmedia_entry *e;

	TAILQ_FOREACH(e, &ifm->ifm_list, ifm_list) {
		if ((int) e->ifm_media == mword) {
			ifm->ifm_cur = e;
			return;
		}
	}
}

void ifmedia_fini(struct ifmedia *ifm) {
	struct ifmedia_entry *e, *n;

	e = TAILQ_FIRST(&ifm->ifm_list);
	while (e != NULL) {
		n = TAILQ_NEXT(e, ifm_list);
		sysfree(e);
		e = n;
	}
	TAILQ_INIT(&ifm->ifm_list);
	ifm->ifm_cur = NULL;
}

int ifmedia_ioctl(struct ifnet *ifp, struct ifreq *ifr, struct ifmedia *ifm,
	unsigned long cmd) {
	(void) ifp;
	(void) ifr;
	(void) ifm;
	(void) cmd;
	return ENOTTY;
}

uint64_t ifmedia_baudrate(int mword) {
	(void) mword;
	return 54000000;
}

/* the NetBSD if_alloc_sadl equivalent: bind the embedded link-level
 * sockaddr before anything reads CLLADDR() */
static struct sockaddr_dl *if_sadl_ensure(struct ifnet *ifp) {
#ifndef AF_LINK /* kept in sync with compat/netbsd/sys/socket.h */
#define AF_LINK 18
#endif
	struct sockaddr_dl *sdl = ifp->if_sadl;

	if (sdl == NULL) {
		sdl = &ifp->if_sadl_storage;
		memset(sdl, 0, sizeof(*sdl));
		sdl->sdl_len = (uint8_t) sizeof(*sdl);
		sdl->sdl_family = AF_LINK;
		ifp->if_sadl = sdl;
	}
	return sdl;
}

void if_set_sadl(struct ifnet *ifp, const void *lla, size_t len, bool factory) {
	struct sockaddr_dl *sdl;

	(void) factory; /* the address is never latched in hardware */
	if (ifp == NULL || lla == NULL || len > sizeof(sdl->sdl_data)) {
		return;
	}
	sdl = if_sadl_ensure(ifp);
	memcpy(LLADDR(sdl), lla, len);
	sdl->sdl_alen = (uint8_t) len;
}

void ether_ifattach(struct ifnet *ifp, const uint8_t *lla) {
	struct sockaddr_dl *sdl = if_sadl_ensure(ifp);

	if (lla != NULL) {
		memcpy(LLADDR(sdl), lla, ETHER_ADDR_LEN);
		sdl->sdl_alen = ETHER_ADDR_LEN;
	}
	ifp->if_broadcastaddr = etherbroadcastaddr;
	ifp->if_flags |= IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
}

void ether_ifdetach(struct ifnet *ifp) {
	(void) ifp;
}

int ifioctl_common(struct ifnet *ifp, unsigned long cmd, void *data) {
	(void) ifp;
	(void) cmd;
	(void) data;
	return 0;
}

int ether_ioctl(struct ifnet *ifp, unsigned long cmd, void *data) {
	(void) ifp;
	(void) cmd;
	(void) data;
	return 0;
}

struct ieee80211com;
int ieee80211_ioctl(struct ieee80211com *ic, unsigned long cmd, void *data);

int ieee80211_ioctl(struct ieee80211com *ic, unsigned long cmd, void *data) {
	(void) ic;
	(void) cmd;
	(void) data;
	return ENOTTY;
}

const char *ether_sprintf(const uint8_t *mac) {
	static char buf[3 * ETHER_ADDR_LEN];

	snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
	    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return buf;
}

char *ether_snprintf(char *buf, size_t len, const uint8_t *mac) {
	snprintf(buf, len, "%02x:%02x:%02x:%02x:%02x:%02x",
	    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return buf;
}

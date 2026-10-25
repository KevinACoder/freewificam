/*
 * @file
 * @brief Thin ifnet(9)/ifmedia(9) shells for the imported stack.
 *
 * The port owns the real interface presentation; these shells keep the imported
 * code working: counters, flags, the link-level address, the media word list,
 * the single receive seam (EAPOL versus data) and the transmit serialization.
 * The declarations live in the kept vocabulary headers (net/if.h,
 * net/if_media.h); this file is their implementation for the ports.
 *
 * @design kernel-services shim: the component's own port shell
 * (components/net80211/port/net/embox/bsd_ifnet.c:107-140 receive seam and
 * transmit serialization, :73-95 event seam, :155-218 media list) re-derived on
 * the port serializer and the port seam; every behavioural difference is in
 * components/net80211/ports/DIVERGENCES.md. The presentation hooks it feeds are the ones
 * port/port.h declares.
 */

#include <errno.h>
#include <string.h>

#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/systm.h>
#include <sys/endian.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_ether.h>
#include <net/if_media.h>
#include <net/route.h>

#include <port/port.h>

#include "../osal/wlan_osal.h"

static unsigned int if_index_counter = 1;

/* ------------------------------------------------------------------ */
/* Interface shells                                                    */
/* ------------------------------------------------------------------ */

int if_initialize(struct ifnet *ifp)
{
	memset(&ifp->if_data, 0, sizeof(ifp->if_data));
	ifp->if_index = if_index_counter++;
	return 0;
}

void if_register(struct ifnet *ifp)
{
	(void) ifp;
}

void if_detach(struct ifnet *ifp)
{
	(void) ifp;
}

void if_deactivate(struct ifnet *ifp)
{
	ifp->if_drv_flags &= ~(unsigned short) IFF_DRV_OACTIVE;
}

void *if_percpuq_create(struct ifnet *ifp)
{
	/* single-queue port: the queue handle is the interface itself */
	return ifp;
}

void if_link_state_change(struct ifnet *ifp, int link_state)
{
	(void) ifp;
	(void) link_state;
}

/* ------------------------------------------------------------------ */
/* Receive seam and event seam (the presentation hooks of port.h)      */
/* ------------------------------------------------------------------ */

static wlan_eapol_rx_fn eapol_rx;
static void *eapol_rx_arg;
static wlan_data_rx_fn data_rx;
static void *data_rx_arg;
static wlan_event_fn event_fn;
static void *event_fn_arg;

void wlan_port_set_eapol_rx(wlan_eapol_rx_fn fn, void *arg)
{
	eapol_rx = fn;
	eapol_rx_arg = arg;
}

void wlan_port_set_data_rx(wlan_data_rx_fn fn, void *arg)
{
	data_rx = fn;
	data_rx_arg = arg;
}

void wlan_port_set_event_handler(wlan_event_fn fn, void *arg)
{
	event_fn = fn;
	event_fn_arg = arg;
}

void rt_ieee80211msg(struct ifnet *ifp, int what, const void *data, size_t len)
{
	enum wlan_port_event event;

	(void) ifp;
	if (event_fn == NULL) {
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
	event_fn(event, len >= ETHER_ADDR_LEN ? data : NULL, event_fn_arg);
}

void if_percpuq_enqueue(void *pq, struct mbuf *m)
{
	const struct ether_header *eh;

	(void) pq;
	if (m == NULL) {
		return;
	}
	/* The receive seam hands out one contiguous buffer, so a frame that
	 * arrived as a chain (a reassembled one, for instance) is linearised
	 * here; m_pullup frees the chain if it cannot build the head. */
	if (m->m_next != NULL) {
		m = m_pullup(m, m->m_pkthdr.len);
		if (m == NULL) {
			return;
		}
	}
	eh = mtod(m, const struct ether_header *);
	if (ntohs(eh->ether_type) == ETHERTYPE_PAE && eapol_rx != NULL) {
		/* the payload follows the 14-byte ethernet header */
		eapol_rx(eh->ether_shost,
		    mtod(m, const uint8_t *) + sizeof(*eh),
		    (size_t) m->m_len - sizeof(*eh), eapol_rx_arg);
		wlan_m_freem(m);
		return;
	}
	if (data_rx != NULL) {
		data_rx(mtod(m, const uint8_t *), (size_t) m->m_len,
		    data_rx_arg);
	}
	wlan_m_freem(m);
}

/* The transmit path is reached from the supplicant, the state-machine worker
 * and the interrupt worker alike; the imported driver expects splnet()
 * serialization here, which the port serializer provides. */
void if_start_lock(struct ifnet *ifp)
{
	wlan_port_serializer_lock();
	if (ifp->if_start != NULL) {
		ifp->if_start(ifp);
	}
	wlan_port_serializer_unlock();
}

/* ------------------------------------------------------------------ */
/* Link-level address and ethernet helpers                             */
/* ------------------------------------------------------------------ */

const uint8_t etherbroadcastaddr[ETHER_ADDR_LEN] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

const uint8_t etheripaddr[ETHER_ADDR_LEN] = { 0 };

#ifndef AF_LINK
#define AF_LINK 18
#endif

static struct sockaddr_dl *if_sadl_ensure(struct ifnet *ifp)
{
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

void if_set_sadl(struct ifnet *ifp, const void *lla, size_t len, bool factory)
{
	struct sockaddr_dl *sdl;

	(void) factory; /* the address is never latched in hardware here */
	if (ifp == NULL || lla == NULL || len > sizeof(sdl->sdl_data)) {
		return;
	}
	sdl = if_sadl_ensure(ifp);
	memcpy(LLADDR(sdl), lla, len);
	sdl->sdl_alen = (uint8_t) len;
}

void ether_ifattach(struct ifnet *ifp, const uint8_t *lla)
{
	struct sockaddr_dl *sdl = if_sadl_ensure(ifp);

	if (lla != NULL) {
		memcpy(LLADDR(sdl), lla, ETHER_ADDR_LEN);
		sdl->sdl_alen = ETHER_ADDR_LEN;
	}
	ifp->if_broadcastaddr = etherbroadcastaddr;
	ifp->if_flags |= IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
}

void ether_ifdetach(struct ifnet *ifp)
{
	(void) ifp;
}

const char *ether_sprintf(const uint8_t *mac)
{
	static char buf[3 * ETHER_ADDR_LEN];

	snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
	    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return buf;
}

char *ether_snprintf(char *buf, size_t len, const uint8_t *mac)
{
	snprintf(buf, len, "%02x:%02x:%02x:%02x:%02x:%02x",
	    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return buf;
}

/* ------------------------------------------------------------------ */
/* ioctl shells: no ioctl surface on a port                            */
/* ------------------------------------------------------------------ */

int ifioctl_common(struct ifnet *ifp, unsigned long cmd, void *data)
{
	(void) ifp;
	(void) cmd;
	(void) data;
	return 0;
}

int ether_ioctl(struct ifnet *ifp, unsigned long cmd, void *data)
{
	(void) ifp;
	(void) cmd;
	(void) data;
	return 0;
}

/* net80211 calls its ioctl entry from the interface path; the ioctl
 * translation unit is not part of the port's compile set (see the component's
 * DIVERGENCES ledger), so the entry refuses instead of vanishing. The
 * prototype matches the stack's own declaration
 * (net80211/ieee80211_var.h:305). */
struct ieee80211com;
int ieee80211_ioctl(struct ieee80211com *ic, u_long cmd, void *data)
{
	(void) ic;
	(void) cmd;
	(void) data;
	return ENOTTY;
}

/* ------------------------------------------------------------------ */
/* ifmedia(9) shell                                                    */
/* ------------------------------------------------------------------ */

void ifmedia_init_with_lock(struct ifmedia *ifm, int dontcare_mask,
    ifm_change_cb_t change, ifm_stat_cb_t stat, kmutex_t *lock)
{
	(void) dontcare_mask;
	(void) change;
	(void) stat;
	(void) lock;
	memset(ifm, 0, sizeof(*ifm));
	TAILQ_INIT(&ifm->ifm_list);
}

void ifmedia_add(struct ifmedia *ifm, int mword, int data, void *aux)
{
	struct ifmedia_entry *e;

	e = wlan_osal_alloc(sizeof(*e));
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

void ifmedia_set(struct ifmedia *ifm, int mword)
{
	struct ifmedia_entry *e;

	TAILQ_FOREACH(e, &ifm->ifm_list, ifm_list) {
		if ((int) e->ifm_media == mword) {
			ifm->ifm_cur = e;
			return;
		}
	}
}

void ifmedia_fini(struct ifmedia *ifm)
{
	struct ifmedia_entry *e, *n;

	e = TAILQ_FIRST(&ifm->ifm_list);
	while (e != NULL) {
		n = TAILQ_NEXT(e, ifm_list);
		wlan_osal_free(e);
		e = n;
	}
	TAILQ_INIT(&ifm->ifm_list);
	ifm->ifm_cur = NULL;
}

int ifmedia_ioctl(struct ifnet *ifp, struct ifreq *ifr, struct ifmedia *ifm,
    unsigned long cmd)
{
	(void) ifp;
	(void) ifr;
	(void) ifm;
	(void) cmd;
	return ENOTTY;
}

/* The media word says nothing about the radio's real rate here; the driver
 * round derives it from the media word (see DIVERGENCES). */
uint64_t ifmedia_baudrate(int mword)
{
	(void) mword;
	return 54000000;
}

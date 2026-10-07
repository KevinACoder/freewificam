/*
 * @file
 * @brief Minimal ifnet(9) shell.
 *
 * Field names follow NetBSD so the imported code reads unchanged. The
 * port owns the lifetime: it creates the shell, wires the callbacks it
 * cares about, and presents it to its own network stack.
 */

#ifndef _COMPAT_NET_IF_H_
#define _COMPAT_NET_IF_H_

/* Also claim the embox header guard: once this file is loaded, the
 * embox <net/if.h> (a different ifnet layout) must stay out. */
#ifndef NET_IF_H_
#define NET_IF_H_
#endif

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <net/if_dl.h>

#define IFNAMSIZ 16

#define IFF_UP 0x0001
#define IFF_BROADCAST 0x0002
#define IFF_DEBUG 0x0004
#define IFF_LOOPBACK 0x0008
#define IFF_POINTOPOINT 0x0010
#define IFF_RUNNING 0x0040
#define IFF_NOARP 0x0080
#define IFF_PROMISC 0x0100
#define IFF_ALLMULTI 0x0200
#define IFF_SIMPLEX 0x0800
#define IFF_MULTICAST 0x8000

/* NetBSD split driver flags out of if_flags; keep both names alive. */
#define IFF_DRV_OACTIVE 0x0400
#define IFF_OACTIVE IFF_DRV_OACTIVE

#define IFF_CANTCHANGE \
	(IFF_BROADCAST | IFF_POINTOPOINT | IFF_LOOPBACK | IFF_PROMISC | \
	 IFF_ALLMULTI | IFF_SIMPLEX | IFF_MULTICAST)

/*
 * Stats: the imported code uses if_statinc()/if_statadd() on named
 * counters. Keep a named struct and macro-expand to it.
 */
struct if_data {
	uint64_t if_ipackets;
	uint64_t if_ierrors;
	uint64_t if_opackets;
	uint64_t if_oerrors;
	uint64_t if_collisions;
	uint64_t if_ibytes;
	uint64_t if_obytes;
	uint64_t if_imcasts;
	uint64_t if_omcasts;
	uint64_t if_noproto;
};

/* ifq (power-save queue): plain mbuf list with the NetBSD fields. */
struct ifqueue {
	struct mbuf *ifq_head;
	struct mbuf *ifq_tail;
	int ifq_len;
	int ifq_maxlen;
	int ifq_drops;
};

#define IF_DEQUEUE(ifq, m) do { \
	(m) = (ifq)->ifq_head; \
	if ((m) != NULL) { \
		(ifq)->ifq_head = (m)->m_nextpkt; \
		if ((ifq)->ifq_head == NULL) \
			(ifq)->ifq_tail = NULL; \
		(ifq)->ifq_len--; \
		(m)->m_nextpkt = NULL; \
	} \
} while (0)

#define IF_PURGE(ifq) do { \
	struct mbuf *__m; \
	for (;;) { \
		IF_DEQUEUE((ifq), __m); \
		if (__m == NULL) \
			break; \
		m_freem(__m); \
	} \
} while (0)

#define IF_QFULL(ifq) ((ifq)->ifq_len >= (ifq)->ifq_maxlen)
#define IF_DROP(ifq) ((ifq)->ifq_drops++)
#define IF_ENQUEUE(ifq, m) do { \
	(m)->m_nextpkt = NULL; \
	if ((ifq)->ifq_tail != NULL) \
		(ifq)->ifq_tail->m_nextpkt = (m); \
	else \
		(ifq)->ifq_head = (m); \
	(ifq)->ifq_tail = (m); \
	(ifq)->ifq_len++; \
} while (0)
#define IFQ_IS_EMPTY(ifq) ((ifq)->ifq_len == 0)
#define IF_POLL(ifq, m) ((m) = (ifq)->ifq_head)
#define IFQ_POLL(ifq, m) IF_POLL((ifq), (m))
#define IFQ_DEQUEUE(ifq, m) IF_DEQUEUE((ifq), (m))
#define IFQ_ENQUEUE(ifq, m, err) do { \
	(m)->m_nextpkt = NULL; \
	if ((ifq)->ifq_tail != NULL) \
		(ifq)->ifq_tail->m_nextpkt = (m); \
	else \
		(ifq)->ifq_head = (m); \
	(ifq)->ifq_tail = (m); \
	(ifq)->ifq_len++; \
	(err) = 0; \
} while (0)

/* interface output queue alias used by the drivers */
#define if_snd if_queue

/* the ioctl-only multicast helpers need the union arm address */
struct ifreq;
static inline struct sockaddr *ifreq_getaddr(unsigned long cmd,
	struct ifreq *ifr) {
	(void) cmd;
	return (struct sockaddr *) &ifr->ifr_ifru;
}

struct ifnet;
typedef void (*if_start_fn)(struct ifnet *);
typedef int (*if_ioctl_fn)(struct ifnet *, unsigned long, void *);
typedef int (*if_init_fn)(struct ifnet *);
typedef void (*if_watchdog_fn)(struct ifnet *);
typedef void (*if_stop_fn)(struct ifnet *, int);

struct ifnet {
	char if_xname[IFNAMSIZ];
	unsigned int if_index;
	unsigned short if_flags;
	unsigned short if_drv_flags;
	unsigned int if_mtu;
	unsigned int if_hdrlen;
	uint64_t if_baudrate;
	int if_timer;

	void *if_softc;
	const void *if_broadcastaddr;

	struct ifqueue if_queue; /* legacy PS queue */

	if_start_fn if_start;
	if_ioctl_fn if_ioctl;
	if_init_fn if_init;
	if_watchdog_fn if_watchdog;
	if_stop_fn if_stop;

	/* link-level address shell (sockaddr_dl payload) */
	struct sockaddr_dl if_sadl_storage;
	struct sockaddr_dl *if_sadl;

	void *if_percpuq;
	struct if_data if_data;
};

#define if_statinc(ifp, x) (((ifp)->if_data.x)++)
#define if_statadd(ifp, x, n) (((ifp)->if_data.x) += (n))
#define if_statdec(ifp, x) (((ifp)->if_data.x)--)
#define if_statset(ifp, x, n) (((ifp)->if_data.x) = (n))
#define IF_STATGET(ifp, x) ((ifp)->if_data.x)

int if_initialize(struct ifnet *ifp);
void if_register(struct ifnet *ifp);
void if_detach(struct ifnet *ifp);
void if_deactivate(struct ifnet *ifp);
void *if_percpuq_create(struct ifnet *ifp);
void if_percpuq_enqueue(void *pq, struct mbuf *m);
void if_start_lock(struct ifnet *ifp);
void if_link_state_change(struct ifnet *ifp, int link_state);

int ifioctl_common(struct ifnet *, unsigned long, void *);
void ether_ifattach(struct ifnet *, const uint8_t *);
void ether_ifdetach(struct ifnet *);
int ether_ioctl(struct ifnet *, unsigned long, void *);

/* set the station address (and bind the link-level sockaddr on first
 * use): drivers call this once the hardware address is known */
void if_set_sadl(struct ifnet *, const void *, size_t, bool);

#define LINK_STATE_UP 1
#define LINK_STATE_DOWN 2
#define LINK_STATE_UNKNOWN 0

#endif /* _COMPAT_NET_IF_H_ */

#define IFQ_SET_READY(ifq) ((void) (ifq))
#define IFQ_MAXLEN 64

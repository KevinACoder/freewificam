/*
 * @file
 * @brief Simplified mbuf model for the NetBSD-imported sources.
 *
 * Every mbuf owns one contiguous buffer (the "cluster"). Chaining and
 * the packet header are kept because the stack relies on them; the
 * external-storage machinery of the BSD allocator does not exist. The
 * implementation lives in the port.
 */

#ifndef _SYS_MBUF_H_
#define _SYS_MBUF_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include "malloc.h"

#define MSIZE 256
#define MCLBYTES 4096
#define MHLEN 128
#define MLEN 224
extern int max_linkhdr;

/* m_flags */
#ifndef M_EXT
#define M_EXT 0x000001
#endif
#ifndef M_PKTHDR
#define M_PKTHDR 0x000002
#endif
#ifndef M_MCAST
#define M_MCAST 0x000004
#endif
#ifndef M_PROTO1
#define M_PROTO1 0x000008
#endif
#ifndef M_LINK0
#define M_LINK0 0x000010
#endif
#ifndef M_LINK1
#define M_LINK1 0x000020
#endif
#ifndef M_LINK2
#define M_LINK2 0x000040
#endif
#ifndef M_LINK3
#define M_LINK3 0x000080
#endif
#ifndef M_LINK4
#define M_LINK4 0x000100
#endif
#ifndef M_LINK5
#define M_LINK5 0x000200
#endif
#ifndef M_LINK6
#define M_LINK6 0x000400
#endif
#ifndef M_BCAST
#define M_BCAST 0x000800
#endif
#ifndef M_ZERO
#define M_ZERO 0x001000
#endif
/* driver-private flags live above 0x8000 to stay out of the way */

#ifndef M_DONTWAIT
#define M_DONTWAIT 1
#endif
#ifndef M_WAITOK
#define M_WAITOK 2
#endif
#ifndef M_NOWAIT
#define M_NOWAIT M_DONTWAIT
#endif
#ifndef M_CANFAIL
#define M_CANFAIL 4
#endif

/* m_type */
#define MT_FREE 0
#define MT_DATA 1
#define MT_HEADER 2
#define MT_OOBDATA 3

#define M_COPYALL 0x7fffffff

struct m_pkthdr {
	int len; /* packet length */
	void *rcvif; /* receive interface shell */
	int csum_data; /* reused as the PS-queue age */
	uint32_t csum_flags;
	void *userdata; /* M_SETCTX storage */
};

struct mbuf {
	uint32_t m_flags;
	struct mbuf *m_next;
	struct mbuf *m_nextpkt;
	char *m_data;
	int m_len;
	int m_type;

	struct m_pkthdr m_pkthdr;

	/* The owned cluster; m_data points inside it. */
	char *m_cluster;
	size_t m_cluster_size;
};

#define MTX(...) /* no claims */

#define mtod(m, t) ((t)((m)->m_data))
#define mtodoff(m, t, off) ((t)((m)->m_data + (off)))

#define M_ALIGN(m, len) m_align((m), (len))
#define M_LEADINGSPACE(m) ((int)((m)->m_data - (m)->m_cluster))
#define M_TRAILINGSPACE(m) \
	((int)((m)->m_cluster + (m)->m_cluster_size - ((m)->m_data + (m)->m_len)))

#define M_MOVE_PKTHDR(to, from) m_move_pkthdr((to), (from))
#define M_SETCTX(m, x) ((m)->m_pkthdr.userdata = (x))
#define M_GETCTX(m, t) ((t)((m)->m_pkthdr.userdata))
#define M_CLEARCTX(m) ((m)->m_pkthdr.userdata = NULL)
#define M_HASFCS 0x002000 /* CRC left in the frame */

struct mbuf *m_get_impl(int wait, int type, int pkthdr);
struct mbuf *m_getcl_impl(int wait, int type, int pkthdr);
void m_free_impl(struct mbuf *m);

#define MGET(m, how, type) ((m) = m_get_impl((how), (type), 0))
#define MGETHDR(m, how, type) ((m) = m_get_impl((how), (type), 1))
#define MCLGET(m, how) ((void) ((m) != NULL && ((m)->m_flags |= M_EXT)))
/* every mbuf already owns a full-size cluster; attaching external
 * storage up to MCLBYTES is a flag operation here */
#define MEXTMALLOC(m, len, how) \
	((void) ((m) != NULL && (len) <= MCLBYTES && ((m)->m_flags |= M_EXT)))

#define m_get(how, type) m_get_impl((how), (type), 0)
#define m_gethdr(how, type) m_get_impl((how), (type), 1)
#define m_getcl(how, type, pkthdr) m_getcl_impl((how), (type), (pkthdr))
#define m_free(m) m_free_impl(m)
#define m_freem(m) wlan_m_freem(m)

struct mbuf *m_copypacket(struct mbuf *m, int how);
void m_freem(struct mbuf *m);
void m_adj(struct mbuf *m, int len);
struct mbuf *m_pullup(struct mbuf *m, int len);
void m_copydata(const struct mbuf *m, int off, size_t len, void *cp);
int m_copyback(struct mbuf *m, int off, int len, const void *cp);
struct mbuf *m_prepend(struct mbuf *m, int len, int how);
int m_makewritable(struct mbuf **, int, int, int);
#define M_PREPEND(m, len, how) ((m) = m_prepend((m), (len), (how)))
struct mbuf *m_copym(struct mbuf *m, int off, int len, int wait);
void m_cat(struct mbuf *m, struct mbuf *n);
int m_append(struct mbuf *m, int len, const void *cp);
void m_align(struct mbuf *m, int len);
void m_move_pkthdr(struct mbuf *to, struct mbuf *from);
void m_claim(struct mbuf *m, void *claim);
#define MCLAIM(m, c) ((void)0)

/* 802.1Q tag carried in the packet header */
static inline int vlan_has_tag(struct mbuf *m) {
	return (m->m_flags & M_PROTO1) != 0;
}
static inline uint16_t vlan_get_tag(struct mbuf *m) {
	return vlan_has_tag(m) ? (uint16_t) m->m_pkthdr.csum_data : 0;
}
static inline void vlan_set_tag(struct mbuf *m, uint16_t tag) {
	m->m_flags |= M_PROTO1;
	m->m_pkthdr.csum_data = tag;
}

struct ifqueue;
struct ifnet;
#define m_set_rcvif(m, ifp) ((m)->m_pkthdr.rcvif = (ifp))
#define m_get_rcvif(m, ifpp) ((*ifpp) = (struct ifnet *)((m)->m_pkthdr.rcvif))

#endif /* _SYS_MBUF_H_ */

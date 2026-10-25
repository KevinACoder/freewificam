/*
 * @file
 * @brief Thin mbuf cluster model for the imported stack.
 *
 * One mbuf owns one contiguous cluster; chains and the packet header carry the
 * semantics the imported sources expect. Every allocation goes through the port
 * seam, so the memory subsystem has a single place to replace later. The
 * external-storage machinery of the BSD allocator does not exist here (the kept
 * vocabulary header documents the same model).
 *
 * @design kernel-services shim: the NetBSD mbuf(9) face lives in the kept
 * vocabulary header (components/net80211/compat/netbsd/sys/mbuf.h:87-176) and
 * the semantics follow the component's own port shell
 * (components/net80211/port/net/embox/bsd_mbuf.c:33-176), re-derived with the
 * allocation on the port seam and without the driver DMA padding (recorded in
 * components/net80211/ports/DIVERGENCES.md).
 */

#include <string.h>

#include <sys/errno.h>
#include <sys/mbuf.h>
#include <sys/systm.h>

#include "../osal/wlan_osal.h"

/* The stack raises this once it knows which frame header it needs
 * (net80211/ieee80211_netbsd.c); the port starts at the ethernet figure the
 * component used. */
int max_linkhdr = 16;

/* Data offset inside the cluster for header mbufs (same model as the
 * component's shell). */
#define MH_ALIGN 256
/* Alignment kept for the cluster start; USB/DMA padding is a driver-round item. */
#define MCL_ALIGN 16

static struct mbuf *mbuf_alloc(int type, int pkthdr)
{
	struct mbuf *m;
	uintptr_t cl;

	m = wlan_osal_zalloc(sizeof(*m) + MCL_ALIGN + MCLBYTES);
	if (m == NULL) {
		return NULL;
	}
	m->m_type = type;
	m->m_flags = M_EXT | (pkthdr ? M_PKTHDR : 0); /* cluster always owned */
	cl = ((uintptr_t) (m + 1) + (MCL_ALIGN - 1)) & ~(uintptr_t) (MCL_ALIGN - 1);
	m->m_cluster = (char *) cl;
	m->m_cluster_size = MCLBYTES;
	m->m_data = m->m_cluster + (pkthdr ? MH_ALIGN : 0);
	m->m_len = 0;
	if (pkthdr) {
		m->m_pkthdr.len = 0;
	}
	return m;
}

struct mbuf *m_get_impl(int wait, int type, int pkthdr)
{
	(void) wait; /* the seam does not block differently for M_DONTWAIT */
	return mbuf_alloc(type, pkthdr);
}

struct mbuf *m_getcl_impl(int wait, int type, int pkthdr)
{
	return m_get_impl(wait, type, pkthdr);
}

void m_free_impl(struct mbuf *m)
{
	if (m == NULL) {
		return;
	}
	if (m->m_flags & M_PKTHDR) {
		m->m_nextpkt = NULL;
	}
	wlan_osal_free(m);
}

/* The vocabulary header maps the m_freem() macro onto this name, so this is the
 * symbol the imported call sites expect. */
void wlan_m_freem(struct mbuf *m)
{
	struct mbuf *next;

	while (m != NULL) {
		next = m->m_next;
		m_free_impl(m);
		m = next;
	}
}

void m_adj(struct mbuf *mp, int req_len)
{
	int len = req_len;
	struct mbuf *m;
	int count;

	if ((m = mp) == NULL) {
		return;
	}
	if (len >= 0) {
		/* trim from the head */
		while (len > 0 && m != NULL) {
			if (m->m_len <= len) {
				len -= m->m_len;
				m->m_data += m->m_len;
				m->m_len = 0;
				m = m->m_next;
			} else {
				m->m_len -= len;
				m->m_data += len;
				len = 0;
			}
		}
	} else {
		/* trim from the tail */
		len = -len;
		count = 0;
		for (m = mp; m != NULL; m = m->m_next) {
			count += m->m_len;
		}
		count = count > len ? count - len : 0;
		for (m = mp; m != NULL; m = m->m_next) {
			if (m->m_len > count) {
				m->m_len = count;
			}
			count -= m->m_len;
		}
	}
	if (mp->m_flags & M_PKTHDR) {
		int plen = 0;

		for (m = mp; m != NULL; m = m->m_next) {
			plen += m->m_len;
		}
		mp->m_pkthdr.len = plen;
	}
}

struct mbuf *m_pullup(struct mbuf *m, int len)
{
	struct mbuf *head, *next;
	int total = 0;

	if (m == NULL) {
		return NULL;
	}
	if (len < 0 || len > MCLBYTES - MH_ALIGN) {
		wlan_m_freem(m);
		return NULL;
	}
	if (m->m_len >= len) {
		return m;
	}
	for (next = m; next != NULL; next = next->m_next) {
		total += next->m_len;
	}
	if (total < len) {
		wlan_m_freem(m);
		return NULL;
	}
	head = mbuf_alloc(m->m_type, (m->m_flags & M_PKTHDR) != 0);
	if (head == NULL) {
		wlan_m_freem(m);
		return NULL;
	}
	if (m->m_flags & M_PKTHDR) {
		m_move_pkthdr(head, m);
	}
	m_copydata(m, 0, (size_t) len, head->m_data);
	head->m_len = len;
	m_adj(m, len);
	while (m != NULL && m->m_len == 0) {
		next = m->m_next;
		m_free_impl(m);
		m = next;
	}
	head->m_next = m;
	return head;
}

void m_copydata(const struct mbuf *m, int off, size_t len, void *cp)
{
	size_t taken;

	while (off > 0 && m != NULL) {
		if (off < m->m_len) {
			break;
		}
		off -= m->m_len;
		m = m->m_next;
	}
	while (len > 0 && m != NULL) {
		taken = (size_t) m->m_len - (size_t) off < len
		    ? (size_t) m->m_len - (size_t) off
		    : len;
		memcpy(cp, m->m_data + off, taken);
		cp = (char *) cp + taken;
		len -= taken;
		off = 0;
		m = m->m_next;
	}
	if (len > 0) {
		printf("m_copydata: short read\n");
	}
}

int m_copyback(struct mbuf *m, int off, int len, const void *cp)
{
	int total = 0;
	struct mbuf *n;
	int taken;

	for (n = m; n != NULL; n = n->m_next) {
		total += n->m_len;
	}
	if (off + len > total) {
		return ENOBUFS;
	}
	for (n = m; n != NULL && len > 0; n = n->m_next) {
		if (off >= n->m_len) {
			off -= n->m_len;
			continue;
		}
		taken = n->m_len - off < len ? n->m_len - off : len;
		memcpy(n->m_data + off, cp, (size_t) taken);
		cp = (const char *) cp + taken;
		len -= taken;
		off = 0;
	}
	return 0;
}

struct mbuf *m_prepend(struct mbuf *m, int len, int how)
{
	struct mbuf *n;

	if (m == NULL || len < 0 || len > MCLBYTES - MH_ALIGN) {
		wlan_m_freem(m);
		return NULL;
	}
	if (M_LEADINGSPACE(m) >= len) {
		m->m_data -= len;
		m->m_len += len;
		if (m->m_flags & M_PKTHDR) {
			m->m_pkthdr.len += len;
		}
		return m;
	}
	n = mbuf_alloc(m->m_type, (m->m_flags & M_PKTHDR) != 0);
	if (n == NULL) {
		wlan_m_freem(m);
		return NULL;
	}
	(void) how;
	n->m_len = len;
	if (m->m_flags & M_PKTHDR) {
		m_move_pkthdr(n, m);
		n->m_pkthdr.len += len;
	}
	n->m_next = m;
	return n;
}

void m_cat(struct mbuf *m, struct mbuf *n)
{
	while (m->m_next != NULL) {
		m = m->m_next;
	}
	m->m_next = n;
	if (m->m_flags & M_PKTHDR) {
		int len = 0;

		for (; n != NULL; n = n->m_next) {
			len += n->m_len;
		}
		m->m_pkthdr.len += len;
	}
}

void m_align(struct mbuf *m, int len)
{
	char *base;

	if (m == NULL || len < 0) {
		return;
	}
	base = m->m_cluster + ((m->m_flags & M_PKTHDR) ? MH_ALIGN : 0);
	m->m_data = base;
	m->m_len = len;
}

struct mbuf *m_copypacket(struct mbuf *m, int how)
{
	struct mbuf *n;

	n = mbuf_alloc(m != NULL ? m->m_type : MT_DATA,
	    m != NULL && (m->m_flags & M_PKTHDR));
	if (n == NULL || m == NULL) {
		wlan_m_freem(n);
		return NULL;
	}
	(void) how;
	n->m_flags |= m->m_flags & (M_BCAST | M_MCAST);
	memcpy(n->m_cluster, m->m_cluster, m->m_cluster_size);
	n->m_data = n->m_cluster + (m->m_data - m->m_cluster);
	n->m_len = m->m_len;
	if (m->m_flags & M_PKTHDR) {
		n->m_pkthdr.len = m->m_pkthdr.len;
	}
	return n;
}

int m_makewritable(struct mbuf **mp, int off, int len, int wait)
{
	(void) mp;
	(void) off;
	(void) len;
	(void) wait;
	/* clusters are private and writable by construction */
	return 0;
}

void m_move_pkthdr(struct mbuf *to, struct mbuf *from)
{
	to->m_flags = (to->m_flags & ~M_PKTHDR) | (from->m_flags & M_PKTHDR);
	to->m_pkthdr = from->m_pkthdr;
	from->m_flags &= ~M_PKTHDR;
}

void m_claim(struct mbuf *m, void *claim)
{
	(void) m;
	(void) claim;
}

/*
 * @file
 * @brief Simplified mbuf implementation over the embox heap.
 *
 * One allocation holds the header plus the cluster; chains and the
 * packet header carry the semantics the imported stack expects.
 *
 * @date 07.09.2026
 * @author zhugengyu
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mbuf.h>
#include <stdio.h>
#include <mem/sysmalloc.h>

/* the BSD malloc/free macros must not intercept the embox calls */
#undef malloc
#undef free
#include <sys/systm.h>

/* The data area starts MH_ALIGN into the cluster. PCIe RX hands the
 * cluster to the device DMA: the allocation pads for a 64-byte
 * aligned cluster and a full-line data offset, and the device region
 * ends on a line boundary, so a POSTREAD invalidate never touches
 * foreign data. */
#define MH_ALIGN 256
#define MH_DMA_PAD 1024

struct mbuf *m_get_impl(int wait, int type, int pkthdr) {
	struct mbuf *m;
	uintptr_t cl;

	if (wait == M_DONTWAIT) {
		/* the embox heap blocks on exhaustion; same call either way */
	}
	m = sysmalloc(sizeof(struct mbuf) + MH_DMA_PAD + MCLBYTES + MH_ALIGN);
	if (m == NULL) {
		return NULL;
	}
	memset(m, 0, sizeof(struct mbuf));

	m->m_type = type;
	m->m_flags = (pkthdr ? M_PKTHDR : 0) | M_EXT; /* cluster always owned */
	cl = ((uintptr_t) (m + 1) + 255) & ~(uintptr_t) 255;
	m->m_cluster = (char *) cl;
	m->m_cluster_size = MCLBYTES + MH_ALIGN;
	m->m_data = m->m_cluster + (pkthdr ? MH_ALIGN : 0);
	m->m_len = 0;
	if (pkthdr) {
		m->m_pkthdr.len = 0;
		m->m_pkthdr.csum_data = 0;
		m->m_pkthdr.userdata = NULL;
	}
	return m;
}

struct mbuf *m_getcl_impl(int wait, int type, int pkthdr) {
	return m_get_impl(wait, type, pkthdr);
}

static void m_free_one(struct mbuf *m) {
	sysfree(m);
}

void m_free_impl(struct mbuf *m) {
	if (m == NULL) {
		return;
	}
	if (m->m_flags & M_PKTHDR) {
		m->m_nextpkt = NULL;
	}
	m_free_one(m);
}

void m_freem(struct mbuf *m) {
	struct mbuf *next;

	while (m != NULL) {
		next = m->m_next;
		m_free_one(m);
		m = next;
	}
}

void m_adj(struct mbuf *mp, int req_len) {
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
	/* keep the packet header length in sync */
	if (mp->m_flags & M_PKTHDR) {
		int plen = 0;

		for (m = mp; m != NULL; m = m->m_next) {
			plen += m->m_len;
		}
		mp->m_pkthdr.len = plen;
	}
}

struct mbuf *m_pullup(struct mbuf *m, int len) {
	struct mbuf *head, *next;
	int total = 0;

	if (m == NULL) {
		return NULL;
	}
	if (len < 0 || len > MCLBYTES - MH_ALIGN) {
		m_freem(m);
		return NULL;
	}
	if (m->m_len >= len) {
		return m;
	}
	for (next = m; next != NULL; next = next->m_next) {
		total += next->m_len;
	}
	if (total < len) {
		m_freem(m);
		return NULL;
	}
	head = m_get_impl(M_DONTWAIT, m->m_type, (m->m_flags & M_PKTHDR) != 0);
	if (head == NULL) {
		m_freem(m);
		return NULL;
	}
	if (m->m_flags & M_PKTHDR) {
		m_move_pkthdr(head, m);
	}
	m_copydata(m, 0, len, head->m_data);
	head->m_len = len;
	m_adj(m, len);
	while (m != NULL && m->m_len == 0) {
		next = m->m_next;
		m_free_one(m);
		m = next;
	}
	head->m_next = m;
	return head;
}

void m_copydata(const struct mbuf *m, int off, size_t len, void *cp) {
	size_t taken;

	while (off > 0 && m != NULL) {
		if (off < m->m_len) {
			break;
		}
		off -= m->m_len;
		m = m->m_next;
	}
	while (len > 0 && m != NULL) {
		taken = (size_t) m->m_len - off < len
			    ? (size_t) m->m_len - off
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

int m_copyback(struct mbuf *m, int off, int len, const void *cp) {
	int total;
	struct mbuf *n;
	int taken;

	total = 0;
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
		memcpy(n->m_data + off, cp, taken);
		cp = (const char *) cp + taken;
		len -= taken;
		off = 0;
	}
	return 0;
}

struct mbuf *m_prepend(struct mbuf *m, int len, int how) {
	struct mbuf *n;

	if (m == NULL || len < 0 || len > MCLBYTES - MH_ALIGN) {
		m_freem(m);
		return NULL;
	}
	if (m != NULL && M_LEADINGSPACE(m) >= len) {
		m->m_data -= len;
		m->m_len += len;
		if (m->m_flags & M_PKTHDR) {
			m->m_pkthdr.len += len;
		}
		return m;
	}
	n = m_get_impl(how, m->m_type, (m->m_flags & M_PKTHDR) != 0);
	if (n == NULL) {
		m_freem(m);
		return NULL;
	}
	n->m_len = len;
	if (m->m_flags & M_PKTHDR) {
		m_move_pkthdr(n, m);
		n->m_pkthdr.len += len;
	}
	n->m_next = m;
	return n;
}

void m_cat(struct mbuf *m, struct mbuf *n) {
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


void m_align(struct mbuf *m, int len) {
	char *base;

	if (m == NULL || len < 0) {
		return;
	}
	base = m->m_cluster + (m->m_flags & M_PKTHDR ? MH_ALIGN : 0);
	m->m_data = base;
	m->m_len = len;
}

struct mbuf *m_copypacket(struct mbuf *m, int how) {
	struct mbuf *n;

	n = m_get_impl(how, m ? m->m_type : MT_DATA,
	    (m != NULL && (m->m_flags & M_PKTHDR)));
	if (n == NULL || m == NULL) {
		m_freem(n);
		return NULL;
	}
	n->m_flags |= m->m_flags & (M_BCAST | M_MCAST);
	memcpy(n->m_cluster, m->m_cluster, m->m_cluster_size);
	n->m_data = n->m_cluster + (m->m_data - m->m_cluster);
	n->m_len = m->m_len;
	if (m->m_flags & M_PKTHDR) {
		n->m_pkthdr.len = m->m_pkthdr.len;
	}
	return n;
}

int m_makewritable(struct mbuf **mp, int off, int len, int wait) {
	(void) mp;
	(void) off;
	(void) len;
	(void) wait;
	/* our clusters are always private and writable */
	return 0;
}

void m_move_pkthdr(struct mbuf *to, struct mbuf *from) {
	to->m_flags = (to->m_flags & ~M_PKTHDR) | (from->m_flags & M_PKTHDR);
	to->m_pkthdr = from->m_pkthdr;
	from->m_flags &= ~M_PKTHDR;
}

/*
 * net80211_mbuf.c - mini-mbuf over a fixed block pool.
 *
 * S8 net80211-adapt spec FR-6: every mbuf owns one contiguous cluster,
 * carved once from a static pool at first use and taken/returned by
 * pointer. The prior project's per-frame heap churn (hundreds of
 * 5.4 KB alloc/free pairs per second fragmenting TLSF until the RX
 * re-arm failed 100% of the time) is the design's counterexample; the
 * pool counts low/short/dup so S8-2's board round can watch the water
 * marks (pool size itself is open question S8-Q5, tuned there).
 */

#include <string.h>

#include "sys/mbuf.h"
#include "sys/systm.h"

/* Leading space inside the cluster: management frames get headers
 * prepended (m_prepend walks backwards), and the USB world copies in,
 * so no DMA line arithmetic is needed - just ordinary headroom. */
#define MH_ALIGN 128
#define MH_BLOCK_ALIGN 64

#define MCLSPACE (MCLBYTES + MH_ALIGN)
#define MBUF_BLOCK_SIZE (sizeof(struct mbuf) + MCLSPACE)

/* S8-Q5 candidate: 128 frames x ~4.2 KB. Scan tops out far below this;
 * S8-2 re-baselines the number on measured water marks. */
#define MBUF_POOL_N 128

struct mbuf_block {
	struct mbuf m;
	char cluster[MCLSPACE];
};
static struct mbuf_block mbuf_pool[MBUF_POOL_N] __attribute__((aligned(MH_BLOCK_ALIGN)));

static int mbuf_pool_inited;
static unsigned char mbuf_free_stack[MBUF_POOL_N];	/* block indices */
static unsigned int mbuf_pool_free;	/* stack top: blocks at home */
unsigned int wlan_mbuf_pool_low;	/* low-water mark */
unsigned int wlan_mbuf_pool_short;	/* requests that found nothing */
unsigned int wlan_mbuf_pool_dups;	/* frees of a block already home */

static void
mbuf_pool_init(void)
{
	unsigned int i;

	for (i = 0; i < MBUF_POOL_N; i++) {
		mbuf_free_stack[i] = (unsigned char)i;
	}
	mbuf_pool_free = MBUF_POOL_N;
	wlan_mbuf_pool_low = MBUF_POOL_N;
	mbuf_pool_inited = 1;
}

static struct mbuf *
mbuf_from_block(struct mbuf_block *b)
{
	return &b->m;
}

static char *
mbuf_cluster_of(struct mbuf *m)
{
	return (char *)m + sizeof(struct mbuf);
}

static struct mbuf *
m_alloc(int type, int pkthdr)
{
	unsigned int idx;
	struct mbuf *m;

	if (!mbuf_pool_inited) {
		mbuf_pool_init();
	}
	if (mbuf_pool_free == 0) {
		wlan_mbuf_pool_short++;
		return NULL;
	}
	idx = mbuf_free_stack[--mbuf_pool_free];
	if (mbuf_pool_free < wlan_mbuf_pool_low) {
		wlan_mbuf_pool_low = mbuf_pool_free;
	}
	m = mbuf_from_block(&mbuf_pool[idx]);
	memset(m, 0, sizeof(*m));

	m->m_type = type;
	m->m_flags = (pkthdr ? M_PKTHDR : 0) | M_EXT; /* cluster always owned */
	m->m_data = mbuf_cluster_of(m) + (pkthdr ? MH_ALIGN : 0);
	m->m_len = 0;
	return m;
}

static void
m_free_one(struct mbuf *m)
{
	unsigned int idx;
	unsigned int i;

	if (m == NULL) {
		return;
	}
	idx = (unsigned int)((struct mbuf_block *)m - mbuf_pool);
	if (idx >= MBUF_POOL_N) {
		wlan_mbuf_pool_dups++;
		return;
	}
	/* A block that is already home must not go in twice: a double
	 * free would hand the same buffer to two owners. */
	for (i = 0; i < mbuf_pool_free; i++) {
		if (mbuf_free_stack[i] == idx) {
			wlan_mbuf_pool_dups++;
			return;
		}
	}
	if (mbuf_pool_free < MBUF_POOL_N) {
		mbuf_free_stack[mbuf_pool_free++] = (unsigned char)idx;
	}
}

struct mbuf *
m_get_impl(int wait, int type, int pkthdr)
{
	(void)wait;
	return m_alloc(type, pkthdr);
}

struct mbuf *
m_getcl_impl(int wait, int type, int pkthdr)
{
	(void)wait;
	return m_alloc(type, pkthdr);
}

void
m_free_impl(struct mbuf *m)
{
	if (m == NULL) {
		return;
	}
	if (m->m_flags & M_PKTHDR) {
		m->m_nextpkt = NULL;
	}
	m_free_one(m);
}

void
m_freem(struct mbuf *m)
{
	struct mbuf *next;

	while (m != NULL) {
		next = m->m_next;
		m_free_one(m);
		m = next;
	}
}

void
m_adj(struct mbuf *mp, int req_len)
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
	/* keep the packet header length in sync */
	if (mp->m_flags & M_PKTHDR) {
		int plen = 0;

		for (m = mp; m != NULL; m = m->m_next) {
			plen += m->m_len;
		}
		mp->m_pkthdr.len = plen;
	}
}

struct mbuf *
m_pullup(struct mbuf *m, int len)
{
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
	head = m_alloc(m->m_type, (m->m_flags & M_PKTHDR) != 0);
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

void
m_copydata(const struct mbuf *m, int off, size_t len, void *cp)
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
		taken = (size_t)m->m_len - off < len
		    ? (size_t)m->m_len - off
		    : len;
		memcpy(cp, m->m_data + off, taken);
		cp = (char *)cp + taken;
		len -= taken;
		off = 0;
		m = m->m_next;
	}
	if (len > 0) {
		kprintf("m_copydata: short read\n");
	}
}

int
m_copyback(struct mbuf *m, int off, int len, const void *cp)
{
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
		cp = (const char *)cp + taken;
		len -= taken;
		off = 0;
	}
	return 0;
}

struct mbuf *
m_prepend(struct mbuf *m, int len, int how)
{
	struct mbuf *n;

	if (m == NULL || len < 0 || len > MCLBYTES - MH_ALIGN) {
		m_freem(m);
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
	n = m_alloc(m->m_type, (m->m_flags & M_PKTHDR) != 0);
	if (n == NULL) {
		m_freem(m);
		return NULL;
	}
	(void)how;
	n->m_len = len;
	if (m->m_flags & M_PKTHDR) {
		m_move_pkthdr(n, m);
		n->m_pkthdr.len += len;
	}
	n->m_next = m;
	return n;
}

void
m_cat(struct mbuf *m, struct mbuf *n)
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

void
m_align(struct mbuf *m, int len)
{
	char *base;

	if (m == NULL || len < 0) {
		return;
	}
	base = mbuf_cluster_of(m) + (m->m_flags & M_PKTHDR ? MH_ALIGN : 0);
	m->m_data = base;
	m->m_len = len;
}

struct mbuf *
m_copypacket(struct mbuf *m, int how)
{
	struct mbuf *n;

	if (m == NULL) {
		return NULL;
	}
	n = m_alloc(m->m_type, (m->m_flags & M_PKTHDR) != 0);
	if (n == NULL) {
		return NULL;
	}
	(void)how;
	n->m_flags |= m->m_flags & (M_BCAST | M_MCAST);
	memcpy(mbuf_cluster_of(n), mbuf_cluster_of(m), MCLSPACE);
	n->m_data = mbuf_cluster_of(n) + (m->m_data - mbuf_cluster_of(m));
	n->m_len = m->m_len;
	if (m->m_flags & M_PKTHDR) {
		n->m_pkthdr.len = m->m_pkthdr.len;
	}
	return n;
}

int
m_makewritable(struct mbuf **mp, int off, int len, int wait)
{
	(void)mp;
	(void)off;
	(void)len;
	(void)wait;
	/* our clusters are always private and writable */
	return 0;
}

void
m_move_pkthdr(struct mbuf *to, struct mbuf *from)
{
	to->m_flags = (to->m_flags & ~M_PKTHDR) | (from->m_flags & M_PKTHDR);
	to->m_pkthdr = from->m_pkthdr;
	from->m_flags &= ~M_PKTHDR;
}

struct mbuf *
m_copym(struct mbuf *m, int off, int len, int wait)
{
	struct mbuf *n;
	int total = 0;
	struct mbuf *t;

	if (m == NULL) {
		return NULL;
	}
	for (t = m; t != NULL; t = t->m_next) {
		total += t->m_len;
	}
	if (off < 0 || off >= total) {
		return NULL;
	}
	if (len < 0 || off + len > total) {
		len = total - off;
	}
	n = m_alloc(m->m_type, m->m_flags & M_PKTHDR ? 1 : 0);
	if (n == NULL) {
		return NULL;
	}
	(void)wait;
	if (m->m_flags & M_PKTHDR) {
		n->m_pkthdr.len = len;
	}
	m_copydata(m, off, (size_t)len, n->m_data);
	n->m_len = len;
	return n;
}

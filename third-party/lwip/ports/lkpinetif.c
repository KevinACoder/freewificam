/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    lkpinetif.c
 * @brief   The netdev half of the FullMAC presentation layer: it binds to the
 *          interface the driver registered, owns the receive hook the netdev face
 *          offers, and hands frames out through the driver's own transmit entry.
 *
 * @design adapter split at the world boundary (see lkpinetif_seam.h).  This
 *         translation unit sees the driver's world - the net device, the receive
 *         hook and the buffer type - and never lwIP's; the stack half is
 *         lkpinetif_lwip.c, and the two meet only through the seam header.  The
 *         classification this file owns is the one thing the netdev face cannot
 *         know: a received frame is either the supplicant's (EAPOL, which the port
 *         installs a sink for) or the stack's, exactly as the net80211 line's
 *         bridge splits them (components/net80211/ports/bridge/wlan_wpa_vendor.c:51-54 the
 *         EAPOL seam it registers).
 *
 * The transmit ownership rule is mac80211's: its ndo_start_xmit consumes the
 * skb on every path (it frees a dropped frame itself), so this adapter hands
 * the skb over through the egress seam and never frees it again.  The seam
 * owns the skb's protocol/dev fields and the tailroom, so the supplicant's
 * EAPOL send and lwIP's linkoutput share exactly one transmit shape.
 *
 * @date    12.10.2026
 */

#include <stddef.h>
#include <string.h>

#include "linuxkpi_core.h"
#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <net/net_namespace.h>

#include "linux80211/net.h"

#ifdef CONFIG_LKPI_CENSUS
#include "lkpi_census.h"
#define	LKPINETIF_NOW_NS()	lkpi_census_now_ns()
#define	LKPINETIF_TX_REQUEST_NOTE()	lkpi_census_tx_request_note()
#else
#define	LKPINETIF_NOW_NS()	((uint64_t)0u)
#define	LKPINETIF_TX_REQUEST_NOTE()	do { } while (0)
#endif

#include "lkpinetif_seam.h"

/* the interface this adapter serves */
static struct net_device *g_dev;

/* how many of the first received frames carry their classification in the log
 * (a diagnostic, bounded so a busy receive path cannot drown the console) */
#define	LKPINETIF_RX_TRACE_MAX	12u

/* the supplicant's EAPOL receive (installed by the port that owns the supplicant) */
static void (*g_eapol_sink)(const uint8_t src[6], const uint8_t *pdu,
    size_t len, void *arg);
static void *g_eapol_arg;

static struct lkpinetif_diag g_diag;

int
lkpinetif_bind(const char *ifname)
{

	if (ifname == NULL)
		return -EINVAL;
	g_dev = dev_get_by_name(&init_net, ifname);
	lkpi_printf(ULOG_DEBUG_LEVEL, "lkpinetif: bound to %s\n",
	    g_dev != NULL ? g_dev->name : "(none)");
	return g_dev != NULL ? 0 : -ENODEV;
}

int
lkpinetif_hwaddr(uint8_t addr[6])
{

	if (g_dev == NULL || g_dev->dev_addr == NULL)
		return -1;
	memcpy(addr, g_dev->dev_addr, ETH_ALEN);
	return 0;
}

void
lkpinetif_set_eapol_sink(void (*fn)(const uint8_t src[6], const uint8_t *pdu,
    size_t len, void *arg), void *arg)
{

	g_eapol_sink = fn;
	g_eapol_arg = arg;
}

void
lkpinetif_get_diag(struct lkpinetif_diag *out)
{

	if (out != NULL)
		*out = g_diag;
}

/* ------------------------------------------------------------------ */
/* transmit                                                            */
/* ------------------------------------------------------------------ */

int
lkpinetif_tx_frame(const uint8_t *frame, size_t len)
{
	uint64_t t0, t1;
	int ret;

	if (frame == NULL || len == 0)
		return -EINVAL;
	if (g_dev == NULL) {
		g_diag.tx_fail++;
		return -ENODEV;
	}

	LKPINETIF_TX_REQUEST_NOTE();
	t0 = LKPINETIF_NOW_NS();
	/* the seam builds the skb (protocol, dev, tailroom) and calls the entry;
	 * the entry owns the skb from here on (see the file's note) */
	ret = lkpi_netdev_xmit(g_dev, frame, len);
	t1 = LKPINETIF_NOW_NS();
	if (ret != 0) {
		g_diag.tx_fail++;
		return ret;
	}
	g_diag.tx_frames++;
	g_diag.tx_bytes += (unsigned long)len;
	g_diag.tx_ndo_ns += (unsigned long long)(t1 - t0);
	if ((unsigned long long)(t1 - t0) > g_diag.tx_ndo_max_ns)
		g_diag.tx_ndo_max_ns = (unsigned long long)(t1 - t0);
	return 0;
}

/* ------------------------------------------------------------------ */
/* receive: the netdev face's hook                                     */
/* ------------------------------------------------------------------ */

/*
 * The stack's receive entry, defined here so the driver's frames have a consumer
 * (the hook is weak in the netdev body: an image without this adapter frees the
 * frames instead, which is the right default and the wrong answer for a data
 * path).  netif_rx has already transferred ownership, so every path below
 * releases the skb exactly once.
 */
void
lkpi_netdev_rx(struct sk_buff *skb)
{
	const struct ethhdr *eth;
	const uint8_t *frame;
	size_t len;
	int ret;
	uint64_t t0, t1;

	if (skb == NULL)
		return;
	g_diag.rx_frames++;
	/* The first frames of a run, with the protocol the handoff classified them
	 * by and the first payload bytes: whether a frame the driver logged as EAPOL
	 * actually arrives as one is the question a stalled handshake turns on. */
	if (g_diag.rx_frames <= LKPINETIF_RX_TRACE_MAX)
		lkpi_printf(ULOG_DEBUG_LEVEL,
		    "lkpinetif: rx#%lu dev=%s(%s) bound=%s proto=0x%04x len=%u data=%02x%02x%02x%02x\n",
		    g_diag.rx_frames,
		    skb->dev != NULL ? skb->dev->name : "?",
		    skb->dev == g_dev ? "ours" : "foreign",
		    g_dev != NULL ? g_dev->name : "?",
		    (unsigned)ntohs(skb->protocol), skb->len,
		    skb->len >= 1 ? skb->data[0] : 0, skb->len >= 2 ? skb->data[1] : 0,
		    skb->len >= 3 ? skb->data[2] : 0, skb->len >= 4 ? skb->data[3] : 0);

	/*
	 * Only the interface this adapter was bound to.  The concurrent-mode build
	 * registers a second (virtual) interface whose frames have no consumer in
	 * this image.
	 */
	if (g_dev == NULL || skb->dev != g_dev) {
		g_diag.rx_foreign++;
		kfree_skb(skb);
		return;
	}

	/*
	 * eth_type_trans() has already pulled the 14-byte header, so skb->data is
	 * the payload: for EAPOL that is the 802.1X PDU the supplicant parses, and
	 * the source address sits the header's own length behind it.
	 */
	eth = (const struct ethhdr *)(skb->data - ETH_HLEN);
	if (skb->protocol == htons(ETH_P_PAE)) {
		g_diag.rx_eapol++;
		if (g_eapol_sink != NULL)
			g_eapol_sink(eth->h_source, skb->data, skb->len,
			    g_eapol_arg);
		kfree_skb(skb);
		return;
	}

	/*
	 * Classify by ethertype before the deliver call: the receive path's
	 * 2026-11-13 open question is whether a broadcast ARP request reaches the
	 * stack at all, and lwIP's ARP table cannot answer it (an unanswered
	 * request leaves no trace).  The ARP opcode is the payload's bytes 6..7,
	 * which is `skb->data` after eth_type_trans()'s pull.
	 */
	if (skb->protocol == htons(ETH_P_ARP)) {
		unsigned int op = 0u;

		if (skb->len >= 8) {
			op = ((unsigned int)skb->data[6] << 8) |
			    (unsigned int)skb->data[7];
		}
		if (op == 2u)
			g_diag.rx_arp_rep++;
		else
			g_diag.rx_arp_req++;
	} else if (skb->protocol == htons(ETH_P_IP)) {
		g_diag.rx_ip++;
	} else {
		if (g_diag.rx_other_n < 4u) {
			unsigned long i = g_diag.rx_other_n;
			const unsigned char *raw = (const unsigned char *)skb->data -
			    ETH_HLEN;
			unsigned int n = (unsigned int)skb->len + ETH_HLEN;
			unsigned int k;

			g_diag.rx_other_len[i] = (unsigned short)n;
			g_diag.rx_other_proto[i] = (unsigned short)ntohs(skb->protocol);
			for (k = 0u; k < 16u && k < n; k++)
				g_diag.rx_other_head[i][k] = raw[k];
			g_diag.rx_other_n++;
		}
		g_diag.rx_other++;
	}

	/* lwIP takes a whole ethernet frame, header included */
	frame = skb->data - ETH_HLEN;
	len = (size_t)skb->len + ETH_HLEN;
	t0 = LKPINETIF_NOW_NS();
	ret = lkpinetif_deliver(frame, len);
	t1 = LKPINETIF_NOW_NS();
	if (ret == -1) {
		g_diag.rx_no_netif++;
	} else if (ret == -2) {
		g_diag.rx_alloc_fail++;
	} else if (ret == -3) {
		g_diag.rx_post_fail++;
	} else if (ret != 0) {
		g_diag.rx_input_fail++;
	} else {
		g_diag.rx_bytes += (unsigned long)len;
		g_diag.rx_deliver_ns += (unsigned long long)(t1 - t0);
		if ((unsigned long long)(t1 - t0) > g_diag.rx_deliver_max_ns)
			g_diag.rx_deliver_max_ns = (unsigned long long)(t1 - t0);
	}
	kfree_skb(skb);
}

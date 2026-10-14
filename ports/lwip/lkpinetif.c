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
 *         bridge splits them (ports/net80211/bridge/wlan_wpa_vendor.c:51-54 the
 *         EAPOL seam it registers).
 *
 * The transmit ownership rule comes from the driver's own entry: it consumes the
 * buffer on every path (os_dep/linux/xmit_linux.c:544-552 drops through
 * rtw_os_pkt_complete, which frees the skb, and the success path attaches it to
 * its transmit frame), so this adapter hands the skb over and never frees it
 * again.
 *
 * @date    12.10.2026
 */

#include <stddef.h>
#include <string.h>

#include "linuxkpi_core.h"
#include "linuxkpi_cfg80211.h"
#include <linux/if_ether.h>
#include <linux/netdevice.h>

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
lkpinetif_bind(struct wiphy *wiphy)
{

	g_dev = lkpicfg_netdev(wiphy);
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
	struct sk_buff *skb;
	uint64_t t0, t1;

	if (frame == NULL || len == 0)
		return -EINVAL;
	if (g_dev == NULL || g_dev->netdev_ops == NULL ||
	    g_dev->netdev_ops->ndo_start_xmit == NULL) {
		g_diag.tx_fail++;
		return -ENODEV;
	}

	skb = lkpi_dev_alloc_skb((unsigned int)len, GFP_KERNEL);
	if (skb == NULL) {
		g_diag.tx_fail++;
		return -ENOMEM;
	}
	if (skb_put(skb, (unsigned int)len) == NULL) {
		kfree_skb(skb);
		g_diag.tx_fail++;
		return -ENOMEM;
	}
	memcpy(skb->data, frame, len);
	g_diag.tx_frames++;
	g_diag.tx_bytes += (unsigned long)len;
	LKPINETIF_TX_REQUEST_NOTE();
	t0 = LKPINETIF_NOW_NS();
	/* the driver's entry owns the skb from here on (see the file's note) */
	(void)g_dev->netdev_ops->ndo_start_xmit(skb, g_dev);
	t1 = LKPINETIF_NOW_NS();
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

	/* lwIP takes a whole ethernet frame, header included */
	frame = skb->data - ETH_HLEN;
	len = (size_t)skb->len + ETH_HLEN;
	t0 = LKPINETIF_NOW_NS();
	ret = lkpinetif_deliver(frame, len);
	t1 = LKPINETIF_NOW_NS();
	if (ret == -1) {
		g_diag.rx_no_netif++;
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

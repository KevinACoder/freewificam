/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    hostap_rx_wrap.c
 * @brief   The link seam that feeds hostapd's AP MLME: the management frames
 *          mac80211 hands to userspace, queued out of the RX path.
 *
 * On Linux an AP's probe request / auth / assoc request reach hostapd through
 * cfg80211_rx_mgmt_ext() (stack/mac80211/rx.c:3999).  This image has no netlink
 * socket (ADR 2026-10-11-nl80211-scope, D1), so the port takes that call over
 * with a link-time --wrap - the same shape as tx_dequeue_context.c - and hands
 * the frame to the imported hostapd stack instead of to a netlink socket.
 *
 * Context split (the same one the previous hand-written MLME used, and for the
 * same reason): this runs inside mac80211's RX handler with rx_path_lock held,
 * so it only memcpy()s under a critical section.  The frame is delivered to
 * hostapd later, from a task (lkpi_hostap_poll), because hostapd's event post
 * yields the CPU and because the MLME's nl80211 commands must not run in the RX
 * path.  The wrap returns false, so mac80211 still treats the frame as
 * unhandled and frees the skb on its own.
 *
 * Everything that is not an AP management frame goes to the real function, so
 * the station-side rounds (C4/C5/C6) are bit-for-bit unaffected.
 */
#include "linuxkpi_core.h"

#include <linux/ieee80211.h>
#include <net/cfg80211.h>

#include "linux80211/hostap_ap.h"

#define HOSTAP_RX_MAX		512u
#define HOSTAP_RX_QUEUE		16u

struct hostap_rx_frame {
	unsigned int	len;
	int		freq;
	unsigned char	buf[HOSTAP_RX_MAX];
};

static struct hostap_rx_frame hostap_rx_queue[HOSTAP_RX_QUEUE];
static unsigned int hostap_rx_head;
static unsigned int hostap_rx_tail;
static unsigned int hostap_rx_dropped;

static DEFINE_SPINLOCK(hostap_rx_lock);

bool __real_cfg80211_rx_mgmt_ext(struct wireless_dev *wdev,
				 struct cfg80211_rx_info *info);

int
lkpi_hostap_rx_push(const void *frame, unsigned int len, int freq)
{
	struct hostap_rx_frame *slot;
	unsigned long flags;

	if (frame == NULL || len < 24u || len > HOSTAP_RX_MAX)
		return -EINVAL;
	spin_lock_irqsave(&hostap_rx_lock, flags);
	if (hostap_rx_head - hostap_rx_tail >= HOSTAP_RX_QUEUE) {
		hostap_rx_dropped++;
		spin_unlock_irqrestore(&hostap_rx_lock, flags);
		return 0;
	}
	slot = &hostap_rx_queue[hostap_rx_head % HOSTAP_RX_QUEUE];
	slot->len = len;
	slot->freq = freq;
	memcpy(slot->buf, frame, len);
	hostap_rx_head++;
	spin_unlock_irqrestore(&hostap_rx_lock, flags);
	return 1;
}

int
lkpi_hostap_poll(void)
{
	struct hostap_rx_frame frame;
	unsigned long flags;
	int rc;

	for (;;) {
		spin_lock_irqsave(&hostap_rx_lock, flags);
		if (hostap_rx_head == hostap_rx_tail) {
			spin_unlock_irqrestore(&hostap_rx_lock, flags);
			return 0;
		}
		frame = hostap_rx_queue[hostap_rx_tail % HOSTAP_RX_QUEUE];
		hostap_rx_tail++;
		spin_unlock_irqrestore(&hostap_rx_lock, flags);

		rc = lkpi_hostap_mgmt_rx(frame.buf, frame.len, frame.freq);
		if (rc != 0)
			return rc;
	}
}

bool
__wrap_cfg80211_rx_mgmt_ext(struct wireless_dev *wdev,
			    struct cfg80211_rx_info *info)
{
	const struct ieee80211_hdr *hdr;

	if (wdev == NULL || info == NULL || info->buf == NULL ||
	    wdev->iftype != NL80211_IFTYPE_AP || info->len < 24u)
		goto real;

	hdr = (const struct ieee80211_hdr *)info->buf;
	if (!ieee80211_is_mgmt(hdr->frame_control))
		goto real;

	/* every management subtype hostapd's MLME may own: probe/auth/assoc/
	 * reassoc requests, deauth, disassoc and the action frames the kernel
	 * did not consume (public action, SA query, ...) */
	(void)lkpi_hostap_rx_push((const unsigned char *)info->buf,
				  (unsigned int)info->len, info->freq);
	return false;

real:
	return __real_cfg80211_rx_mgmt_ext(wdev, info);
}

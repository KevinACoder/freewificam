/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    ap_mlme_rx_wrap.c
 * @brief   The link seam that gives the AP MLME its input: the management frames
 *          mac80211 hands to userspace.
 *
 * On Linux an AP's probe request / auth / assoc request reach hostapd through
 * cfg80211_rx_mgmt_ext() (stack/mac80211/rx.c:3999).  This image has no netlink
 * socket and no hostapd (ADR 2026-11-10-nl80211-scope, D1), so the port takes
 * that call over with a link-time --wrap (the same shape as
 * tx_dequeue_context.c).  The wrap copies the frame into the MLME's queue and
 * returns false, so mac80211 still treats it as an unhandled frame and frees
 * the skb on its own: the port's copy is the port's business, not the stack's.
 *
 * Context: this runs inside mac80211's RX handler chain (rx_path_lock held).
 * It only memcpy()s under a critical section; the nl80211 actions happen later
 * from the case task, in lkpi_ap_mlme_poll().  That split is what keeps
 * NL80211_CMD_FRAME / NEW_STATION out of the RX path.
 *
 * Everything that is not an AP management frame goes to the real function, so
 * the station-side rounds (C4/C5/C6) are bit-for-bit unaffected.
 */
#include "linuxkpi_core.h"

#include <linux/ieee80211.h>
#include <net/cfg80211.h>

#include "linux80211/ap_mlme.h"

bool __real_cfg80211_rx_mgmt_ext(struct wireless_dev *wdev,
				 struct cfg80211_rx_info *info);

bool __wrap_cfg80211_rx_mgmt_ext(struct wireless_dev *wdev,
				 struct cfg80211_rx_info *info)
{
	const struct ieee80211_hdr *hdr;
	unsigned int stype;

	if (!lkpi_ap_mlme_enabled() || wdev == NULL || info == NULL ||
	    info->buf == NULL || wdev->iftype != NL80211_IFTYPE_AP ||
	    info->len < 24u || info->len > LKPI_AP_MLME_FRAME_MAX)
		goto real;

	hdr = (const struct ieee80211_hdr *)info->buf;
	if (!ieee80211_is_mgmt(hdr->frame_control))
		goto real;

	stype = (unsigned int)IEEE80211_FCTL_STYPE &
		le16_to_cpu(hdr->frame_control);
	switch (stype) {
	case IEEE80211_STYPE_PROBE_REQ:
	case IEEE80211_STYPE_AUTH:
	case IEEE80211_STYPE_ASSOC_REQ:
	case IEEE80211_STYPE_REASSOC_REQ:
		(void)lkpi_ap_mlme_rx_push((const unsigned char *)info->buf,
					   (unsigned int)info->len);
		return false;
	default:
		break;
	}

real:
	return __real_cfg80211_rx_mgmt_ext(wdev, info);
}

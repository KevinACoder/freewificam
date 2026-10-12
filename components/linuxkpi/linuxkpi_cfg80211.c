/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_cfg80211.c
 * @brief   The cfg80211 substitute body: the closure of the cfg80211 calls the
 *          extracted vendor driver was written against, implemented on this
 *          project's objects and handed to a consumer through the seam in
 *          linuxkpi_cfg80211.h.
 *
 * @design ports & adapters (the same pattern as the mac80211 substitute,
 *         components/linuxkpi/linuxkpi_80211.c:1-33). The driver is the consumer
 *         of the cfg80211 face (include/net/cfg80211.h is the contract,
 *         unmodified); this file is the adapter-neutral body behind that face,
 *         and the board test / control seam is the adapter that reads a scan
 *         result. The division of labour is the design's rule: cfg80211 owns the
 *         wiphy object, the BSS table and the notification calls a FullMAC
 *         driver makes upward; the MLME (scanning state, association state) is
 *         the driver's own (this is a vendor FullMAC stack, not mac80211) and is
 *         NOT re-implemented here. What each body does is decided by what the
 *         driver does with it, not by cfg80211's full semantics: the call
 *         surface is the closure extracted from the driver's own unresolved
 *         symbols, and each body states the driver call site it serves.
 *
 * Object lifetimes: wiphy_new_nm allocates one zeroed block holding this file's
 * context, then the public wiphy, then the driver-private area (wiphy_priv()).
 * The BSS table behind cfg80211_inform_bss_frame/get_bss/put_bss/unlink_bss is
 * real - a bounded array - because a board test reads it back through
 * lkpicfg_bss_count/lkpicfg_bss_at. The one scan request is built here and kept
 * until the next scan (the driver holds the pointer until it reports done).
 *
 * Reference for the shape: components/linuxkpi/linuxkpi_80211.c:1-33.
 * @date    12.10.2026
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "linuxkpi_core.h"
#include "linuxkpi_cfg80211.h"

#include <linux/if_ether.h>
#include <linux/ieee80211.h>
#include <linux/netdevice.h>
#include <net/cfg80211.h>

/* ------------------------------------------------------------------ */
/* the netdev registry lookup (linuxkpi_netdev.c)                      */
/* ------------------------------------------------------------------ */

/*
 * The device the driver registered for a wiphy, found through the wireless-dev
 * record the driver hung on it (net_device.ieee80211_ptr->wiphy). The body is
 * the net-device registry's; it is declared here rather than in the seam header
 * because the seam is fixed (the board test is written against it) and this is
 * an internal coupling between two files of the same component.
 */
extern struct net_device *lkpi_netdev_by_wiphy(struct wiphy *wiphy);

/* ------------------------------------------------------------------ */
/* storage                                                             */
/* ------------------------------------------------------------------ */

#ifndef CONFIG_LKPICFG_MAX_BSS
#define CONFIG_LKPICFG_MAX_BSS		64
#endif
#ifndef CONFIG_LKPICFG_MAX_WIPHYS
#define CONFIG_LKPICFG_MAX_WIPHYS	2
#endif
#ifndef CONFIG_LKPICFG_MAX_SCAN_CHANNELS
#define CONFIG_LKPICFG_MAX_SCAN_CHANNELS	64
#endif
/*
 * The element stream one table entry keeps.  A supplicant cannot match a PSK
 * network without the frame's RSN/WPA element, so the entry stores what the
 * driver reported; the bound is a table-entry budget, and truncation cuts the
 * stream's tail (the security element sits early, after SSID/rates/DS), so a
 * frame with more elements than the bound still carries the ones that matter.
 */
#ifndef CONFIG_LKPICFG_BSS_IES_MAX
#define CONFIG_LKPICFG_BSS_IES_MAX	256
#endif

/*
 * One access point as the substitute holds it: the public cfg80211 object the
 * driver keeps a pointer to (cfg80211_inform_bss_frame returns it and
 * cfg80211_get_bss/unlink_bss take it back) plus the consumer-facing record the
 * seam hands out. The two live together so the container_of() in the lookups is
 * one subtraction.
 */
struct lkpicfg_bss_entry {
	struct cfg80211_bss	bss;
	struct lkpicfg_bss	info;
	u8			ies[CONFIG_LKPICFG_BSS_IES_MAX];
	size_t			ies_len;
};

/*
 * The wiphy context this file owns. The public wiphy is placed at a fixed
 * offset behind it in the same allocation, and the driver-private area behind
 * that; this prefix is reached back from the wiphy by subtracting its size.
 */
struct lkpicfg_wiphy_ctx {
	const struct cfg80211_ops	*ops;
	struct wiphy			*wiphy;		/* the public object */
	struct lkpicfg_bss_entry	bss[CONFIG_LKPICFG_MAX_BSS];
	unsigned int			bss_count;
	struct cfg80211_scan_request	*scan_req;	/* reused across scans */
	unsigned int			registered:1;
};

static struct lkpicfg_wiphy_ctx *lkpicfg_ctxs[CONFIG_LKPICFG_MAX_WIPHYS];

static const struct lkpicfg_consumer_ops *lkpicfg_consumer;
static void *lkpicfg_consumer_arg;

int
lkpicfg_consumer_register(const struct lkpicfg_consumer_ops *ops, void *arg)
{

	lkpicfg_consumer = ops;
	lkpicfg_consumer_arg = arg;
	(void)lkpicfg_consumer_arg;
	return 0;
}

static struct lkpicfg_wiphy_ctx *
lkpicfg_ctx_of(struct wiphy *wiphy)
{
	unsigned int i;

	if (wiphy == NULL)
		return NULL;
	for (i = 0; i < CONFIG_LKPICFG_MAX_WIPHYS; i++)
		if (lkpicfg_ctxs[i] != NULL && lkpicfg_ctxs[i]->wiphy == wiphy)
			return lkpicfg_ctxs[i];
	return NULL;
}

static void
lkpicfg_ctx_track(struct lkpicfg_wiphy_ctx *ctx)
{
	unsigned int i;

	for (i = 0; i < CONFIG_LKPICFG_MAX_WIPHYS; i++)
		if (lkpicfg_ctxs[i] == NULL || lkpicfg_ctxs[i] == ctx) {
			lkpicfg_ctxs[i] = ctx;
			return;
		}
}

static void
lkpicfg_ctx_untrack(struct lkpicfg_wiphy_ctx *ctx)
{
	unsigned int i;

	for (i = 0; i < CONFIG_LKPICFG_MAX_WIPHYS; i++)
		if (lkpicfg_ctxs[i] == ctx)
			lkpicfg_ctxs[i] = NULL;
}

/* ------------------------------------------------------------------ */
/* wiphy lifecycle (driver: ioctl_cfg80211.c:9905 wiphy_new, :9946      */
/* wiphy_register, :9927 wiphy_free)                                    */
/* ------------------------------------------------------------------ */

struct wiphy *
wiphy_new_nm(const struct cfg80211_ops *ops, int sizeof_priv,
    const char *requested_name)
{
	struct lkpicfg_wiphy_ctx *ctx;
	struct wiphy *wiphy;
	size_t priv = sizeof_priv > 0 ? (size_t)sizeof_priv : 0;

	(void)requested_name;	/* this port names no wiphy device (the driver
				 * passes NULL through wiphy_new) */
	if (ops == NULL) {
		lkpi_printf(ULOG_ERROR_LEVEL, "lkpicfg: wiphy_new without an ops table\n");
		return NULL;
	}
	/*
	 * One block: the context, then the public wiphy at offset
	 * sizeof(context), then the driver-private area at wiphy + priv tail.
	 * Everything zeroed, so the driver's own data starts clean (it reads
	 * wiphy_priv() and fills it).
	 */
	ctx = kzalloc(sizeof(*ctx) + offsetof(struct wiphy, priv) + priv,
	    GFP_KERNEL);
	if (ctx == NULL) {
		lkpi_printf(ULOG_ERROR_LEVEL, "lkpicfg: no memory for a wiphy\n");
		return NULL;
	}
	ctx->ops = ops;
	wiphy = (struct wiphy *)((char *)ctx + sizeof(*ctx));
	ctx->wiphy = wiphy;
	lkpicfg_ctx_track(ctx);
	lkpi_printf(ULOG_INFO_LEVEL, "lkpicfg: wiphy allocated priv=%u\n",
	    (unsigned)priv);
	return wiphy;
}

int
wiphy_register(struct wiphy *wiphy)
{
	struct lkpicfg_wiphy_ctx *ctx = lkpicfg_ctx_of(wiphy);

	if (wiphy == NULL || ctx == NULL)
		return -EINVAL;
	if (ctx->registered)
		return -EALREADY;
	ctx->registered = 1;
	/*
	 * The wiphy's device backlink is the bus device the driver probed with
	 * (set_wiphy_dev) and is read back through hw->wiphy->dev; nothing here
	 * owns a bus device, so the driver's own null-guarded path stays quiet.
	 */
	if (lkpicfg_consumer != NULL && lkpicfg_consumer->attached != NULL)
		lkpicfg_consumer->attached(wiphy);
	lkpi_printf(ULOG_INFO_LEVEL, "lkpicfg: wiphy registered\n");
	return 0;
}

void
wiphy_unregister(struct wiphy *wiphy)
{
	struct lkpicfg_wiphy_ctx *ctx = lkpicfg_ctx_of(wiphy);

	if (ctx == NULL)
		return;
	ctx->registered = 0;
}

void
wiphy_free(struct wiphy *wiphy)
{
	struct lkpicfg_wiphy_ctx *ctx = lkpicfg_ctx_of(wiphy);

	if (ctx == NULL)
		return;
	lkpicfg_ctx_untrack(ctx);
	if (ctx->scan_req != NULL)
		kfree(ctx->scan_req);
	/* the wiphy is the context's second member: the block starts at the ctx */
	kfree(ctx);
}

/*
 * The custom regulatory domain submission (driver: wifi_regd.c:387). Real
 * cfg80211 validates and installs the domain; this port carries no regulatory
 * core, so the domain is stored on the wiphy (the member the face already has)
 * and nothing enforces it. Minimal on purpose: no channel is disabled by it.
 */
void
wiphy_apply_custom_regulatory(struct wiphy *wiphy,
    const struct ieee80211_regdomain *regd)
{

	if (wiphy != NULL)
		wiphy->regd = regd;
}

/*
 * [LNX] include/net/cfg80211.h:8101,8115: the self-managed-regdomain submission
 * (driver: mvm/nvm.c:615/:667, mvm/mac80211.c). Real cfg80211 requires the wiphy
 * to be flagged REGULATORY_WIPHY_SELF_MANAGED and installs the domain through the
 * regulatory core; this port carries no regulatory core, so the domain is stored on
 * the wiphy (the same member wiphy_apply_custom_regulatory writes) and the sync
 * form is the identical store - the interface's sync form only differs in applying
 * immediately under the wiphy lock, and there is no deferred work here to flush.
 * Both answer success, which is what the driver's regulatory setup expects.
 */
int
regulatory_set_wiphy_regd(struct wiphy *wiphy, struct ieee80211_regdomain *rd)
{

	if (wiphy == NULL || rd == NULL)
		return -EINVAL;
	wiphy->regd = rd;
	return 0;
}

int
regulatory_set_wiphy_regd_sync(struct wiphy *wiphy,
    struct ieee80211_regdomain *rd)
{

	return regulatory_set_wiphy_regd(wiphy, rd);
}

/*
 * [LNX] include/net/cfg80211.h:8200: query the internal regulatory database for a
 * WMM rule. This port carries no database (see regulatory_hint,
 * linuxkpi_80211_helpers.c:562), so there is no rule to return and the interface's
 * own "not found" answer is -ENODATA. The caller (iwl-nvm-parse.c:1868) applies a
 * rule only when the query succeeds, so a device whose NVM names a country simply
 * keeps the rule the firmware/regulatory face already set.
 */
int
reg_query_regdb_wmm(char *alpha2, int freq, struct ieee80211_reg_rule *rule)
{

	(void)alpha2;
	(void)freq;
	(void)rule;
	return -ENODATA;
}

/* ------------------------------------------------------------------ */
/* channel lookup (driver: ioctl_cfg80211.c:218, wifi_regd.c:287)       */
/* ------------------------------------------------------------------ */

int
ieee80211_frequency_to_channel(int freq)
{

	/*
	 * [LNX] net/wireless/util.c:ieee80211_frequency_to_channel. The 2.4 GHz
	 * and 5 GHz arms are the ones this chip reaches; the 6 GHz and S1G arms
	 * the interface also carries are left out because no registered band on
	 * this port can ask for them. A frequency outside both arms answers 0.
	 */
	if (freq == 2484)
		return 14;
	if (freq >= 2412 && freq <= 2472)
		return (freq - 2407) / 5;
	if (freq >= 5000 && freq < 5900)
		return (freq - 5000) / 5;
	if (freq == 0)
		return 0;
	return 0;
}

struct ieee80211_channel *
ieee80211_get_channel(struct wiphy *wiphy, int freq)
{
	unsigned int b, c;

	if (wiphy == NULL)
		return NULL;
	for (b = 0; b < NUM_NL80211_BANDS; b++) {
		struct ieee80211_supported_band *band = wiphy->bands[b];

		if (band == NULL)
			continue;
		for (c = 0; c < (unsigned int)band->n_channels; c++)
			if ((int)band->channels[c].center_freq == freq)
				return &band->channels[c];
	}
	return NULL;
}

/* ------------------------------------------------------------------ */
/* element search (driver: mvm/tx.c:736, mvm/ftm-initiator.c:1455)      */
/* ------------------------------------------------------------------ */

/*
 * [LNX] include/net/cfg80211.h:7825-7854: the interface's inline wrapper over
 * cfg80211_find_elem_match() with a match byte array. The offset counts from the
 * element's ID byte (the inline subtracts the two header bytes before calling the
 * element form), the returned pointer is the ID byte, and a nonzero match_len
 * requires match_offset >= 2 - all the inline's contract. The walk below is the
 * same one this component's cfg80211_find_ie() uses (linuxkpi_80211_helpers.c:302);
 * the extra test is the match array, at the caller's offset, bounded by the
 * element's own length.
 */
const u8 *
cfg80211_find_ie_match(u8 eid, const u8 *ies, unsigned int len,
    const u8 *match, unsigned int match_len, unsigned int match_offset)
{
	const u8 *pos = ies;
	const u8 *end = ies + len;

	if (ies == NULL || (int)len <= 0)
		return NULL;
	/* the interface's WARN_ON contract: a nonzero match_len comes with an
	 * offset of at least 2, a zero match_len with an offset of zero */
	if (match_len != 0 && match_offset < 2)
		return NULL;
	if (match_len == 0 && match_offset != 0)
		return NULL;

	while (pos + 2 <= end) {
		if (pos[1] > (size_t)(end - pos - 2))
			return NULL;
		if (pos[0] == eid) {
			if (match_len == 0)
				return pos;
			if (pos[1] >= match_offset - 2 + match_len &&
			    memcmp(pos + match_offset, match, match_len) == 0)
				return pos;
		}
		pos += pos[1] + 2;
	}
	return NULL;
}

/* ------------------------------------------------------------------ */
/* the BSS table (driver: ioctl_cfg80211.c:856 inform, :925 get,        */
/* :895/:897 put, :2604 unlink)                                         */
/* ------------------------------------------------------------------ */

static struct lkpicfg_bss_entry *
lkpicfg_entry_of(struct cfg80211_bss *bss)
{

	return container_of(bss, struct lkpicfg_bss_entry, bss);
}

/* Copy the SSID element out of a management frame's element stream. */
static void
lkpicfg_parse_ssid(const u8 *ies, size_t ielen, struct lkpicfg_bss *out)
{
	size_t i = 0;

	while (i + 2 <= ielen) {
		u8 id = ies[i];
		u8 elen = ies[i + 1];

		if (i + 2 + (size_t)elen > ielen)
			break;
		if (id == WLAN_EID_SSID) {
			u8 n = elen > IEEE80211_MAX_SSID_LEN ?
			    IEEE80211_MAX_SSID_LEN : elen;

			memcpy(out->ssid, ies + i + 2, n);
			out->ssid_len = n;
			return;
		}
		i += 2 + (size_t)elen;
	}
}

struct cfg80211_bss *
cfg80211_inform_bss_frame(struct wiphy *wiphy,
    struct ieee80211_channel *rx_channel,
    struct ieee80211_mgmt *mgmt, size_t len, s32 signal, gfp_t gfp)
{
	struct lkpicfg_wiphy_ctx *ctx = lkpicfg_ctx_of(wiphy);
	struct lkpicfg_bss_entry *entry;
	const u8 *ies;
	size_t ielen;

	(void)gfp;
	if (ctx == NULL || mgmt == NULL)
		return NULL;
	if (ctx->bss_count >= CONFIG_LKPICFG_MAX_BSS) {
		/*
		 * A bounded table: a sweep that hears more than the bound keeps
		 * the first entries rather than overwriting (a reader compares
		 * what it got, and a silently replaced row would be worse).
		 */
		lkpi_printf(ULOG_INFO_LEVEL, "lkpicfg: BSS table full (%u), report dropped\n",
		    (unsigned)CONFIG_LKPICFG_MAX_BSS);
		return NULL;
	}
	/*
	 * The frame the driver builds carries the BSSID in addr3 (mgmt->bssid)
	 * and the beacon/probe-resp fixed fields; the elements follow the
	 * capability word. The signal arrives in mBm (100 * dBm, the driver's
	 * own convention: ioctl_cfg80211.c:806), and the seam carries dBm.
	 */
	entry = &ctx->bss[ctx->bss_count];
	memset(entry, 0, sizeof(*entry));
	memcpy(entry->info.bssid, mgmt->bssid, ETH_ALEN);
	entry->info.capability = le16_to_cpu(mgmt->u.beacon.capab_info);
	entry->info.freq = rx_channel != NULL ? rx_channel->center_freq : 0;
	entry->info.signal = (int)(signal / 100);
	ies = mgmt->u.beacon.variable;
	ielen = (offsetofend(struct ieee80211_mgmt, u.beacon.capab_info) <= len) ?
	    len - offsetofend(struct ieee80211_mgmt, u.beacon.capab_info) : 0;
	lkpicfg_parse_ssid(ies, ielen, &entry->info);

	/*
	 * Keep the element stream itself: the seam's consumer is a supplicant, and
	 * matching a PSK network needs the RSN/WPA element that only lives here
	 * (see the header's note on the borrowed pointer).
	 */
	entry->ies_len = ielen > sizeof(entry->ies) ? sizeof(entry->ies) : ielen;
	if (entry->ies_len != 0)
		memcpy(entry->ies, ies, entry->ies_len);
	entry->info.ies = entry->ies;
	entry->info.ies_len = entry->ies_len;

	/* the public object the driver holds a pointer to */
	memcpy(entry->bss.bssid, mgmt->bssid, ETH_ALEN);
	entry->bss.channel = rx_channel;
	entry->bss.signal = signal;
	entry->bss.capability = entry->info.capability;

	ctx->bss_count++;
	if (lkpicfg_consumer != NULL && lkpicfg_consumer->bss != NULL)
		lkpicfg_consumer->bss(wiphy, &entry->info);
	return &entry->bss;
}

struct cfg80211_bss *
cfg80211_get_bss(struct wiphy *wiphy, struct ieee80211_channel *channel,
    const u8 *bssid, const u8 *ssid, size_t ssid_len,
    enum ieee80211_bss_type bss_type, enum ieee80211_privacy privacy)
{
	struct lkpicfg_wiphy_ctx *ctx = lkpicfg_ctx_of(wiphy);
	unsigned int i;

	(void)channel;
	(void)bss_type;
	(void)privacy;
	if (ctx == NULL)
		return NULL;
	for (i = 0; i < ctx->bss_count; i++) {
		struct lkpicfg_bss *e = &ctx->bss[i].info;

		if (bssid != NULL && memcmp(e->bssid, bssid, ETH_ALEN) != 0)
			continue;
		if (ssid != NULL && ssid_len > 0) {
			if ((size_t)e->ssid_len != ssid_len ||
			    memcmp(e->ssid, ssid, ssid_len) != 0)
				continue;
		}
		return &ctx->bss[i].bss;
	}
	return NULL;
}

/*
 * cfg80211_put_bss() drops the caller's reference. This port's table owns every
 * entry for its whole life, so there is no reference to drop and the call is
 * inert; the driver calls it unconditionally after every lookup.
 */
void
cfg80211_put_bss(struct wiphy *wiphy, struct cfg80211_bss *bss)
{

	(void)wiphy;
	(void)bss;
}

void
cfg80211_unlink_bss(struct wiphy *wiphy, struct cfg80211_bss *bss)
{
	struct lkpicfg_wiphy_ctx *ctx = lkpicfg_ctx_of(wiphy);
	struct lkpicfg_bss_entry *entry;
	unsigned int i;

	if (ctx == NULL || bss == NULL)
		return;
	entry = lkpicfg_entry_of(bss);
	for (i = 0; i < ctx->bss_count; i++)
		if (&ctx->bss[i] == entry) {
			/* shift the tail down: the table stays dense */
			memmove(&ctx->bss[i], &ctx->bss[i + 1],
			    (ctx->bss_count - i - 1) * sizeof(ctx->bss[0]));
			ctx->bss_count--;
			return;
		}
}

/*
 * [LNX] include/net/cfg80211.h:10643: the interface drops every BSS row of a
 * wiphy. The driver calls it on a firmware restart (mvm/fw.c:447), where the rows'
 * GP2 timestamps are stale. This substitute's whole scan-entry store is the one
 * table, so emptying it is the flush; the seam's own clear
 * (lkpicfg_bss_clear, below) is the same operation, kept for the board test.
 */
void
cfg80211_bss_flush(struct wiphy *wiphy)
{
	struct lkpicfg_wiphy_ctx *ctx = lkpicfg_ctx_of(wiphy);

	if (ctx != NULL)
		ctx->bss_count = 0;
}

/* ------------------------------------------------------------------ */
/* scan (driver: ioctl_cfg80211.c:2898 cfg80211_rtw_scan, :2540         */
/* indicate_scan_done)                                                  */
/* ------------------------------------------------------------------ */

void
cfg80211_scan_done(struct cfg80211_scan_request *request,
    struct cfg80211_scan_info *info)
{
	struct wiphy *wiphy;

	(void)info;
	if (request == NULL)
		return;
	wiphy = request->wiphy;
	if (lkpicfg_consumer != NULL && lkpicfg_consumer->scan_done != NULL)
		lkpicfg_consumer->scan_done(wiphy);
}

/* ------------------------------------------------------------------ */
/* the seam's downward entry points (the board test's calls)           */
/* ------------------------------------------------------------------ */

struct wiphy *
lkpicfg_wiphy(void)
{
	unsigned int i;

	for (i = 0; i < CONFIG_LKPICFG_MAX_WIPHYS; i++)
		if (lkpicfg_ctxs[i] != NULL && lkpicfg_ctxs[i]->registered)
			return lkpicfg_ctxs[i]->wiphy;
	return NULL;
}

int
lkpicfg_scan_start(struct wiphy *wiphy)
{
	struct lkpicfg_wiphy_ctx *ctx = lkpicfg_ctx_of(wiphy);
	struct cfg80211_scan_request *req;
	struct ieee80211_supported_band *band;
	struct net_device *dev;
	unsigned int n, i;

	if (ctx == NULL || ctx->ops == NULL || ctx->ops->scan == NULL)
		return -EOPNOTSUPP;
	/*
	 * The scan is driven through the interface the driver registered: the
	 * scan op reads request->wdev and finds its adapter from
	 * wdev->netdev, so the wireless-dev record must be the driver's own
	 * (the one it hung on the net device).
	 */
	dev = lkpi_netdev_by_wiphy(wiphy);
	if (dev == NULL || dev->ieee80211_ptr == NULL)
		return -ENODEV;

	/* one request buffer, reused: the driver keeps the pointer until done */
	if (ctx->scan_req == NULL) {
		ctx->scan_req = kzalloc(offsetof(struct cfg80211_scan_request,
		    channels) + CONFIG_LKPICFG_MAX_SCAN_CHANNELS *
		    sizeof(struct ieee80211_channel *), GFP_KERNEL);
		if (ctx->scan_req == NULL)
			return -ENOMEM;
	}
	req = ctx->scan_req;
	/* clear the header only; the channel array is refilled below */
	memset(req, 0, offsetof(struct cfg80211_scan_request, channels));

	req->wiphy = wiphy;
	req->wdev = dev->ieee80211_ptr;
	req->n_ssids = 0;		/* a broadcast sweep (all SSIDs) */
	req->ssids = NULL;
	req->ie = NULL;
	req->ie_len = 0;

	/*
	 * The channels the device advertises: the 2.4 GHz band the driver
	 * filled in before registering. A band with no channels still produces
	 * a valid (empty) request the driver walks its own way.
	 */
	band = wiphy->bands[NL80211_BAND_2GHZ];
	if (band != NULL) {
		n = (unsigned int)band->n_channels;
		if (n > CONFIG_LKPICFG_MAX_SCAN_CHANNELS)
			n = CONFIG_LKPICFG_MAX_SCAN_CHANNELS;
		for (i = 0; i < n; i++)
			req->channels[i] = &band->channels[i];
		req->n_channels = n;
	}

	lkpi_printf(ULOG_INFO_LEVEL, "lkpicfg: scan start channels=%u\n",
	    (unsigned)req->n_channels);
	return ctx->ops->scan(wiphy, req);
}

unsigned int
lkpicfg_bss_count(struct wiphy *wiphy)
{
	struct lkpicfg_wiphy_ctx *ctx = lkpicfg_ctx_of(wiphy);

	return ctx != NULL ? ctx->bss_count : 0;
}

int
lkpicfg_bss_at(struct wiphy *wiphy, unsigned int idx, struct lkpicfg_bss *out)
{
	struct lkpicfg_wiphy_ctx *ctx = lkpicfg_ctx_of(wiphy);

	if (ctx == NULL || out == NULL || idx >= ctx->bss_count)
		return -EINVAL;
	*out = ctx->bss[idx].info;
	return 0;
}

void
lkpicfg_bss_clear(struct wiphy *wiphy)
{
	struct lkpicfg_wiphy_ctx *ctx = lkpicfg_ctx_of(wiphy);

	if (ctx != NULL)
		ctx->bss_count = 0;
}

int
lkpicfg_get_hwaddr(struct wiphy *wiphy, u8 addr[6])
{

	if (wiphy == NULL || addr == NULL)
		return -EINVAL;
	/* the driver fills perm_addr from the chip's eFuse before registering */
	memcpy(addr, wiphy->perm_addr, ETH_ALEN);
	return 0;
}

struct net_device *
lkpicfg_netdev(struct wiphy *wiphy)
{

	return lkpi_netdev_by_wiphy(wiphy);
}

int
lkpicfg_netdev_up(struct wiphy *wiphy)
{
	struct net_device *dev = lkpi_netdev_by_wiphy(wiphy);

	if (dev == NULL)
		return -ENODEV;
	if (dev->netdev_ops == NULL || dev->netdev_ops->ndo_open == NULL)
		return -EOPNOTSUPP;
	/* the driver's own ndo_open path (rtw netdev_open) */
	return dev->netdev_ops->ndo_open(dev);
}

/* ------------------------------------------------------------------ */
/* the MLME calls (driver: ioctl_cfg80211.c:3834 connect, :4084        */
/* disconnect, :1660 add_key)                                          */
/* ------------------------------------------------------------------ */

/*
 * The security selection a connect request carries, read out of the caller's
 * association elements.  The driver's connect takes the version, the group
 * cipher, the pairwise cipher and the AKM as separate fields of
 * cfg80211_connect_params and reads the group cipher unconditionally
 * (ioctl_cfg80211.c:4019 - a zero there turns its encryption state off even when
 * the pairwise entry is set), so the element stream is the one place these
 * values can come from without this port inventing a policy.
 */
struct lkpicfg_secsel {
	u32	wpa_version;	/* NL80211_WPA_VERSION_*; 0 = no security element */
	u32	group;		/* cipher suite selector, 0 = none */
	u32	pairwise;	/* first pairwise suite, 0 = none */
	u32	akm;		/* first AKM suite, 0 = none */
};

/* A four-byte selector ([LNX] include/linux/ieee80211.h: the
 * 00:0f:ac:xx space, which is what WLAN_CIPHER_SUITE_* / WLAN_AKM_SUITE_* are) */
static u32
lkpicfg_selector(const u8 *p)
{

	return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

/*
 * Read one security element's body (the fields after the element's own two-byte
 * header).  The RSN element (WLAN_EID_RSN) and the WPA one (a vendor element
 * whose body starts after its OUI and type) carry the same field order; the WPA
 * form spells its selectors in the 00:50:f2 OUI, whose last byte is the same
 * suite number, so it is mapped into the 802.11 selector space the driver
 * compares against.  `sel->wpa_version` then says which element it was.
 */
static int
lkpicfg_parse_security_body(const u8 *body, size_t len, int wpa1,
    struct lkpicfg_secsel *sel)
{
	const u8 *p = body;
	const u8 *end = body + len;
	u16 n;

	if (len < 6)		/* version + one four-byte selector */
		return -1;
	p += 2;			/* the element's version field */
	sel->group = wpa1 ? SUITE(0x000fac, p[3]) : lkpicfg_selector(p);
	p += 4;
	if (p + 2 > end)
		return -1;
	/* The element's counts are little-endian on the wire (the selectors below
	 * are four-byte arrays and read in wire order, which is why the two
	 * spellings differ). */
	n = (u16)(p[0] | (p[1] << 8));
	p += 2;
	if (n == 0 || p + 4 > end)
		return -1;
	sel->pairwise = wpa1 ? SUITE(0x000fac, p[3]) : lkpicfg_selector(p);
	p += 4 * (size_t)n;
	if (p + 2 > end)
		return -1;
	n = (u16)(p[0] | (p[1] << 8));
	p += 2;
	if (n == 0 || p + 4 > end)
		return -1;
	sel->akm = wpa1 ? SUITE(0x000fac, p[3]) : lkpicfg_selector(p);
	sel->wpa_version = wpa1 ? NL80211_WPA_VERSION_1 : NL80211_WPA_VERSION_2;
	return 0;
}

static void
lkpicfg_parse_security(const u8 *ies, size_t ielen, struct lkpicfg_secsel *sel)
{
	size_t i = 0;

	memset(sel, 0, sizeof(*sel));
	while (i + 2 <= ielen) {
		u8 id = ies[i];
		u8 elen = ies[i + 1];

		if (i + 2 + (size_t)elen > ielen)
			break;
		if (id == WLAN_EID_RSN) {
			if (lkpicfg_parse_security_body(ies + i + 2, elen, 0,
			    sel) == 0)
				return;
		} else if (id == WLAN_EID_VENDOR_SPECIFIC && elen >= 4 + 6 &&
		    ies[i + 2] == 0x00 && ies[i + 3] == 0x50 &&
		    ies[i + 4] == 0xf2 && ies[i + 5] == 0x01) {
			if (lkpicfg_parse_security_body(ies + i + 6,
			    (size_t)elen - 4, 1, sel) == 0)
				return;
		}
		i += 2 + (size_t)elen;
	}
}

int
lkpicfg_connect(struct wiphy *wiphy, const u8 *ssid, size_t ssid_len,
    const u8 *bssid, const u8 *ie, size_t ie_len)
{
	struct lkpicfg_wiphy_ctx *ctx = lkpicfg_ctx_of(wiphy);
	struct cfg80211_connect_params sme;
	struct lkpicfg_secsel sel;
	struct net_device *dev;

	if (ctx == NULL || ctx->ops == NULL || ctx->ops->connect == NULL)
		return -EOPNOTSUPP;
	if (ssid == NULL || ssid_len == 0 || ssid_len > IEEE80211_MAX_SSID_LEN)
		return -EINVAL;
	dev = lkpi_netdev_by_wiphy(wiphy);
	if (dev == NULL)
		return -ENODEV;

	memset(&sme, 0, sizeof(sme));
	sme.ssid = ssid;
	sme.ssid_len = ssid_len;
	sme.bssid = bssid;
	sme.auth_type = NL80211_AUTHTYPE_OPEN_SYSTEM;
	sme.ie = ie;
	sme.ie_len = ie_len;
	sme.privacy = (ie != NULL && ie_len != 0);

	lkpicfg_parse_security(ie, ie_len, &sel);
	sme.crypto.wpa_versions = sel.wpa_version;
	sme.crypto.cipher_group = sel.group;
	if (sel.pairwise != 0) {
		sme.crypto.n_ciphers_pairwise = 1;
		sme.crypto.ciphers_pairwise[0] = sel.pairwise;
	}
	if (sel.akm != 0) {
		sme.crypto.n_akm_suites = 1;
		sme.crypto.akm_suites[0] = sel.akm;
	}

	lkpi_printf(ULOG_INFO_LEVEL,
	    "lkpicfg: connect ssid_len=%u bssid=%s ie=%u ver=0x%x group=0x%x pairwise=0x%x akm=0x%x\n",
	    (unsigned)ssid_len, bssid != NULL ? "pinned" : "any", (unsigned)ie_len,
	    (unsigned)sel.wpa_version, (unsigned)sel.group,
	    (unsigned)sel.pairwise, (unsigned)sel.akm);
	return ctx->ops->connect(wiphy, dev, &sme);
}

int
lkpicfg_disconnect(struct wiphy *wiphy, u16 reason)
{
	struct lkpicfg_wiphy_ctx *ctx = lkpicfg_ctx_of(wiphy);
	struct net_device *dev;

	if (ctx == NULL || ctx->ops == NULL || ctx->ops->disconnect == NULL)
		return -EOPNOTSUPP;
	dev = lkpi_netdev_by_wiphy(wiphy);
	if (dev == NULL)
		return -ENODEV;
	return ctx->ops->disconnect(wiphy, dev, reason);
}

int
lkpicfg_set_key(struct wiphy *wiphy, unsigned int key_idx, int pairwise,
    const u8 *mac_addr, const u8 *key, size_t key_len,
    const u8 *seq, size_t seq_len)
{
	struct lkpicfg_wiphy_ctx *ctx = lkpicfg_ctx_of(wiphy);
	struct key_params params;
	struct net_device *dev;

	if (ctx == NULL || ctx->ops == NULL || ctx->ops->add_key == NULL)
		return -EOPNOTSUPP;
	if (key == NULL || key_len == 0)
		return -EINVAL;
	dev = lkpi_netdev_by_wiphy(wiphy);
	if (dev == NULL || dev->ieee80211_ptr == NULL)
		return -ENODEV;

	memset(&params, 0, sizeof(params));
	params.key = key;
	params.key_len = (int)key_len;
	params.seq = seq;
	params.seq_len = (int)seq_len;
	params.cipher = WLAN_CIPHER_SUITE_CCMP;

	lkpi_printf(ULOG_INFO_LEVEL,
	    "lkpicfg: set key idx=%u pairwise=%d len=%u seq=%u\n",
	    key_idx, pairwise, (unsigned)key_len, (unsigned)seq_len);
	/* link_id 0: this port's interface has no multi-link identity, and the
	 * driver's add_key ignores it (ioctl_cfg80211.c:1660-1668) */
	return ctx->ops->add_key(wiphy, dev->ieee80211_ptr, 0, (u8)key_idx,
	    pairwise != 0, mac_addr, &params);
}

/* ------------------------------------------------------------------ */
/* the connection/link notifications (driver: ioctl_cfg80211.c:1129,    */
/* :1195, :1200, :1212)                                                 */
/* ------------------------------------------------------------------ */

static struct wiphy *
lkpicfg_wiphy_of_netdev(struct net_device *dev)
{

	if (dev == NULL || dev->ieee80211_ptr == NULL)
		return NULL;
	return dev->ieee80211_ptr->wiphy;
}

void
cfg80211_connect_result(struct net_device *dev, const u8 *bssid,
    const u8 *req_ie, size_t req_ie_len, const u8 *resp_ie,
    size_t resp_ie_len, u16 status, gfp_t gfp)
{
	struct wiphy *wiphy = lkpicfg_wiphy_of_netdev(dev);

	(void)req_ie;
	(void)req_ie_len;
	(void)resp_ie;
	(void)resp_ie_len;
	(void)gfp;
	if (lkpicfg_consumer != NULL && lkpicfg_consumer->connected != NULL)
		lkpicfg_consumer->connected(wiphy, bssid, status);
}

void
cfg80211_disconnected(struct net_device *dev, u16 reason, const u8 *ie,
    size_t ie_len, bool locally_generated, gfp_t gfp)
{
	struct wiphy *wiphy = lkpicfg_wiphy_of_netdev(dev);

	(void)ie;
	(void)ie_len;
	(void)locally_generated;
	(void)gfp;
	if (lkpicfg_consumer != NULL && lkpicfg_consumer->disconnected != NULL)
		lkpicfg_consumer->disconnected(wiphy, reason);
}

void
cfg80211_roamed(struct net_device *dev, struct cfg80211_roam_info *info,
    gfp_t gfp)
{
	struct wiphy *wiphy = lkpicfg_wiphy_of_netdev(dev);
	const u8 *bssid = NULL;

	(void)gfp;
	if (info != NULL)
		bssid = info->links[0].bssid;
	if (lkpicfg_consumer != NULL && lkpicfg_consumer->connected != NULL)
		lkpicfg_consumer->connected(wiphy, bssid, 0);
}

void
cfg80211_ibss_joined(struct net_device *dev, const u8 *bssid,
    struct ieee80211_channel *channel, gfp_t gfp)
{
	struct wiphy *wiphy = lkpicfg_wiphy_of_netdev(dev);

	(void)channel;
	(void)gfp;
	if (lkpicfg_consumer != NULL && lkpicfg_consumer->connected != NULL)
		lkpicfg_consumer->connected(wiphy, bssid, 0);
}

/* ------------------------------------------------------------------ */
/* the notifications with no consumer on this port                      */
/* ------------------------------------------------------------------ */

/*
 * AP-mode, mesh, TDLS, remain-on-channel, channel-switch and vendor reports are
 * a declared non-goal for this port (the SDIO port's spec names the STA mainline
 * as the target). Each body below is inert for the same reason: nothing on this
 * port listens for the event, so it is dropped rather than invented, and the
 * driver's own call site still returns cleanly.
 */
void
cfg80211_new_sta(struct wireless_dev *wdev, const u8 *mac_addr,
    struct station_info *sinfo, gfp_t gfp)
{

	(void)wdev;
	(void)mac_addr;
	(void)sinfo;
	(void)gfp;
}

void
cfg80211_del_sta(struct wireless_dev *wdev, const u8 *mac_addr, gfp_t gfp)
{

	(void)wdev;
	(void)mac_addr;
	(void)gfp;
}

void
cfg80211_michael_mic_failure(struct net_device *dev, const u8 *addr,
    enum nl80211_key_type key_type, int key_id, const u8 *tsc, gfp_t gfp)
{

	(void)dev;
	(void)addr;
	(void)key_type;
	(void)key_id;
	(void)tsc;
	(void)gfp;
}

void
cfg80211_ch_switch_notify(struct net_device *dev,
    struct cfg80211_chan_def *chandef, unsigned int link_id)
{

	(void)dev;
	(void)chandef;
	(void)link_id;
}

bool
cfg80211_rx_mgmt(struct wireless_dev *wdev, int freq, int sig_dbm,
    const u8 *buf, size_t len, u32 flags)
{

	/*
	 * A received management frame handed up for user-space processing: this
	 * port has no nl80211 listener, so the frame is not consumed and the
	 * driver is told so (false = "not delivered"). The scan path reports its
	 * candidates through cfg80211_inform_bss_frame, not here.
	 */
	(void)wdev;
	(void)freq;
	(void)sig_dbm;
	(void)buf;
	(void)len;
	(void)flags;
	return false;
}

void
cfg80211_mgmt_tx_status(struct wireless_dev *wdev, u64 cookie, const u8 *buf,
    size_t len, bool ack, gfp_t gfp)
{

	(void)wdev;
	(void)cookie;
	(void)buf;
	(void)len;
	(void)ack;
	(void)gfp;
}

void
cfg80211_ready_on_channel(struct wireless_dev *wdev, u64 cookie,
    struct ieee80211_channel *chan, unsigned int duration, gfp_t gfp)
{

	(void)wdev;
	(void)cookie;
	(void)chan;
	(void)duration;
	(void)gfp;
}

void
cfg80211_remain_on_channel_expired(struct wireless_dev *wdev, u64 cookie,
    struct ieee80211_channel *chan, gfp_t gfp)
{

	(void)wdev;
	(void)cookie;
	(void)chan;
	(void)gfp;
}

/*
 * [LNX] include/net/cfg80211.h:8659: an unprotected deauth/disassoc the driver
 * dropped in station mode. Unreachable on this line - the only callers are the MQ
 * receive path (mvm/rxmq.c:409) and the MLD path (mld/rx.c:1901), and the AC 7260
 * is a non-MQ, non-MLD part. The body is inert for the same reason every other
 * notification here is: no nl80211 listener exists on this port to be told.
 */
void
cfg80211_rx_unprot_mlme_mgmt(struct net_device *dev, const u8 *buf, size_t len)
{

	(void)dev;
	(void)buf;
	(void)len;
}

/*
 * [LNX] include/net/cfg80211.h:10394: close every interface of a wiphy after a
 * fatal error. Unreachable on this line - its one caller is the MEI nic-stolen
 * handler (mvm/ops.c:1185), built only with CONFIG_IWLMEI, whose units this line
 * excludes. There is no interface to close and no error to recover from, so the
 * body is inert.
 */
void
cfg80211_shutdown_all_interfaces(struct wiphy *wiphy)
{

	(void)wiphy;
}

/*
 * [LNX] include/net/cfg80211.h:10529,10543: the peer-measurement result and
 * completion reports. Unreachable on this line - the FTM initiator that makes them
 * (mvm/ftm-initiator.c) runs only for a userspace peer-measurement request that
 * arrives through nl80211, and this port has no nl80211 socket. Inert for that
 * reason.
 */
void
cfg80211_pmsr_report(struct wireless_dev *wdev,
    struct cfg80211_pmsr_request *req, struct cfg80211_pmsr_result *result,
    gfp_t gfp)
{

	(void)wdev;
	(void)req;
	(void)result;
	(void)gfp;
}

void
cfg80211_pmsr_complete(struct wireless_dev *wdev,
    struct cfg80211_pmsr_request *req, gfp_t gfp)
{

	(void)wdev;
	(void)req;
	(void)gfp;
}

/* ------------------------------------------------------------------ */
/* the vendor-command reply surface (driver: rtw_cfgvendor.c)           */
/* ------------------------------------------------------------------ */

/*
 * A vendor reply is a netlink skb handed back to user space. This port has no
 * netlink socket, so the builders allocate the skb the driver fills and the
 * send/reply calls release it - the driver's own accounting stays exact and
 * nothing leaks, but no bytes leave the image.
 */
struct sk_buff *
cfg80211_vendor_cmd_alloc_reply_skb(struct wiphy *wiphy, int approxlen)
{

	(void)wiphy;
	return alloc_skb(approxlen > 0 ? (unsigned int)approxlen : 1,
	    GFP_KERNEL);
}

int
cfg80211_vendor_cmd_reply(struct sk_buff *skb)
{

	if (skb != NULL)
		kfree_skb(skb);
	return 0;
}

struct sk_buff *
cfg80211_vendor_event_alloc(struct wiphy *wiphy, struct wireless_dev *wdev,
    int approxlen, int event_idx, gfp_t gfp)
{

	(void)wiphy;
	(void)wdev;
	(void)event_idx;
	(void)gfp;
	return alloc_skb(approxlen > 0 ? (unsigned int)approxlen : 1,
	    GFP_KERNEL);
}

void
cfg80211_vendor_event(struct sk_buff *skb, gfp_t gfp)
{

	(void)gfp;
	if (skb != NULL)
		kfree_skb(skb);
}

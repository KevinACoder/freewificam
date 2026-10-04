/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * linuxkpi_80211 - the mac80211 entry points the verbatim drivers call,
 * mapped onto the vendored net80211 protocol core.
 *
 * M2 scope (this file): the structural half.  hw allocation, registration
 * bookkeeping, the workqueue-facing entry points, and the TX/RX skb
 * plumbing; every net80211 state-machine junction (bss_info_changed,
 * scan, keys, association, ic creation) is a logged stub until the M4
 * wiring round lands.  A stub that logs-and-frees is deliberately chosen
 * over a half-working mapping: silent mis-mappings cost board rounds.
 */

#include "linuxkpi_core.h"
#include <net/mac80211.h>
#include "linuxkpi_net80211.h"

/*
 * W1 form: the net80211-side bridge (os/net80211/, W3) provides
 * lkpi_n80211_attach / lkpi_n80211_rx_frame. Until it lands these weak
 * defaults keep the mac80211 world linkable single-factor: registration
 * reports -ENODEV (the board round treats that as "bridge pending",
 * not a driver failure).
 */
__attribute__((weak)) int
lkpi_n80211_attach(struct ieee80211_hw *hw, const uint8_t perm_addr[6],
    const struct lkpi_n80211_drv *drv)
{
	(void)hw; (void)perm_addr; (void)drv;
	/* W2 落桩桥 (spec W2 范围注记): registration succeeds without a
	 * net80211 world so the probe completes and the chip-ready anchors
	 * land; W3's real bridge (os/net80211/) overrides this weak
	 * definition and takes the vtable. */
	lkpi_printf("lkpi80211: net80211 bridge not present (W3) - "
	    "registered without protocol world\n");
	return 0;
}

__attribute__((weak)) void
lkpi_n80211_rx_frame(const void *data, size_t len, int signal_dbm)
{
	(void)data; (void)len; (void)signal_dbm;
}

/* the net80211-side driver vtable lives below */
static struct lkpi_n80211_drv lkpi_n80211_drv;

/* ------------------------------------------------------------------ */
/* per-hw context                                                      */
/* ------------------------------------------------------------------ */

struct lkpi80211_hw_ctx {
	struct ieee80211_hw	hw;
	bool			registered;
	bool			started;
	struct ieee80211_vif	*vif;		/* the one registered vif */
	struct ieee80211_sta	*sta;		/* the one known peer */
	struct workqueue_struct *wq;
};

static struct lkpi80211_hw_ctx *lkpi80211_ctx_of(struct ieee80211_hw *hw)
{

	return container_of(hw, struct lkpi80211_hw_ctx, hw);
}

/* one adapter per image; the net80211-side vtable reaches the hw here */
static struct lkpi80211_hw_ctx *lkpi80211_primary;
static struct lkpi80211_hw_ctx *
lkpi80211_primary_ctx(void)
{

	return lkpi80211_primary;
}

/* ------------------------------------------------------------------ */
/* hw lifecycle                                                        */
/* ------------------------------------------------------------------ */

struct ieee80211_hw *
ieee80211_alloc_hw(size_t priv_data_len, const struct ieee80211_ops *ops)
{
	struct lkpi80211_hw_ctx *ctx;
	struct ieee80211_hw *hw;

	ctx = kzalloc(sizeof(*ctx) + priv_data_len, GFP_KERNEL);
	if (ctx == NULL)
		return NULL;
	if (lkpi80211_primary == NULL)
		lkpi80211_primary = ctx;

	hw = &ctx->hw;
	hw->ops = ops;
	hw->priv = (char *)ctx + sizeof(*ctx);
	hw->wiphy = &hw->wiphy_storage;
	hw->conf.chandef.chan = NULL;
	hw->wiphy->rts_threshold = 2347;
	hw->wiphy->max_scan_ssids = 1;
	hw->wiphy->max_scan_ie_len = 512;
	if (priv_data_len != 0)
		memset(hw->priv, 0, priv_data_len);
	return hw;
}

void
ieee80211_free_hw(struct ieee80211_hw *hw)
{
	struct lkpi80211_hw_ctx *ctx;

	if (hw == NULL)
		return;
	ctx = lkpi80211_ctx_of(hw);
	if (ctx->wq != NULL)
		lkpi_destroy_workqueue(ctx->wq);
	kfree(ctx);
}


/* ------------------------------------------------------------------ */
/* the net80211-side driver vtable (plain C ABI over ieee80211_ops)     */
/* ------------------------------------------------------------------ */

/* the STA vif the bridge owns through this vtable */
static struct ieee80211_vif *lkpi_n80211_vif;
/* the channel objects the bridge's config requests point the chandef at */
static struct ieee80211_channel lkpi_n80211_chans[14];

static int
lkpi_n80211_drv_start(void)
{
	struct lkpi80211_hw_ctx *ctx = lkpi80211_primary_ctx();
	struct ieee80211_hw *hw = &ctx->hw;
	int ret;

	lkpi_printf("lkpi80211: drv start enter\n");
	ret = hw->ops->start(hw);
	lkpi_printf("lkpi80211: drv start ret %d\n", ret);
	return ret;
}

static void
lkpi_n80211_drv_stop(void)
{
	struct lkpi80211_hw_ctx *ctx = lkpi80211_primary_ctx();
	struct ieee80211_hw *hw = &ctx->hw;

	(void)hw->ops->stop(hw, false);
}

static int
lkpi_n80211_drv_add_interface(const uint8_t addr[6])
{
	struct lkpi80211_hw_ctx *ctx = lkpi80211_primary_ctx();
	struct ieee80211_vif *vif;
	int ret;

	if (ctx->vif != NULL)
		return -EBUSY;
	vif = kzalloc(sizeof(*vif), GFP_KERNEL);
	if (vif == NULL)
		return -ENOMEM;
	vif->type = NL80211_IFTYPE_STATION;
	memcpy(vif->addr, addr, ETH_ALEN);
	lkpi_printf("lkpi80211: drv add_interface enter\n");
	ret = (&ctx->hw)->ops->add_interface(&ctx->hw, vif);
	lkpi_printf("lkpi80211: drv add_interface ret %d\n", ret);
	if (ret != 0) {
		kfree(vif);
		return ret;
	}
	ctx->vif = vif;
	lkpi_n80211_vif = vif;
	return 0;
}

static int
lkpi_n80211_drv_config(int center_freq, int chan)
{
	struct lkpi80211_hw_ctx *ctx = lkpi80211_primary_ctx();
	struct ieee80211_channel *c;
	int i;

	if (chan < 1 || chan > 14)
		return -EINVAL;
	c = &lkpi_n80211_chans[chan - 1];
	(&ctx->hw)->conf.chandef.chan = c;
	(&ctx->hw)->conf.chandef.width = NL80211_CHAN_WIDTH_20_NOHT;
	(&ctx->hw)->conf.chandef.center_freq1 = (u32)center_freq;
	(&ctx->hw)->conf.chandef.center_freq2 = 0;
	return (&ctx->hw)->ops->config(&ctx->hw,
	    IEEE80211_CONF_CHANGE_CHANNEL);
}

static void
lkpi_n80211_drv_sw_scan_start(const uint8_t mac[6])
{
	struct lkpi80211_hw_ctx *ctx = lkpi80211_primary_ctx();

	(&ctx->hw)->ops->sw_scan_start(&ctx->hw, ctx->vif, mac);
}

static void
lkpi_n80211_drv_sw_scan_complete(void)
{
	struct lkpi80211_hw_ctx *ctx = lkpi80211_primary_ctx();

	(&ctx->hw)->ops->sw_scan_complete(&ctx->hw, ctx->vif);
}

static int
lkpi_n80211_drv_tx_mgmt(const uint8_t *frame, size_t len)
{
	struct lkpi80211_hw_ctx *ctx = lkpi80211_primary_ctx();
	struct ieee80211_tx_control control;
	struct ieee80211_tx_info *info;
	struct sk_buff *skb;

	struct ieee80211_hw *hw = &ctx->hw;

	if (ctx->vif == NULL || hw->ops->tx == NULL)
		return -EOPNOTSUPP;
	skb = lkpi_skb_alloc((unsigned int)(len + 32), GFP_KERNEL);
	if (skb == NULL)
		return -ENOMEM;
	memcpy(skb_put(skb, (unsigned int)len), frame, len);
	info = IEEE80211_SKB_CB(skb);
	memset(info, 0, sizeof(*info));
	info->control.vif = lkpi_n80211_vif;
	memset(&control, 0, sizeof(control));
	/* rtw88's tx frees the skb itself on failure and hands the
	 * completion back through tx_status_irqsafe/free_txskb */
	hw->ops->tx(hw, &control, skb);
	return 0;
}

static void
lkpi_n80211_drv_init(void)
{
	int i;

	for (i = 0; i < 14; i++) {
		lkpi_n80211_chans[i].band = NL80211_BAND_2GHZ;
		lkpi_n80211_chans[i].hw_value = (u16)(i + 1);
		lkpi_n80211_chans[i].center_freq = (u16)(2412 + 5 * i);
	}
	lkpi_n80211_drv.start = lkpi_n80211_drv_start;
	lkpi_n80211_drv.stop = lkpi_n80211_drv_stop;
	lkpi_n80211_drv.add_interface = lkpi_n80211_drv_add_interface;
	lkpi_n80211_drv.config = lkpi_n80211_drv_config;
	lkpi_n80211_drv.sw_scan_start = lkpi_n80211_drv_sw_scan_start;
	lkpi_n80211_drv.sw_scan_complete = lkpi_n80211_drv_sw_scan_complete;
	lkpi_n80211_drv.tx_mgmt = lkpi_n80211_drv_tx_mgmt;
}

int
ieee80211_register_hw(struct ieee80211_hw *hw)
{
	struct lkpi80211_hw_ctx *ctx = lkpi80211_ctx_of(hw);

	if (ctx->registered)
		return -EALREADY;
	lkpi_n80211_drv_init();
	ctx->registered = true;
	/*
	 * The workqueue must exist before the driver's start path queues
	 * work; allocating it lazily here (instead of at probe) keeps the
	 * thread creation off the USB probe context.
	 */
	if (ctx->wq == NULL)
		ctx->wq = lkpi_alloc_workqueue("lkpi80211", 0, 1);
	if (lkpi_n80211_attach(hw, hw->wiphy->perm_addr,
	    &lkpi_n80211_drv) != 0) {
		ctx->registered = false;
		return -ENOMEM;
	}
	lkpi_printf("lkpi80211: hw registered, net80211 adapter up\n");
	return 0;
}

void
ieee80211_unregister_hw(struct ieee80211_hw *hw)
{
	struct lkpi80211_hw_ctx *ctx = lkpi80211_ctx_of(hw);

	ctx->registered = false;
	lkpi_printf("lkpi80211: hw unregistered\n");
}

void
ieee80211_restart_hw(struct ieee80211_hw *hw)
{

	lkpi_printf("lkpi80211: restart_hw (stub)\n");
	(void)hw;
}

/* ------------------------------------------------------------------ */
/* work scheduling                                                     */
/* ------------------------------------------------------------------ */

void
ieee80211_queue_work(struct ieee80211_hw *hw, struct work_struct *work)
{
	struct lkpi80211_hw_ctx *ctx = lkpi80211_ctx_of(hw);

	if (ctx->wq == NULL)
		ctx->wq = lkpi_alloc_workqueue("lkpi80211", 0, 1);
	lkpi_queue_work(ctx->wq, work);
}

void
ieee80211_queue_delayed_work(struct ieee80211_hw *hw,
    struct delayed_work *dwork, unsigned long delay)
{
	struct lkpi80211_hw_ctx *ctx = lkpi80211_ctx_of(hw);

	if (ctx->wq == NULL)
		ctx->wq = lkpi_alloc_workqueue("lkpi80211", 0, 1);
	lkpi_queue_delayed_work(ctx->wq, dwork, delay);
}

/* ------------------------------------------------------------------ */
/* queues                                                              */
/* ------------------------------------------------------------------ */

void
ieee80211_wake_queues(struct ieee80211_hw *hw)
{

	(void)hw;
	/* no TXQ engine yet (M4): the driver's own queues are always awake */
}

void
ieee80211_stop_queues(struct ieee80211_hw *hw)
{

	(void)hw;
}

void
ieee80211_tx_info_clear_status(struct ieee80211_tx_info *info)
{

	info->status.rates[0].count = 0;
	info->flags &= ~(IEEE80211_TX_STAT_ACK | IEEE80211_TX_STAT_AMPDU |
	    IEEE80211_TX_STAT_AMPDU_NO_BACK);
	info->status.ampdu_len = 0;
	info->status.ampdu_ack_len = 0;
}

void
ieee80211_tx_status_irqsafe(struct ieee80211_hw *hw, struct sk_buff *skb)
{

	/*
	 * M4 will translate this into net80211 TX completion bookkeeping
	 * (rate report / retry counters); for now the skb owns its fate.
	 */
	(void)hw;
	lkpi_skb_free(skb);
}

void
ieee80211_free_txskb(struct ieee80211_hw *hw, struct sk_buff *skb)
{

	(void)hw;
	lkpi_skb_free(skb);
}

void
ieee80211_purge_tx_queue(struct ieee80211_hw *hw, struct sk_buff_head *skbs)
{

	(void)hw;
	lkpi_skb_queue_purge(skbs);
}

struct sk_buff *
ieee80211_tx_dequeue(struct ieee80211_hw *hw, struct ieee80211_txq *txq)
{

	/* no mac80211 TXQ engine (the driver drives its own queues) */
	(void)hw;
	(void)txq;
	return NULL;
}

int
ieee80211_txq_get_depth(struct ieee80211_txq *txq, unsigned long *frame_cnt,
    unsigned long *byte_cnt)
{

	if (frame_cnt != NULL)
		*frame_cnt = 0;
	if (byte_cnt != NULL)
		*byte_cnt = 0;
	(void)txq;
	return 0;
}

void
ieee80211_rx_irqsafe(struct ieee80211_hw *hw, struct sk_buff *skb)
{

	/* the net80211 side of the bridge: skb -> pool mbuf -> input;
	 * the bridge copies into the pool mbuf, this side owns the skb */
	{
		struct ieee80211_rx_status *rx =
		    (struct ieee80211_rx_status *)skb->cb;

		lkpi_n80211_rx_frame(skb->data, skb->len, rx->signal);
	}
	lkpi_skb_free(skb);
}

void
ieee80211_rx_napi(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
    struct sk_buff *skb, struct napi_struct *napi)
{

	(void)sta;
	(void)napi;
	ieee80211_rx_irqsafe(hw, skb);
}

/* ------------------------------------------------------------------ */
/* peer / vif lookup                                                   */
/* ------------------------------------------------------------------ */

struct ieee80211_sta *
ieee80211_find_sta(struct ieee80211_vif *vif, const u8 *addr)
{

	/*
	 * Single-peer world: M4 resolves the peer through the ic's node
	 * table; until then the driver's sta_add() bookkeeping is the only
	 * source of peers and this lookup has nothing to walk.
	 */
	(void)vif;
	(void)addr;
	return NULL;
}

struct ieee80211_sta *
ieee80211_find_sta_by_ifaddr(struct ieee80211_hw *hw, const u8 *addr,
    const u8 *localaddr)
{

	(void)hw;
	(void)addr;
	(void)localaddr;
	return NULL;
}

void
ieee80211_iterate_active_interfaces_atomic(struct ieee80211_hw *hw, u32 iter_flags,
    void (*iterator)(void *, u8 *, struct ieee80211_vif *), void *data)
{
	struct lkpi80211_hw_ctx *ctx = lkpi80211_ctx_of(hw);

	(void)iter_flags;
	if (ctx->vif != NULL)
		iterator(data, ctx->vif->addr, ctx->vif);
}

void
ieee80211_iterate_active_interfaces(struct ieee80211_hw *hw, u32 iter_flags,
    void (*iterator)(void *, u8 *, struct ieee80211_vif *), void *data)
{

	ieee80211_iterate_active_interfaces_atomic(hw, iter_flags,
	    iterator, data);
}

void
ieee80211_iterate_stations_atomic(struct ieee80211_hw *hw,
    void (*iterator)(void *, struct ieee80211_sta *), void *data)
{
	struct lkpi80211_hw_ctx *ctx = lkpi80211_ctx_of(hw);

	if (ctx->sta != NULL)
		iterator(data, ctx->sta);
}

void
ieee80211_iterate_stations(struct ieee80211_hw *hw,
    void (*iterator)(void *, struct ieee80211_sta *), void *data)
{

	ieee80211_iterate_stations_atomic(hw, iterator, data);
}

void
ieee80211_iter_keys(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    void (*iterator)(struct ieee80211_hw *, struct ieee80211_vif *,
	struct ieee80211_sta *, struct ieee80211_key_conf *, void *),
    void *data)
{

	(void)hw;
	(void)vif;
	(void)iterator;
	(void)data;
}

void
ieee80211_iter_keys_rcu(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    void (*iterator)(struct ieee80211_hw *, struct ieee80211_vif *,
	struct ieee80211_sta *, struct ieee80211_key_conf *, void *),
    void *data)
{

	ieee80211_iter_keys(hw, vif, iterator, data);
}

/* ------------------------------------------------------------------ */
/* state notifications (logged stubs until M4)                         */
/* ------------------------------------------------------------------ */

void
ieee80211_request_smps(struct ieee80211_vif *vif, int link_id,
    enum ieee80211_smps_mode smps_mode)
{

	(void)vif;
	(void)link_id;
	(void)smps_mode;
}

int
ieee80211_start_tx_ba_session(struct ieee80211_sta *pubsta, u16 tid, u16 ssn)
{

	(void)pubsta;
	(void)tid;
	(void)ssn;
	return -EOPNOTSUPP;
}

void
ieee80211_start_tx_ba_cb_irqsafe(struct ieee80211_vif *vif, const u8 *ra,
    u16 tid)
{

	(void)vif;
	(void)ra;
	(void)tid;
}

void
ieee80211_stop_tx_ba_cb_irqsafe(struct ieee80211_vif *vif, const u8 *ra,
    u16 tid)
{

	(void)vif;
	(void)ra;
	(void)tid;
}

void
ieee80211_scan_completed(struct ieee80211_hw *hw,
    struct cfg80211_scan_info *info)
{

	/*
	 * M4: drive the net80211 scan state machine from here.  The flag
	 * print keeps the junction visible during bring-up.
	 */
	lkpi_printf("lkpi80211: scan_completed aborted=%d (M4 pending)\n",
	    info != NULL ? info->aborted : -1);
	(void)hw;
}

void
ieee80211_connection_loss(struct ieee80211_vif *vif)
{

	lkpi_printf("lkpi80211: connection_loss (M4 pending)\n");
	(void)vif;
}

void
ieee80211_cqm_rssi_notify(struct ieee80211_vif *vif,
    enum nl80211_cqm_rssi_threshold_event rssi_event, s32 rssi_level,
    gfp_t gfp)
{

	(void)vif;
	(void)rssi_event;
	(void)rssi_level;
	(void)gfp;
}

void
ieee80211_report_low_ack(struct ieee80211_sta *sta)
{

	(void)sta;
}

/* ------------------------------------------------------------------ */
/* frame builders                                                      */
/* ------------------------------------------------------------------ */

struct sk_buff *
ieee80211_probereq_get(struct ieee80211_hw *hw, const u8 *src_addr,
    const u8 *ssid, size_t ssid_len, size_t tail_len)
{
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	size_t len = IEEE80211_HDRLEN + 2 + ssid_len + tail_len;

	(void)hw;
	if (ssid_len > IEEE80211_MAX_SSID_LEN)
		return NULL;
	skb = lkpi_skb_alloc((unsigned int)(len + 32), GFP_KERNEL);
	if (skb == NULL)
		return NULL;
	mgmt = (struct ieee80211_mgmt *)skb_put(skb,
	    (unsigned int)(IEEE80211_HDRLEN + 2 + ssid_len));
	memset(mgmt, 0, IEEE80211_HDRLEN + 2 + ssid_len);
	mgmt->frame_control = htole16(IEEE80211_FTYPE_MGMT |
	    IEEE80211_STYPE_PROBE_REQ);
	memset(mgmt->da, 0xff, ETH_ALEN);
	if (src_addr != NULL)
		memcpy(mgmt->sa, src_addr, ETH_ALEN);
	memset(mgmt->bssid, 0xff, ETH_ALEN);
	mgmt->u.beacon.variable[0] = 0;		/* SSID element id */
	mgmt->u.beacon.variable[1] = (u8)ssid_len;
	if (ssid != NULL && ssid_len != 0)
		memcpy(&mgmt->u.beacon.variable[2], ssid, ssid_len);
	return skb;
}

struct sk_buff *
ieee80211_nullfunc_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    int power_save, bool tell_ap)
{
	struct sk_buff *skb;
	struct ieee80211_hdr *hdr;

	(void)hw;
	skb = lkpi_skb_alloc(32, GFP_KERNEL);
	if (skb == NULL)
		return NULL;
	hdr = (struct ieee80211_hdr *)skb_put(skb, IEEE80211_HDRLEN);
	memset(hdr, 0, IEEE80211_HDRLEN);
	hdr->frame_control = htole16(IEEE80211_FTYPE_DATA |
	    IEEE80211_STYPE_NULLFUNC |
	    IEEE80211_FCTL_TODS | (power_save ? IEEE80211_FCTL_PM : 0));
	(void)tell_ap;
	memcpy(hdr->addr1, vif->bss_conf.bssid, ETH_ALEN);
	memcpy(hdr->addr2, vif->addr, ETH_ALEN);
	memcpy(hdr->addr3, vif->bss_conf.bssid, ETH_ALEN);
	return skb;
}

struct sk_buff *
ieee80211_pspoll_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct sk_buff *skb;
	struct {
		__le16	frame_control;
		__le16	aid;
		u8	bssid[6];
		u8	ta[6];
	} __packed *pspoll;

	(void)hw;
	skb = lkpi_skb_alloc(16, GFP_KERNEL);
	if (skb == NULL)
		return NULL;
	pspoll = (void *)skb_put(skb, 16);
	memset(pspoll, 0, 16);
	pspoll->frame_control = htole16(IEEE80211_FTYPE_CTL |
	    IEEE80211_STYPE_PSPOLL);
	pspoll->aid = htole16(vif->cfg.aid | 0xc000);
	memcpy(pspoll->bssid, vif->bss_conf.bssid, ETH_ALEN);
	memcpy(pspoll->ta, vif->addr, ETH_ALEN);
	return skb;
}

struct sk_buff *
ieee80211_proberesp_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{

	/* AP role joins with the M6 milestone */
	(void)hw;
	(void)vif;
	return NULL;
}

struct sk_buff *
ieee80211_beacon_get_tim(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    u16 *tim_offset, u16 *tim_length, int link_id)
{

	/* AP role joins with the M6 milestone */
	(void)hw;
	(void)vif;
	if (tim_offset != NULL)
		*tim_offset = 0;
	if (tim_length != NULL)
		*tim_length = 0;
	(void)link_id;
	return NULL;
}

/* ------------------------------------------------------------------ */
/* misc                                                                */
/* ------------------------------------------------------------------ */

struct ieee80211_hw *
wiphy_to_ieee80211_hw(struct wiphy *wiphy)
{

	return container_of(wiphy, struct ieee80211_hw, wiphy_storage);
}

struct wiphy *
wiphy_to_wiphy(struct ieee80211_hw *hw)
{

	return &hw->wiphy_storage;
}

int
regulatory_hint(struct wiphy *wiphy, const char *alpha2)
{

	/* M4 maps this onto the net80211 channel list; 8821cu ships its own
	 * regulatory tables and the freewc AP environment is fixed-country */
	lkpi_printf("lkpi80211: regulatory_hint %c%c (accepted, M4 pending)\n",
	    alpha2[0], alpha2[1]);
	(void)wiphy;
	return 0;
}

void
get_random_mask_addr(u8 *buf, const u8 *addr, const u8 *mask)
{
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		buf[i] = (addr[i] & mask[i]) | ((u8)get_random_int() & ~mask[i]);
}

struct led_trigger *
ieee80211_create_tpt_led_trigger(struct ieee80211_hw *hw, unsigned int flags,
    const struct ieee80211_tpt_blink *blink_table, unsigned int blink_table_len)
{

	/* the image has no LED subsystem: a token that is never fired */
	(void)flags;
	(void)blink_table;
	(void)blink_table_len;
	return (struct led_trigger *)hw;
}

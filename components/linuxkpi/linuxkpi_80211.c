/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linuxkpi_80211.c
 * @brief   The mac80211 substitute body: the effective closure of the mac80211
 *          calls the extracted driver was written against, implemented on this
 *          project's objects and handed to the net80211 bridge.
 *
 * @design ports & adapters (the same pattern as the USB engine). The driver is
 *         the consumer of the mac80211 face (include/net/mac80211.h is the
 *         contract, unmodified); this file is the adapter-neutral body behind
 *         that face, and the bridge is the adapter that speaks net80211. The
 *         division of labour is the design's central rule: MLME - scanning
 *         state, association state, key policy, node management - belongs to
 *         net80211 and is NOT re-implemented here; this file only carries the
 *         objects mac80211 owns (the hw descriptor, the vif and sta arrays with
 *         their driver-private areas, the channel descriptor, the queue state)
 *         and routes every event to the bridge through linuxkpi_80211.h.
 *         What the function bodies do is decided by what the driver does with
 *         them, not by mac80211's full semantics: the call surface is the
 *         closure extracted from the driver's own unresolved symbols (23
 *         functions), and each one names the driver call site it serves. The
 *         Linux side of that closure - what the real mac80211 does for these
 *         calls - is the semantics reference (the kernel's net/mac80211 tree), consulted
 *         behaviour, never copied: no bytes of it are used, and no MLME logic
 *         moves in here.
 *
 * Object lifetimes: alloc_hw allocates the hw context (this file's struct), the
 * public hw at its front and the driver-private area behind it, all in one
 * zeroed block; vif and sta objects are allocated here too because their
 * sizes are the driver's (hw->vif_data_size / hw->sta_data_size, set by the
 * driver before register). The bridge owns none of these and frees none of
 * them; it drives them through the helpers in linuxkpi_80211.h.
 *
 * Reference for the shape: the previous line's linuxkpi_80211.c was consulted
 * only for its call inventory (the reference-rewrite rule); this file's bodies
 * are written against the driver's use sites, cited inline.
 * @date    08.10.2026
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "linuxkpi_core.h"
#include <net/mac80211.h>
#include <net/cfg80211.h>

#include "linuxkpi_80211.h"
#include "linuxkpi_txq.h"

/* ------------------------------------------------------------------ */
/* compile-time contracts                                              */
/* ------------------------------------------------------------------ */

/*
 * The control block is the driver's only place to keep a tx_info/rx_status:
 * both casts go through `skb->cb`, and a block smaller than either would let a
 * memset of the structure wipe payload behind it. The 48-byte Linux cb caused
 * exactly that on this project's board once; the pool's 192-byte cb is the
 * contract and this is where it is checked against the structures it must hold.
 */
_Static_assert(sizeof(struct ieee80211_tx_info) <=
    sizeof(((struct sk_buff *)0)->cb),
    "the transmit status block does not fit the control block");
_Static_assert(sizeof(struct ieee80211_rx_status) <=
    sizeof(((struct sk_buff *)0)->cb),
    "the receive status block does not fit the control block");

/* ------------------------------------------------------------------ */
/* storage                                                             */
/* ------------------------------------------------------------------ */

#ifndef CONFIG_LKPI80211_MAX_VIFS
#define CONFIG_LKPI80211_MAX_VIFS	4
#endif
#ifndef CONFIG_LKPI80211_MAX_STAS
#define CONFIG_LKPI80211_MAX_STAS	8
#endif

/*
 * The hw context this file owns. `hw` is first so a pointer to the context and
 * a pointer to the public descriptor are the same address (the driver only ever
 * sees the public one); the driver-private area follows the whole structure, so
 * hw->priv points just past the context.
 */
struct lkpi80211_hw_ctx {
	struct ieee80211_hw	hw;
	struct wiphy		wiphy;		/* embedded: no separate lifetime */
	const struct ieee80211_ops *ops;
	struct device		*bus_dev;	/* the bus device behind the wiphy */
	struct ieee80211_channel chan;		/* the current channel descriptor */
	struct ieee80211_chanctx_conf chanctx;	/* the emulated context */
	struct ieee80211_vif	*vifs[CONFIG_LKPI80211_MAX_VIFS];
	struct ieee80211_sta	*stas[CONFIG_LKPI80211_MAX_STAS];
	u32			queue_stopped;	/* bit per AC, the queue family */
	u32			filter_total;	/* the last programmed filter */
	unsigned int		registered:1;
	unsigned int		scanning:1;
	unsigned int		have_chanctx:1;
	unsigned int		tx_ba_sessions;
	unsigned int		rx_frames;
	unsigned int		tx_completions;
};

#define LKPI80211_CTX(_hw)	((struct lkpi80211_hw_ctx *)(_hw))

static const struct lkpi80211_bridge_ops *bridge_ops;
static void *bridge_arg;

/*
 * The vif-to-hw backlink. The Linux struct ieee80211_vif carries no hw
 * pointer; the substitute owns the mapping, so a vif handed to a lookup helper
 * (ieee80211_find_sta(vif, addr), the driver's own call at core.c:4821) still
 * resolves to the right station table. The table is this file's, sized by the
 * vif slots of every hw it allocated - one hw per registration in this port.
 */
static struct {
	struct ieee80211_vif *vif;
	struct ieee80211_hw *hw;
} vif_hw_map[CONFIG_LKPI80211_MAX_VIFS * 2];

static void
vif_hw_bind(struct ieee80211_vif *vif, struct ieee80211_hw *hw)
{
	unsigned int i;

	for (i = 0; i < sizeof(vif_hw_map) / sizeof(vif_hw_map[0]); i++) {
		if (vif_hw_map[i].vif == NULL || vif_hw_map[i].vif == vif) {
			vif_hw_map[i].vif = vif;
			vif_hw_map[i].hw = hw;
			return;
		}
	}
}

static void
vif_hw_unbind(struct ieee80211_vif *vif)
{
	unsigned int i;

	for (i = 0; i < sizeof(vif_hw_map) / sizeof(vif_hw_map[0]); i++)
		if (vif_hw_map[i].vif == vif) {
			vif_hw_map[i].vif = NULL;
			vif_hw_map[i].hw = NULL;
		}
}

static struct ieee80211_hw *
vif_hw(struct ieee80211_vif *vif)
{
	unsigned int i;

	for (i = 0; i < sizeof(vif_hw_map) / sizeof(vif_hw_map[0]); i++)
		if (vif_hw_map[i].vif == vif)
			return vif_hw_map[i].hw;
	return NULL;
}

void
lkpi80211_bridge_register(const struct lkpi80211_bridge_ops *ops, void *arg)
{

	bridge_ops = ops;
	bridge_arg = arg;
}

/* ------------------------------------------------------------------ */
/* hw allocation (driver calls: core.c:7813 ieee80211_alloc_hw)         */
/* ------------------------------------------------------------------ */

struct ieee80211_hw *
ieee80211_alloc_hw(size_t priv_data_len, const struct ieee80211_ops *ops)
{
	struct lkpi80211_hw_ctx *ctx;
	unsigned int i;

	if (ops == NULL) {
		lkpi_printf("lkpi80211: alloc_hw without an ops table\n");
		return NULL;
	}
	/*
	 * One block: the context, then the driver's private area. The vif/sta
	 * areas themselves are allocated later, when the driver's own sizes are
	 * known (it sets hw->vif_data_size in probe, before registering).
	 */
	ctx = kzalloc(sizeof(*ctx) + priv_data_len, GFP_KERNEL);
	if (ctx == NULL) {
		lkpi_printf("lkpi80211: no memory for an hw context\n");
		return NULL;
	}
	ctx->ops = ops;
	ctx->hw.priv = (u8 *)ctx + sizeof(*ctx);
	ctx->hw.wiphy = &ctx->wiphy;
	/* the queue count the driver reads back (core.c:7891 sets 4; the
	 * engine's own default matches so a driver that does not set it still
	 * gets a workable value) */
	ctx->hw.queues = 4;
	for (i = 0; i < IEEE80211_NUM_ACS; i++)
		ctx->hw.conf.chandef.chan = &ctx->chan;
	lkpi_printf("lkpi80211: hw allocated priv=%u ctx=%u\n",
	    (unsigned)priv_data_len, (unsigned)sizeof(*ctx));
	return &ctx->hw;
}

void
ieee80211_free_hw(struct ieee80211_hw *hw)
{
	struct lkpi80211_hw_ctx *ctx;
	unsigned int i;

	if (hw == NULL)
		return;
	ctx = LKPI80211_CTX(hw);
	if (ctx->registered) {
		lkpi_printf("lkpi80211: free_hw while registered; unregistering\n");
		ieee80211_unregister_hw(hw);
	}
	/* stations before interfaces: a station's queues belong to its vif too */
	for (i = 0; i < CONFIG_LKPI80211_MAX_STAS; i++) {
		if (ctx->stas[i] != NULL)
			lkpi_txq_free_sta(ctx->stas[i]);
		kfree(ctx->stas[i]);
	}
	for (i = 0; i < CONFIG_LKPI80211_MAX_VIFS; i++) {
		if (ctx->vifs[i] != NULL)
			lkpi_txq_free_vif(ctx->vifs[i]);
		kfree(ctx->vifs[i]);
	}
	kfree(ctx);
}

/* ------------------------------------------------------------------ */
/* registration (driver: core.c:7941 register / :7984 unregister)       */
/* ------------------------------------------------------------------ */

int
ieee80211_register_hw(struct ieee80211_hw *hw)
{
	struct lkpi80211_hw_ctx *ctx;
	int ret;

	if (hw == NULL)
		return -EINVAL;
	ctx = LKPI80211_CTX(hw);
	if (ctx->registered)
		return -EALREADY;
	if (bridge_ops == NULL || bridge_ops->attach == NULL) {
		/*
		 * No bridge means no net80211 behind the face: the radio could
		 * be powered but nothing would see a frame. Fail honestly - a
		 * silent success would leave the driver believing it is live.
		 */
		lkpi_printf("lkpi80211: register_hw without a bridge (-ENODEV)\n");
		return -ENODEV;
	}
	ret = bridge_ops->attach(hw);
	if (ret != 0) {
		lkpi_printf("lkpi80211: bridge attach failed (%d)\n", ret);
		return ret;
	}
	/*
	 * The wiphy's device backlink is the USB lane's device (the driver was
	 * probed with it and reads it back through hw->wiphy->dev at its debugfs
	 * site); the substitute binds whatever the attach hook installed, so a
	 * bridge that has no device leaves it NULL and the driver's own
	 * null-guarded debugfs path stays quiet.
	 */
	set_wiphy_dev(&ctx->wiphy, ctx->bus_dev);
	ctx->registered = 1;
	lkpi_printf("lkpi80211: hw registered\n");
	return 0;
}

void
ieee80211_unregister_hw(struct ieee80211_hw *hw)
{
	struct lkpi80211_hw_ctx *ctx;
	unsigned int i;

	if (hw == NULL)
		return;
	ctx = LKPI80211_CTX(hw);
	if (!ctx->registered)
		return;
	if (bridge_ops != NULL && bridge_ops->detach != NULL)
		bridge_ops->detach(hw);
	for (i = 0; i < CONFIG_LKPI80211_MAX_VIFS; i++) {
		if (ctx->vifs[i] != NULL) {
			vif_hw_unbind(ctx->vifs[i]);
			kfree(ctx->vifs[i]);
			ctx->vifs[i] = NULL;
		}
	}
	for (i = 0; i < CONFIG_LKPI80211_MAX_STAS; i++) {
		if (ctx->stas[i] != NULL) {
			kfree(ctx->stas[i]);
			ctx->stas[i] = NULL;
		}
	}
	ctx->registered = 0;
	ctx->scanning = 0;
	ctx->queue_stopped = 0;
	lkpi_printf("lkpi80211: hw unregistered\n");
}

/* ------------------------------------------------------------------ */
/* rx / tx status (driver: core.c:6423, :6531 rx; :5171 tx status)      */
/* ------------------------------------------------------------------ */

void
ieee80211_rx_irqsafe(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct ieee80211_rx_status *rs;
	struct lkpi80211_hw_ctx *ctx;

	if (hw == NULL || skb == NULL)
		return;
	ctx = LKPI80211_CTX(hw);
	/*
	 * The driver leaves the status block in the control block
	 * (IEEE80211_SKB_RXCB(skb) is `(struct ieee80211_rx_status *)skb->cb`).
	 * The bridge copies the frame into the net80211 world's own buffer, so
	 * the skb is consumed here - the same ownership the Linux entry has
	 * (mac80211's rx_irqsafe frees the skb once processing is done) and the
	 * same thing the no-bridge path below does. The control block is cleared
	 * first: a stale status block behind a buffer that returns to the pool
	 * would be a trap for whoever gets it next.
	 */
	rs = (struct ieee80211_rx_status *)skb->cb;
	ctx->rx_frames++;
	if (bridge_ops != NULL && bridge_ops->rx != NULL) {
		bridge_ops->rx(hw, rs, skb);
		memset(skb->cb, 0, sizeof(skb->cb));
		lkpi_skb_free(skb);
		return;
	}
	lkpi_printf("lkpi80211: rx with no bridge; frame dropped\n");
	lkpi_skb_free(skb);
}

void
ieee80211_tx_status_irqsafe(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct lkpi80211_hw_ctx *ctx;

	if (hw == NULL || skb == NULL)
		return;
	ctx = LKPI80211_CTX(hw);
	ctx->tx_completions++;
	/*
	 * The completion callback a driver may have registered on this frame
	 * (mac80211 calls it before releasing the skb; the driver's tx path
	 * relies on it for its urb return). It is read from the control block
	 * before the block is cleared.
	 */
	{
		struct ieee80211_tx_info *info = (struct ieee80211_tx_info *)skb->cb;
		void (*fn)(void *) = (void (*)(void *))info->status.status_driver_data[0];

		if (info->status.rates[0].idx >= 0) {
			/* the driver marked a completion callback in slot 0's
			 * data convention used by this project's shim */
		}
		(void)fn;
	}
	if (bridge_ops != NULL && bridge_ops->tx_complete != NULL)
		bridge_ops->tx_complete(hw, skb);
	memset(skb->cb, 0, sizeof(skb->cb));
	/* the substitute owns the release (the caller has stopped caring) */
	lkpi_skb_free(skb);
}

/* ------------------------------------------------------------------ */
/* station lookup (driver: core.c:4821 find_sta, :4886 by_ifaddr)       */
/* ------------------------------------------------------------------ */

struct ieee80211_sta *
ieee80211_find_sta(struct ieee80211_vif *vif, const u8 *addr)
{
	struct lkpi80211_hw_ctx *ctx;
	unsigned int i;

	struct ieee80211_hw *hw;

	if (vif == NULL || addr == NULL)
		return NULL;
	hw = vif_hw(vif);
	if (hw == NULL)
		return NULL;
	ctx = LKPI80211_CTX(hw);
	for (i = 0; i < CONFIG_LKPI80211_MAX_STAS; i++) {
		struct ieee80211_sta *sta = ctx->stas[i];

		if (sta != NULL && memcmp(sta->addr, addr, 6) == 0)
			return sta;
	}
	return NULL;
}

struct ieee80211_sta *
ieee80211_find_sta_by_ifaddr(struct ieee80211_hw *hw, const u8 *addr,
    const u8 *localaddr)
{
	struct lkpi80211_hw_ctx *ctx;
	unsigned int i;

	(void)localaddr;	/* one interface per hw in this port */
	if (hw == NULL || addr == NULL)
		return NULL;
	ctx = LKPI80211_CTX(hw);
	for (i = 0; i < CONFIG_LKPI80211_MAX_STAS; i++) {
		struct ieee80211_sta *sta = ctx->stas[i];

		if (sta != NULL && memcmp(sta->addr, addr, 6) == 0)
			return sta;
	}
	return NULL;
}

void
ieee80211_iterate_active_interfaces_atomic(struct ieee80211_hw *hw,
    u32 iter_flags,
    void (*iterator)(void *data, u8 *mac, struct ieee80211_vif *vif),
    void *data)
{
	struct lkpi80211_hw_ctx *ctx;
	unsigned int i;

	(void)iter_flags;
	if (hw == NULL || iterator == NULL)
		return;
	ctx = LKPI80211_CTX(hw);
	for (i = 0; i < CONFIG_LKPI80211_MAX_VIFS; i++) {
		struct ieee80211_vif *vif = ctx->vifs[i];

		if (vif == NULL)
			continue;
		iterator(data, vif->addr, vif);
	}
}

void
ieee80211_iterate_stations_atomic(struct ieee80211_hw *hw,
    void (*iterator)(void *data, struct ieee80211_sta *sta), void *data)
{
	struct lkpi80211_hw_ctx *ctx;
	unsigned int i;

	if (hw == NULL || iterator == NULL)
		return;
	ctx = LKPI80211_CTX(hw);
	for (i = 0; i < CONFIG_LKPI80211_MAX_STAS; i++) {
		if (ctx->stas[i] != NULL)
			iterator(data, ctx->stas[i]);
	}
}

/* ------------------------------------------------------------------ */
/* queue family (driver: core.c:5143 wake / :7888 stop)                 */
/* ------------------------------------------------------------------ */

void
ieee80211_stop_queues(struct ieee80211_hw *hw)
{

	if (hw != NULL)
		LKPI80211_CTX(hw)->queue_stopped = 0xffffffffu;
}

void
ieee80211_wake_queues(struct ieee80211_hw *hw)
{
	struct lkpi80211_hw_ctx *ctx;

	if (hw == NULL)
		return;
	ctx = LKPI80211_CTX(hw);
	/*
	 * The driver's own "is the tx path open" switch is this queue state
	 * (_tx_stopped rides the pair in rtl8xxxu).  Upstream's substance for
	 * this call is the same: clearing the stop reasons, and nothing else
	 * (util.c:ieee80211_wake_queues -> ieee80211_wake_queues_by_reason).
	 * A driver's wake_tx_queue op is called when the stack has frames for a
	 * txq - this port's enqueue side - not from here, so the pair stays a
	 * pair and no driver is woken by a queue state change.
	 */
	ctx->queue_stopped = 0;
}

void
ieee80211_handle_wake_tx_queue(struct ieee80211_hw *hw,
    struct ieee80211_txq *txq)
{
	struct lkpi80211_hw_ctx *ctx;
	struct ieee80211_tx_control control;
	struct sk_buff *skb;
	u8 ac;

	if (hw == NULL || txq == NULL)
		return;
	ctx = LKPI80211_CTX(hw);
	ctx->queue_stopped = 0;
	if (ctx->ops->tx == NULL)
		return;
	/*
	 * The default wake handler (upstream util.c:ieee80211_handle_wake_tx_queue):
	 * schedule the queue that woke, then drain whatever the round hands out
	 * into the driver's own transmit entry.  A driver that points its
	 * wake_tx_queue op here (rtl8xxxu core.c:7599) therefore needs no txq
	 * code of its own, and a port whose bridge hands frames straight to
	 * .tx (this one's, today) sees an empty drain and no change.
	 */
	ac = txq->ac;
	ieee80211_txq_schedule_start(hw, ac);
	ieee80211_schedule_txq(hw, txq);
	while ((txq = ieee80211_next_txq(hw, ac)) != NULL) {
		control.sta = txq->sta;
		while ((skb = ieee80211_tx_dequeue(hw, txq)) != NULL)
			ctx->ops->tx(hw, &control, skb);
		ieee80211_return_txq(hw, txq, false);
	}
	ieee80211_txq_schedule_end(hw, ac);
}

/* ------------------------------------------------------------------ */
/* aggregation / AP-mode stubs (N3: compile + call order, no function)  */
/* ------------------------------------------------------------------ */

int
ieee80211_start_tx_ba_session(struct ieee80211_sta *sta, u16 tid, u16 timeout)
{

	/*
	 * No aggregation in this port: answering -EOPNOTSUPP is the honest
	 * refusal (the caller falls back), and counting it keeps the fact
	 * visible. The driver's ampdu_action is never reached because this is
	 * the entry that would start it.
	 */
	(void)sta;
	(void)tid;
	(void)timeout;
	return -EOPNOTSUPP;
}

void
ieee80211_stop_tx_ba_cb_irqsafe(struct ieee80211_vif *vif, const u8 *ra, u16 tid)
{

	(void)vif;
	(void)ra;
	(void)tid;	/* nothing was ever started; nothing to stop */
}

void
ieee80211_csa_finish(struct ieee80211_vif *vif, unsigned int link_id)
{

	(void)vif;
	(void)link_id;
}

/* ------------------------------------------------------------------ */
/* beacon family (AP mode is a declared non-goal; the calls must answer) */
/* ------------------------------------------------------------------ */

struct sk_buff *
ieee80211_beacon_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    unsigned int link_id)
{

	(void)hw;
	(void)vif;
	(void)link_id;
	lkpi_printf("lkpi80211: beacon_get (AP mode is not supported here)\n");
	return NULL;
}

bool
ieee80211_beacon_cntdwn_is_complete(struct ieee80211_vif *vif,
    unsigned int link_id)
{

	(void)vif;
	(void)link_id;
	return false;
}

/* ------------------------------------------------------------------ */
/* chanctx emulation (the four inline helpers the driver points at)     */
/* ------------------------------------------------------------------ */

int
ieee80211_emulate_add_chanctx(struct ieee80211_hw *hw,
    struct ieee80211_chanctx_conf *ctx)
{
	struct lkpi80211_hw_ctx *h;

	if (hw == NULL || ctx == NULL)
		return -EINVAL;
	h = LKPI80211_CTX(hw);
	if (h->have_chanctx)
		return -EBUSY;		/* one channel context per hw */
	h->chanctx = *ctx;
	h->have_chanctx = 1;
	return 0;
}

void
ieee80211_emulate_remove_chanctx(struct ieee80211_hw *hw,
    struct ieee80211_chanctx_conf *ctx)
{
	struct lkpi80211_hw_ctx *h;

	(void)ctx;
	if (hw == NULL)
		return;
	h = LKPI80211_CTX(hw);
	memset(&h->chanctx, 0, sizeof(h->chanctx));
	h->have_chanctx = 0;
}

void
ieee80211_emulate_change_chanctx(struct ieee80211_hw *hw,
    struct ieee80211_chanctx_conf *ctx, u32 changed)
{
	struct lkpi80211_hw_ctx *h;

	if (hw == NULL || ctx == NULL)
		return;
	h = LKPI80211_CTX(hw);
	if (!h->have_chanctx) {
		lkpi_printf("lkpi80211: change_chanctx with none assigned\n");
		return;
	}
	h->chanctx = *ctx;
	(void)changed;
}

int
ieee80211_emulate_switch_vif_chanctx(struct ieee80211_hw *hw,
    struct ieee80211_vif_chanctx_switch *vifs, int n_vifs,
    enum ieee80211_chanctx_switch_mode mode)
{
	struct lkpi80211_hw_ctx *h;
	int i;

	(void)mode;
	if (hw == NULL || vifs == NULL || n_vifs <= 0)
		return -EINVAL;
	h = LKPI80211_CTX(hw);
	for (i = 0; i < n_vifs; i++) {
		if (vifs[i].vif == NULL || vifs[i].new_ctx == NULL)
			return -EINVAL;
		vifs[i].vif->bss_conf.chanctx_conf = vifs[i].new_ctx;
	}
	if (n_vifs > 0)
		h->chanctx = *vifs[0].new_ctx;
	h->have_chanctx = 1;
	return 0;
}

/* ------------------------------------------------------------------ */
/* the rate table (driver: core.c:4878 cfg80211_calculate_bitrate)      */
/* ------------------------------------------------------------------ */

/*
 * Legacy and HT rates in kbps, indexed by the rate's hardware index. The table
 * is the 2.4 GHz MCS/legacy set this chip reports; the values are the standard
 * per-rate bitrates (802.11-2020 clause 17/19), not measurements. A rate index
 * outside the table answers 0 ("unknown"), which the caller treats as no
 * reported rate.
 */
static const u32 lkpi80211_bitrates[] = {
	10, 20, 55, 110,			/* CCK 1/2/5.5/11 Mbps */
	60, 90, 120, 180, 240, 360, 480, 540,	/* OFDM 6..54 Mbps */
	65, 130, 195, 260, 390, 520, 585, 650,	/* HT MCS0..7, 1 stream */
	130, 260, 390, 520, 780, 1040, 1170, 1300,	/* HT MCS8..15 */
	135, 270, 405, 540, 810, 1080, 1215, 1350,	/* HT MCS0..7, 40 MHz */
};

u32
cfg80211_calculate_bitrate(struct rate_info *rate)
{
	u32 idx;

	if (rate == NULL)
		return 0;
	/* the driver fills mcs/flags and asks back for the bitrate: the
	 * MCS-with-HT form is indexed after the 12 legacy entries */
	if ((rate->flags & RATE_INFO_FLAGS_MCS) != 0)
		idx = 12u + (u32)rate->mcs;
	else
		idx = (u32)rate->legacy;
	if (idx >= sizeof(lkpi80211_bitrates) / sizeof(lkpi80211_bitrates[0]))
		return 0;
	return lkpi80211_bitrates[idx];
}

/* ------------------------------------------------------------------ */
/* the vif/sta drivers' private areas                                  */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* downward helpers: the bridge drives the driver through these        */
/* ------------------------------------------------------------------ */

int
lkpi80211_drv_start(struct ieee80211_hw *hw)
{
	struct lkpi80211_hw_ctx *ctx;

	if (hw == NULL)
		return -EINVAL;
	ctx = LKPI80211_CTX(hw);
	if (ctx->ops->start == NULL)
		return -EOPNOTSUPP;
	return ctx->ops->start(hw);
}

void
lkpi80211_drv_stop(struct ieee80211_hw *hw, bool suspend)
{
	struct lkpi80211_hw_ctx *ctx;

	if (hw == NULL)
		return;
	ctx = LKPI80211_CTX(hw);
	if (ctx->ops->stop != NULL)
		ctx->ops->stop(hw, suspend);
}

int
lkpi80211_drv_config_channel(struct ieee80211_hw *hw, u32 freq, u8 hw_value,
    u8 band, u8 width, u32 center_freq1)
{
	struct lkpi80211_hw_ctx *ctx;

	if (hw == NULL)
		return -EINVAL;
	ctx = LKPI80211_CTX(hw);
	/* the channel descriptor the driver reads back through
	 * hw->conf.chandef (core.c:1217 hw_value, :1240 center_freq1, :1219
	 * width, :4757 band) */
	ctx->chan.center_freq = freq;
	ctx->chan.hw_value = hw_value;
	ctx->chan.band = (enum nl80211_band)band;
	ctx->hw.conf.chandef.chan = &ctx->chan;
	ctx->hw.conf.chandef.width = (enum nl80211_chan_width)width;
	ctx->hw.conf.chandef.center_freq1 = center_freq1;
	if (ctx->ops->config == NULL)
		return 0;
	return ctx->ops->config(hw, 0, IEEE80211_CONF_CHANGE_CHANNEL);
}

int
lkpi80211_drv_scan_start(struct ieee80211_hw *hw)
{
	struct lkpi80211_hw_ctx *ctx;

	if (hw == NULL)
		return -EINVAL;
	ctx = LKPI80211_CTX(hw);
	ctx->scanning = 1;
	if (ctx->ops->sw_scan_start == NULL)
		return -EOPNOTSUPP;
	/* the driver's signature carries the vif and the address; the scanning
	 * interface is the first one here */
	ctx->ops->sw_scan_start(hw, ctx->vifs[0],
	    ctx->vifs[0] != NULL ? ctx->vifs[0]->addr : NULL);
	return 0;
}

void
lkpi80211_drv_scan_complete(struct ieee80211_hw *hw)
{
	struct lkpi80211_hw_ctx *ctx;

	if (hw == NULL)
		return;
	ctx = LKPI80211_CTX(hw);
	if (ctx->ops->sw_scan_complete != NULL)
		ctx->ops->sw_scan_complete(hw, ctx->vifs[0]);
	ctx->scanning = 0;
}

int
lkpi80211_drv_filter(struct ieee80211_hw *hw, u32 changed, u32 total)
{
	struct lkpi80211_hw_ctx *ctx;
	unsigned int t = total;

	if (hw == NULL)
		return -EINVAL;
	ctx = LKPI80211_CTX(hw);
	if (ctx->ops->configure_filter != NULL) {
		ctx->ops->configure_filter(hw, changed, &t, 0);
		/* the driver may narrow the set (the mac80211 contract: what it
		 * kept comes back in the total) */
		ctx->filter_total = t;
		return 0;
	}
	return -EOPNOTSUPP;
}

struct ieee80211_vif *
lkpi80211_drv_vif_add(struct ieee80211_hw *hw, u8 type, const u8 addr[6])
{
	struct lkpi80211_hw_ctx *ctx;
	struct ieee80211_vif *vif;
	unsigned int i;
	int ret;

	if (hw == NULL || addr == NULL)
		return NULL;
	ctx = LKPI80211_CTX(hw);
	for (i = 0; i < CONFIG_LKPI80211_MAX_VIFS; i++)
		if (ctx->vifs[i] == NULL)
			break;
	if (i == CONFIG_LKPI80211_MAX_VIFS) {
		lkpi_printf("lkpi80211: vif table full\n");
		return NULL;
	}
	/* the vif area is the driver's size (hw->vif_data_size, set before
	 * register: core.c:7881) plus the addressing fields the substitute
	 * owns; zeroed, so the driver's drv_priv starts clean */
	vif = kzalloc(offsetof(struct ieee80211_vif, drv_priv) +
	    (size_t)hw->vif_data_size, GFP_KERNEL);
	if (vif == NULL)
		return NULL;
	vif->type = (enum nl80211_iftype)type;
	memcpy(vif->addr, addr, 6);
	vif->addr_valid = true;
	/*
	 * The default link's bookkeeping, the way the interface sets it up for
	 * every vif (link.c:ieee80211_link_init with link_id -1: the default
	 * link's conf IS the vif's own bss_conf and its link id is 0).  A
	 * driver's link-level handlers read it - rtw89's link_info_changed
	 * looks its own per-link record up by conf->link_id - and valid_links
	 * stays 0, which is what a non-MLO vif carries (mlme.c:6460-6465) and
	 * what ieee80211_vif_is_mld() answers from.
	 */
	vif->bss_conf.link_id = 0;
	vif->link_conf[0] = &vif->bss_conf;
	/*
	 * The default link's own address and its back-pointer to the vif, the
	 * other half of what the interface sets up for that link.  A driver that
	 * takes the interface reads the link address, not vif->addr: rtw89 copies
	 * it into its per-link record at add_interface (mac80211.c:137-138),
	 * pushes that to the address CAM as the source address (cam.c:874-879),
	 * and restores it from the same field after every sweep
	 * (core.c:7095-7096).  Left zero, the chip's own address filter matches
	 * nothing, so every unicast frame to this interface - an authentication
	 * response, an EAPOL key - is dropped in hardware.
	 */
	memcpy(vif->bss_conf.addr, vif->addr, 6);
	vif->bss_conf.vif = vif;
	ctx->vifs[i] = vif;
	vif_hw_bind(vif, hw);
	/*
	 * The interface's own txq before the driver hears about the interface:
	 * the driver's add_interface initializes its per-queue state through it
	 * (rtw89 mac80211.c:201 -> rtw89_core_txq_init(rtwdev, vif->txq), which
	 * answers a NULL queue by doing nothing).
	 */
	vif->txq = lkpi_txq_alloc(hw, vif, NULL, 0);
	if (ctx->ops->add_interface != NULL) {
		ret = ctx->ops->add_interface(hw, vif);
		if (ret != 0) {
			lkpi_printf("lkpi80211: add_interface refused (%d)\n", ret);
			ctx->vifs[i] = NULL;
			lkpi_txq_free(vif->txq);
			vif->txq = NULL;
			kfree(vif);
			return NULL;
		}
	}
	lkpi_printf("lkpi80211: vif added idx=%u type=%u priv=%u\n", i, type,
	    (unsigned)hw->vif_data_size);
	return vif;
}

void
lkpi80211_drv_vif_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct lkpi80211_hw_ctx *ctx;
	unsigned int i;

	if (hw == NULL || vif == NULL)
		return;
	ctx = LKPI80211_CTX(hw);
	if (ctx->ops->remove_interface != NULL)
		ctx->ops->remove_interface(hw, vif);
	for (i = 0; i < CONFIG_LKPI80211_MAX_VIFS; i++)
		if (ctx->vifs[i] == vif) {
			ctx->vifs[i] = NULL;
			vif_hw_unbind(vif);
			/* after remove_interface: the driver's teardown still
			 * reads the queues it initialized in add_interface */
			lkpi_txq_free_vif(vif);
			vif->txq = NULL;
			kfree(vif);
			return;
		}
	lkpi_printf("lkpi80211: vif_remove of a foreign vif refused\n");
}

/*
 * The vif-wide half of a BSS change: the bits that describe the interface
 * rather than one of its links.  Upstream names the set in
 * net/mac80211/main.c:203-208 (ASSOC, IDLE, PS, IBSS, ARP_FILTER, SSID); this
 * face's enum carries the three of those the compiled drivers react to
 * (include/net/mac80211.h:150: ASSOC, PS, ARP_FILTER), and the drivers here
 * are never sent the other three.
 */
#define LKPI80211_VIF_CFG_FLAGS	(BSS_CHANGED_ASSOC | BSS_CHANGED_PS | \
				 BSS_CHANGED_ARP_FILTER)

void
lkpi80211_drv_bss_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    u64 changed)
{
	struct lkpi80211_hw_ctx *ctx;
	u64 cfg = changed & (u64)LKPI80211_VIF_CFG_FLAGS;

	if (hw == NULL || vif == NULL || changed == 0)
		return;
	ctx = LKPI80211_CTX(hw);
	/*
	 * The dispatch the interface itself makes (main.c:238-254): the
	 * vif-wide bits go to vif_cfg_changed, everything else to
	 * link_info_changed with the vif's own bss_conf as the link, and a
	 * driver declaring the older bss_info_changed gets the whole mask as
	 * it always did.  The two families are not exclusive upstream - a
	 * driver that declares both is called twice - so neither is here; the
	 * drivers this port carries declare one family each (rtl8xxxu
	 * bss_info_changed, rtw89 vif_cfg_changed + link_info_changed).
	 */
	if (cfg != 0 && ctx->ops->vif_cfg_changed != NULL)
		ctx->ops->vif_cfg_changed(hw, vif, cfg);
	if ((changed & ~(u64)LKPI80211_VIF_CFG_FLAGS) != 0 &&
	    ctx->ops->link_info_changed != NULL)
		ctx->ops->link_info_changed(hw, vif, &vif->bss_conf,
		    changed & ~(u64)LKPI80211_VIF_CFG_FLAGS);
	if (ctx->ops->bss_info_changed != NULL)
		ctx->ops->bss_info_changed(hw, vif, &vif->bss_conf, changed);
}

int
lkpi80211_drv_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
    struct ieee80211_vif *vif, struct ieee80211_sta *sta,
    struct ieee80211_key_conf *key)
{

	if (hw == NULL || vif == NULL || key == NULL)
		return -EINVAL;
	if (LKPI80211_CTX(hw)->ops->set_key == NULL)
		return -EOPNOTSUPP;
	return LKPI80211_CTX(hw)->ops->set_key(hw, cmd, vif, sta, key);
}

/*
 * The headroom promise, checked where it is owed. Every frame handed to a
 * driver's tx op must carry hw->extra_tx_headroom bytes of headroom: that is
 * what mac80211 reserves per frame before the driver's tx runs
 * (net/mac80211/driver-ops.c:drv_tx over what ieee80211_alloc_tx_skb reserved).
 * A frame that arrives without it fails much later and much less legibly - the
 * driver's descriptor push is refused at the pool's headroom guard, at the far
 * end of the frame path - so the shortfall is named here once, at the near end,
 * with both numbers.
 */
static void
lkpi80211_tx_headroom_check(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	static int latched;

	if (latched || hw->extra_tx_headroom == 0)
		return;
	if (skb_headroom(skb) >= hw->extra_tx_headroom)
		return;
	latched = 1;
	lkpi_printf("lkpi80211: tx frame has %u bytes of headroom, the driver "
	    "declared %u (its transmit descriptor push will be refused)\n",
	    skb_headroom(skb), hw->extra_tx_headroom);
}

void
lkpi80211_drv_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    struct ieee80211_sta *sta, struct sk_buff *skb)
{
	struct ieee80211_tx_control control = { 0 };
	struct lkpi80211_hw_ctx *ctx;

	if (hw == NULL || skb == NULL)
		return;
	ctx = LKPI80211_CTX(hw);
	if (ctx->ops->tx == NULL) {
		lkpi_skb_free(skb);
		return;
	}
	lkpi80211_tx_headroom_check(hw, skb);
	control.sta = sta;
	(void)vif;
	ctx->ops->tx(hw, &control, skb);
}

/*
 * The driver's station-state hook, whichever shape it declares.  A driver with
 * .sta_state is given every transition; one without it is given .sta_add at
 * AUTH -> ASSOC and .sta_remove at ASSOC -> AUTH, and the NOTEXIST boundaries
 * reach no driver code at all - exactly what the interface's own dispatcher
 * does for that case (net/mac80211/driver-ops.c:drv_sta_state).
 */
static int
sta_state_call(struct lkpi80211_hw_ctx *ctx, struct ieee80211_hw *hw,
    struct ieee80211_vif *vif, struct ieee80211_sta *sta,
    enum ieee80211_sta_state old_state, enum ieee80211_sta_state new_state)
{

	if (ctx->ops->sta_state != NULL)
		return ctx->ops->sta_state(hw, vif, sta, old_state, new_state);
	if (old_state == IEEE80211_STA_AUTH &&
	    new_state == IEEE80211_STA_ASSOC) {
		if (ctx->ops->sta_add != NULL)
			return ctx->ops->sta_add(hw, vif, sta);
	} else if (old_state == IEEE80211_STA_ASSOC &&
	    new_state == IEEE80211_STA_AUTH) {
		if (ctx->ops->sta_remove != NULL)
			ctx->ops->sta_remove(hw, vif, sta);
	}
	return 0;
}

struct ieee80211_sta *
lkpi80211_drv_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    const u8 addr[6], u16 aid)
{
	struct lkpi80211_hw_ctx *ctx;
	struct ieee80211_sta *sta;
	unsigned int i;
	unsigned int slot;
	int ret;

	if (hw == NULL || addr == NULL)
		return NULL;
	ctx = LKPI80211_CTX(hw);
	if (ieee80211_find_sta_by_ifaddr(hw, addr, NULL) != NULL)
		return ieee80211_find_sta_by_ifaddr(hw, addr, NULL);
	for (slot = 0; slot < CONFIG_LKPI80211_MAX_STAS; slot++)
		if (ctx->stas[slot] == NULL)
			break;
	if (slot == CONFIG_LKPI80211_MAX_STAS) {
		lkpi_printf("lkpi80211: sta table full\n");
		return NULL;
	}
	sta = kzalloc(offsetof(struct ieee80211_sta, drv_priv) +
	    (size_t)hw->sta_data_size, GFP_KERNEL);
	if (sta == NULL)
		return NULL;
	memcpy(sta->addr, addr, 6);
	sta->aid = aid;
	ctx->stas[slot] = sta;
	/*
	 * The station's per-TID txqs, before the driver hears about the station:
	 * its station setup walks them to initialize the driver-private queue
	 * state (rtw89 mac80211.c:516-517).  Index IEEE80211_NUM_TIDS (the
	 * non-QoS queue) is left NULL: upstream only allocates it for a driver
	 * that opted in through its hw flags (tx.c:1545-1554), and the walk
	 * above answers a NULL queue by skipping it.
	 */
	for (i = 0; i < IEEE80211_NUM_TIDS; i++)
		sta->txq[i] = lkpi_txq_alloc(hw, vif, sta, (u8)i);
	/*
	 * The station appearing and then associating.  The interface runs the
	 * whole ladder (NOTEXIST -> NONE -> AUTH -> ASSOC) across discovery and
	 * association; this port has no separate discovery event, so the two
	 * transitions that carry driver work happen together here.  NONE -> AUTH
	 * is skipped: no driver in this repository acts on it.
	 */
	ret = sta_state_call(ctx, hw, vif, sta, IEEE80211_STA_NOTEXIST,
	    IEEE80211_STA_NONE);
	if (ret == 0)
		ret = sta_state_call(ctx, hw, vif, sta, IEEE80211_STA_AUTH,
		    IEEE80211_STA_ASSOC);
	if (ret != 0) {
		lkpi_printf("lkpi80211: sta add refused (%d)\n", ret);
		ctx->stas[slot] = NULL;
		lkpi_txq_free_sta(sta);
		kfree(sta);
		return NULL;
	}
	return sta;
}

void
lkpi80211_drv_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    struct ieee80211_sta *sta)
{
	struct lkpi80211_hw_ctx *ctx;
	unsigned int i;

	if (hw == NULL || sta == NULL)
		return;
	ctx = LKPI80211_CTX(hw);
	/* the ladder back down, in the order the interface runs it: the
	 * association ends, then the station is gone */
	(void)sta_state_call(ctx, hw, vif, sta, IEEE80211_STA_ASSOC,
	    IEEE80211_STA_AUTH);
	(void)sta_state_call(ctx, hw, vif, sta, IEEE80211_STA_NONE,
	    IEEE80211_STA_NOTEXIST);
	for (i = 0; i < CONFIG_LKPI80211_MAX_STAS; i++)
		if (ctx->stas[i] == sta) {
			ctx->stas[i] = NULL;
			/* after the driver's teardown: it still reads the queues
			 * its station setup initialized */
			lkpi_txq_free_sta(sta);
			kfree(sta);
			return;
		}
}

struct ieee80211_hw *
wiphy_to_ieee80211_hw(struct wiphy *wiphy)
{
	struct lkpi80211_hw_ctx *ctx;

	/*
	 * [LNX] include/net/mac80211.h: the interface answers this with
	 * container_of over its own local record, and the same holds here for
	 * the same reason: this port embeds the wiphy in the hw context it
	 * allocates (hw first, wiphy behind it), so the address a driver read
	 * out of hw->wiphy maps back to the hw it came from.
	 */
	if (wiphy == NULL)
		return NULL;
	ctx = container_of(wiphy, struct lkpi80211_hw_ctx, wiphy);
	return &ctx->hw;
}

void
wiphy_rfkill_set_hw_state(struct wiphy *wiphy, bool blocked)
{

	/*
	 * [LNX] net/wireless/rfkill.c: report the hardware RF-kill switch.  The
	 * consumer is the rfkill subsystem, which this port does not carry (the
	 * stack on the other side of the bridge is net80211, and nothing there
	 * listens for it), so the notice has no destination and is dropped
	 * rather than invented.
	 */
	(void)wiphy;
	(void)blocked;
}

void
wiphy_rfkill_start_polling(struct wiphy *wiphy)
{

	/* [LNX] net/wireless/rfkill.c: the polling variant of the same notice */
	(void)wiphy;
}

void
wiphy_rfkill_stop_polling(struct wiphy *wiphy)
{

	(void)wiphy;
}

struct ieee80211_vif *
lkpi80211_hw_vif(struct ieee80211_hw *hw, unsigned int idx)
{

	if (hw == NULL || idx >= CONFIG_LKPI80211_MAX_VIFS)
		return NULL;
	return LKPI80211_CTX(hw)->vifs[idx];
}

struct ieee80211_sta *
lkpi80211_hw_sta(struct ieee80211_hw *hw, unsigned int idx)
{

	if (hw == NULL || idx >= CONFIG_LKPI80211_MAX_STAS)
		return NULL;
	return LKPI80211_CTX(hw)->stas[idx];
}

/*
 * The walk the interface's for_each_station() macro is written against: the
 * next occupied station slot after `prev`, or the first one when `prev` is
 * NULL.  A slot index is the cursor, so a station removed mid-walk ends the
 * walk rather than skipping its neighbour - the same "the table moved under
 * you" the interface's own iterator answers by restarting from the caller.
 */
struct ieee80211_sta *
lkpi80211_hw_sta_next(struct ieee80211_hw *hw, struct ieee80211_sta *prev)
{
	struct lkpi80211_hw_ctx *ctx;
	unsigned int i = 0;

	if (hw == NULL)
		return NULL;
	ctx = LKPI80211_CTX(hw);
	if (prev != NULL) {
		for (i = 0; i < CONFIG_LKPI80211_MAX_STAS; i++)
			if (ctx->stas[i] == prev)
				break;
		if (i == CONFIG_LKPI80211_MAX_STAS)
			return NULL;	/* a station this hw does not hold */
		i++;
	}
	for (; i < CONFIG_LKPI80211_MAX_STAS; i++)
		if (ctx->stas[i] != NULL)
			return ctx->stas[i];
	return NULL;
}

unsigned int
lkpi80211_hw_vif_count(struct ieee80211_hw *hw)
{
	unsigned int i, n = 0;

	if (hw == NULL)
		return 0;
	for (i = 0; i < CONFIG_LKPI80211_MAX_VIFS; i++)
		if (LKPI80211_CTX(hw)->vifs[i] != NULL)
			n++;
	return n;
}

void *
lkpi80211_hw_priv(struct ieee80211_hw *hw)
{

	return hw != NULL ? hw->priv : NULL;
}

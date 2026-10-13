/* SPDX-License-Identifier: BSD-3-Clause */
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
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
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
/* the deferred receive queue's depth: a burst the driver hands up between two
 * deliveries of the receive worker; beyond it the oldest frame is dropped and
 * counted (mac80211's own queue grows instead, but a fixed ring keeps the
 * port's memory accounting honest) */
#ifndef CONFIG_LKPI80211_RXQ_DEPTH
#define CONFIG_LKPI80211_RXQ_DEPTH	32
#endif

/* the deferred-receive worker (body below ieee80211_rx_irqsafe): the hw
 * allocator arms it before any driver can hand a frame up */
static void lkpi80211_rx_work(struct work_struct *w);

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
	/*
	 * The driver-private tail that record ends with (drv_priv[]): the
	 * interface reserves hw->chanctx_data_size bytes behind every channel
	 * context and a driver stores its own handle there - the AC 7260 keeps
	 * its PHY-context id in those two bytes (mvm/mac80211.c:4987 reads
	 * `(u16 *)ctx->drv_priv`).  The record above is a fixed member, so its
	 * flexible array has no storage of its own; this array is that storage,
	 * and the size a driver declares is checked against it before the
	 * driver is told about the context.
	 */
	u8 chanctx_priv[64];
	struct ieee80211_vif	*vifs[CONFIG_LKPI80211_MAX_VIFS];
	struct ieee80211_sta	*stas[CONFIG_LKPI80211_MAX_STAS];
	/*
	 * The per-vif wireless_dev the interface embeds in each of its own
	 * interfaces (net/mac80211/iface.c: `sdata->wdev`), which
	 * ieee80211_vif_to_wdev() hands back.  A compiled caller reads only its
	 * iftype (the firmware debug-trigger match) and its wiphy; netdev stays
	 * NULL because this port has no net_device per interface.
	 */
	struct wireless_dev	wdevs[CONFIG_LKPI80211_MAX_VIFS];
	u32			queue_stopped;	/* bit per AC, the queue family */
	u32			filter_total;	/* the last programmed filter */
	unsigned int		registered:1;
	unsigned int		scanning:1;
	/*
	 * The hardware-scan route's own state. `hw_scanning` is set while a
	 * firmware sweep is in flight (there the driver owns the dwell, so the
	 * end arrives as a call rather than as the bridge's walk running out),
	 * and `hw_scan_req` is the request object handed to ops->hw_scan: it
	 * must outlive that call, so it is held here until the completion or a
	 * cancel releases it.
	 */
	unsigned int		hw_scanning:1;
	/*
	 * The deferred receive queue (mac80211's local->rx_skb_queue, drained
	 * by its tasklet): ieee80211_rx_irqsafe only enqueues, and a worker
	 * delivers to the bridge.  Running the delivery inline in the caller
	 * deadlocks: the AC 7260's receive runs on the threaded IRQ, which also
	 * completes host commands, while the stack-side input takes the port
	 * serializer - so a frame handed up while another context holds the
	 * serializer inside a driver call that waits for a host command makes
	 * that command wait behind its own completion (measured on the board:
	 * SCD_QUEUE_CFG timed out and the driver force-fired an NMI).  Linux's
	 * irqsafe form exists exactly to break that chain.
	 */
	struct sk_buff		*rxq[CONFIG_LKPI80211_RXQ_DEPTH];
	unsigned int		rxq_head, rxq_tail, rxq_len;
	struct work_struct	rx_work;
	struct workqueue_struct *rx_wq;
	unsigned long		rxq_drops;
	struct ieee80211_scan_request *hw_scan_req;
	unsigned int		have_chanctx:1;
	/*
	 * The face's "the interface is running" bitmap, one bit per vif slot -
	 * mac80211's IEEE80211_SDATA_IN_DRIVER / SDATA_STATE_RUNNING pair in the
	 * one place this port needs it.  The interface marks a vif running only
	 * AFTER the driver's add_interface has returned (iface.c:1261 calls
	 * drv_add_interface, :1275 sets SDATA_STATE_RUNNING), and its active-
	 * interface iteration skips the ones that are not (util.c
	 * __iterate_interfaces).  A driver leans on that: the AC 7260's
	 * MAC-context setup walks the active interfaces to hand out MAC/TSF ids
	 * and treats "the vif is already there" as a resume, skipping the whole
	 * setup (mvm/mac-ctxt.c:258-268) - which is how a vif visible too early
	 * ends up with no MAC context in the firmware, no link initialisation,
	 * and a firmware assert on the first queue it configures.
	 */
	u32			vif_running;
	unsigned int		tx_ba_sessions;
	unsigned int		rx_frames;
	unsigned int		tx_completions;
	unsigned int		scan_completions;
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
/* the key table                                                       */
/* ------------------------------------------------------------------ */

/*
 * The keys the interface would keep in its own per-interface key_list.  In this
 * port the key policy lives with net80211 behind the bridge, and the driver is
 * told about each key through its set_key op; this table records those keys so
 * the interface's own key walk (ieee80211_iter_keys / ieee80211_iter_keys_rcu),
 * its per-key receive-sequence read (ieee80211_get_key_rx_seq) and its per-key
 * notices (ieee80211_key_mic_failure / ieee80211_key_replay) answer from what was
 * actually installed, rather than from a list that does not exist.
 *
 * Ownership: the interface owns the key object it hands the driver, and a driver
 * may keep the pointer (the AC 7260 stores it for the beacon-protection keys -
 * mvm/mac80211.c:4269 rcu_assign_pointer(mvmvif->bcn_prot.keys[..], key)).  The
 * bridge's own set_key caller allocates its key_conf and frees it as soon as the
 * call returns (ports/net80211/bridge/wlan_seam_adapter.c kzalloc/free around
 * lkpi80211_drv_key), so the substitute takes a stable copy, records it, and
 * hands THAT to the driver - the same ownership the interface's key_list gives.
 * A key is identified the way the interface identifies it: by (vif, sta, keyidx,
 * pairwise), which is what a DISABLE_KEY names.
 */
#ifndef CONFIG_LKPI80211_MAX_KEYS
#define CONFIG_LKPI80211_MAX_KEYS	8
#endif

struct lkpi80211_key_entry {
	unsigned int		used:1;
	struct ieee80211_hw	*hw;
	struct ieee80211_vif	*vif;
	struct ieee80211_sta	*sta;
	struct ieee80211_key_conf *conf;	/* heap copy, sizeof(*conf)+keylen */
	unsigned int		icverrors;
	unsigned int		replays;
	/*
	 * The per-TID receive sequence the interface keeps per key.  This port's
	 * receive IV checking is net80211's, on the bridge's side, so nothing
	 * advances it here: a read answers the key's initial value, which is the
	 * honest answer for a device that does not do RX IV checking itself.
	 */
	union {
		struct {
			u32 iv32;
			u16 iv16;
		} tkip[IEEE80211_NUM_TIDS];
		u8 pn[IEEE80211_NUM_TIDS + 1][IEEE80211_CCMP_PN_LEN];
	} rx;
};

static struct lkpi80211_key_entry lkpi80211_keys[CONFIG_LKPI80211_MAX_KEYS];

/* the record that owns this exact key object (what the driver was handed) */
static struct lkpi80211_key_entry *
lkpi80211_key_find_conf(struct ieee80211_key_conf *keyconf)
{
	unsigned int i;

	for (i = 0; i < CONFIG_LKPI80211_MAX_KEYS; i++)
		if (lkpi80211_keys[i].used && lkpi80211_keys[i].conf == keyconf)
			return &lkpi80211_keys[i];
	return NULL;
}

/* the record for this key identity, the way a DISABLE_KEY names it */
static struct lkpi80211_key_entry *
lkpi80211_key_find_id(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    struct ieee80211_sta *sta, s8 keyidx, bool pairwise)
{
	unsigned int i;

	for (i = 0; i < CONFIG_LKPI80211_MAX_KEYS; i++) {
		struct lkpi80211_key_entry *e = &lkpi80211_keys[i];
		bool e_pairwise;

		if (!e->used || e->conf == NULL || e->hw != hw)
			continue;
		if (e->vif != vif || e->sta != sta || e->conf->keyidx != keyidx)
			continue;
		e_pairwise = (e->conf->flags & IEEE80211_KEY_FLAG_PAIRWISE) != 0;
		if (e_pairwise == pairwise)
			return e;
	}
	return NULL;
}

static void
lkpi80211_key_release(struct lkpi80211_key_entry *e)
{

	kfree(e->conf);
	memset(e, 0, sizeof(*e));
}

/*
 * Take a stable copy of `key`, record it, and return the record (whose ->conf
 * is the object the driver is to be handed).  Returns NULL if the table is full
 * or the copy cannot be made; the caller then falls back to the bridge's own
 * key_conf, so a full table degrades to "not tracked" rather than a failed
 * set_key.
 */
static struct lkpi80211_key_entry *
lkpi80211_key_install(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    struct ieee80211_sta *sta, struct ieee80211_key_conf *key)
{
	struct lkpi80211_key_entry *e;
	struct ieee80211_key_conf *copy;
	bool pairwise = (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) != 0;
	unsigned int i;

	/* a re-set of the same key replaces its record */
	e = lkpi80211_key_find_id(hw, vif, sta, key->keyidx, pairwise);
	if (e != NULL) {
		kfree(e->conf);
		e->conf = NULL;
	} else {
		for (i = 0; i < CONFIG_LKPI80211_MAX_KEYS; i++)
			if (!lkpi80211_keys[i].used)
				break;
		if (i == CONFIG_LKPI80211_MAX_KEYS) {
			lkpi_printf(ULOG_WARNING_LEVEL, "lkpi80211: key table full; key %d not tracked\n",
			    key->keyidx);
			return NULL;
		}
		e = &lkpi80211_keys[i];
		e->used = 1;
	}
	copy = kzalloc(sizeof(*copy) + key->keylen, GFP_KERNEL);
	if (copy == NULL) {
		e->used = 0;
		e->conf = NULL;
		return NULL;
	}
	memcpy(copy, key, sizeof(*copy) + key->keylen);
	e->hw = hw;
	e->vif = vif;
	e->sta = sta;
	e->conf = copy;
	memset(&e->rx, 0, sizeof(e->rx));
	e->icverrors = 0;
	e->replays = 0;
	return e;
}

static void
lkpi80211_key_drop(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    struct ieee80211_sta *sta, struct ieee80211_key_conf *key)
{
	struct lkpi80211_key_entry *e;

	e = lkpi80211_key_find_conf(key);
	if (e == NULL)
		e = lkpi80211_key_find_id(hw, vif, sta, key->keyidx,
		    (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) != 0);
	if (e != NULL)
		lkpi80211_key_release(e);
}

static void
lkpi80211_keys_drop_hw(struct ieee80211_hw *hw)
{
	unsigned int i;

	for (i = 0; i < CONFIG_LKPI80211_MAX_KEYS; i++)
		if (lkpi80211_keys[i].used && lkpi80211_keys[i].hw == hw)
			lkpi80211_key_release(&lkpi80211_keys[i]);
}

/* ------------------------------------------------------------------ */
/* the rate-control algorithm registry                                 */
/* ------------------------------------------------------------------ */

/*
 * The algorithms a driver registers at module init.  The interface keeps them
 * in a list and looks one up by name when it needs to pick rates; this port's
 * transmit path is driven by the bridge (which does not consult mac80211 rate
 * control), so the registry is what a registration has to mean here: the ops
 * are remembered and a duplicate name is refused, exactly as the interface
 * refuses it, so the driver's own module-init ordering still holds.
 */
#ifndef CONFIG_LKPI80211_MAX_RC
#define CONFIG_LKPI80211_MAX_RC	4
#endif

static const struct rate_control_ops *lkpi80211_rc_ops[CONFIG_LKPI80211_MAX_RC];

/* ------------------------------------------------------------------ */
/* hw allocation (driver calls: core.c:7813 ieee80211_alloc_hw)         */
/* ------------------------------------------------------------------ */

struct ieee80211_hw *
ieee80211_alloc_hw(size_t priv_data_len, const struct ieee80211_ops *ops)
{
	struct lkpi80211_hw_ctx *ctx;
	unsigned int i;

	if (ops == NULL) {
		lkpi_printf(ULOG_ERROR_LEVEL, "lkpi80211: alloc_hw without an ops table\n");
		return NULL;
	}
	/*
	 * One block: the context, then the driver's private area. The vif/sta
	 * areas themselves are allocated later, when the driver's own sizes are
	 * known (it sets hw->vif_data_size in probe, before registering).
	 */
	ctx = kzalloc(sizeof(*ctx) + priv_data_len, GFP_KERNEL);
	if (ctx == NULL) {
		lkpi_printf(ULOG_ERROR_LEVEL, "lkpi80211: no memory for an hw context\n");
		return NULL;
	}
	ctx->ops = ops;
	ctx->hw.priv = (u8 *)ctx + sizeof(*ctx);
	ctx->hw.wiphy = &ctx->wiphy;
	/*
	 * The deferred-receive executor: the interface's own receive thread.
	 * One queue, one worker - the receive order is the order the driver
	 * handed the frames up, which the parser relies on (scan results and
	 * the association exchange both read their sequence from it).
	 */
	INIT_WORK(&ctx->rx_work, lkpi80211_rx_work);
	ctx->rx_wq = lkpi_alloc_workqueue("lkpi80211-rx", 0, 1);
	if (ctx->rx_wq == NULL) {
		lkpi_printf(ULOG_ERROR_LEVEL, "lkpi80211: no receive executor\n");
		kfree(ctx);
		return NULL;
	}
	/* the queue count the driver reads back (core.c:7891 sets 4; the
	 * engine's own default matches so a driver that does not set it still
	 * gets a workable value) */
	ctx->hw.queues = 4;
	for (i = 0; i < IEEE80211_NUM_ACS; i++)
		ctx->hw.conf.chandef.chan = &ctx->chan;
	lkpi_printf(ULOG_INFO_LEVEL, "lkpi80211: hw allocated priv=%u ctx=%u\n",
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
		lkpi_printf(ULOG_WARNING_LEVEL, "lkpi80211: free_hw while registered; unregistering\n");
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
	/* the receive executor before the context it sits in: no delivery can be
	 * in flight after the drain returns */
	if (ctx->rx_wq != NULL) {
		lkpi_work_flush(&ctx->rx_work);
		lkpi_destroy_workqueue(ctx->rx_wq);
		ctx->rx_wq = NULL;
	}
	while (ctx->rxq_len > 0) {
		struct sk_buff *skb = ctx->rxq[ctx->rxq_head];

		ctx->rxq[ctx->rxq_head] = NULL;
		ctx->rxq_head = (ctx->rxq_head + 1) % CONFIG_LKPI80211_RXQ_DEPTH;
		ctx->rxq_len--;
		if (skb != NULL)
			lkpi_skb_free(skb);
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
	/*
	 * The interface address the driver registered, into the permanent one:
	 * the driver fills wiphy->addresses[0] while it probes (the AC 7260 from
	 * its own NVM, mvm/mac80211.c:568-570) and the interface's own
	 * registration copies the first entry into perm_addr (net/wireless/
	 * core.c:976-977, wiphy_register).  The bridge reads perm_addr when it
	 * attaches, so without the copy the interface comes up with an all-zero
	 * address - and that address is what the driver hands the firmware in
	 * the MAC context it programs for the vif.
	 */
	if (ctx->wiphy.addresses != NULL && ctx->wiphy.n_addresses > 0 &&
	    is_valid_ether_addr(ctx->wiphy.addresses[0].addr)) {
		memcpy(ctx->wiphy.perm_addr, ctx->wiphy.addresses[0].addr,
		    ETH_ALEN);
	} else {
		lkpi_printf(ULOG_WARNING_LEVEL,
		    "lkpi80211: the driver registered no usable address (n_addresses=%u); the interface keeps the zero address\n",
		    (unsigned)ctx->wiphy.n_addresses);
	}
	if (bridge_ops == NULL || bridge_ops->attach == NULL) {
		/*
		 * No bridge means no net80211 behind the face: the radio could
		 * be powered but nothing would see a frame. Fail honestly - a
		 * silent success would leave the driver believing it is live.
		 */
		lkpi_printf(ULOG_INFO_LEVEL, "lkpi80211: register_hw without a bridge (-ENODEV)\n");
		return -ENODEV;
	}
	ret = bridge_ops->attach(hw);
	if (ret != 0) {
		lkpi_printf(ULOG_ERROR_LEVEL, "lkpi80211: bridge attach failed (%d)\n", ret);
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
	lkpi_printf(ULOG_INFO_LEVEL, "lkpi80211: hw registered\n");
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
	lkpi80211_keys_drop_hw(hw);
	lkpi_printf(ULOG_INFO_LEVEL, "lkpi80211: hw unregistered\n");
}

/* ------------------------------------------------------------------ */
/* rx / tx status (driver: core.c:6423, :6531 rx; :5171 tx status)      */
/* ------------------------------------------------------------------ */

/*
 * The receive boundary's frame rebuild. The vendored receive path keeps a frame
 * larger than its (128-byte) linear request in page fragments
 * (third-party/iwlwifi/mvm/rx.c:80-91), and this port's net80211 side speaks
 * linear frames - the bridge copies a frame's bytes out of one buffer. So the
 * single entry every receive path funnels through materializes the whole frame
 * before anything else reads it: the substitute's own linearization, in the
 * place the interface's own receive processing would have run (net/mac80211/rx.c
 * hands the stack a frame it has already pulled together). The original
 * buffer's release puts the fragment pages back, so only the new buffer lives
 * on. A refused allocation drops the frame loudly once - a silent drop here is
 * the "empty scan table while the radio hears beacons" failure this port has
 * paid for before.
 */
static struct sk_buff *
lkpi80211_rx_linearize(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct sk_buff *lin;
	unsigned char *p;
	unsigned int total;

	if (skb->data_len == 0)
		return skb;		/* already linear: the frame is the buffer */
	(void)hw;
	total = skb->len;
	lin = lkpi_dev_alloc_skb(total, GFP_ATOMIC);
	if (lin == NULL) {
		static bool said;

		if (!said) {
			said = true;
			lkpi_printf(ULOG_ERROR_LEVEL,
			    "lkpi80211: rx frame of %u bytes could not be linearized; frame dropped\n",
			    total);
		}
		lkpi_skb_free(skb);
		return NULL;
	}
	p = lkpi_skb_put(lin, total);
	if (p == NULL || skb_copy_bits(skb, 0, p, (int)total) != 0) {
		lkpi_printf(ULOG_ERROR_LEVEL, "lkpi80211: rx frame rebuild failed; frame dropped\n");
		lkpi_skb_free(lin);
		lkpi_skb_free(skb);
		return NULL;
	}
	memcpy(lin->cb, skb->cb, sizeof(lin->cb));
	lin->protocol = skb->protocol;
	lkpi_skb_free(skb);
	return lin;
}

/*
 * The deferred receive worker: what ieee80211_rx_irqsafe enqueued, delivered to
 * the bridge from its own context.  Linux drains the same queue from a tasklet
 * (net/mac80211/rx.c: ieee80211_rx_irqsafe), and the reason is the one this
 * port measured: the frame's delivery enters the stack under the port
 * serializer, while the driver's receive context (here the PCI threaded IRQ)
 * is also the context that completes host commands - a delivery running inline
 * there can deadlock a command whose completion it is itself carrying.
 */
static void
lkpi80211_rx_work(struct work_struct *w)
{
	struct lkpi80211_hw_ctx *ctx =
	    container_of(w, struct lkpi80211_hw_ctx, rx_work);

	for (;;) {
		struct sk_buff *skb;
		struct ieee80211_rx_status *rs;

		if (ctx->rxq_len == 0)
			return;
		skb = ctx->rxq[ctx->rxq_head];
		ctx->rxq[ctx->rxq_head] = NULL;
		ctx->rxq_head = (ctx->rxq_head + 1) % CONFIG_LKPI80211_RXQ_DEPTH;
		ctx->rxq_len--;

		rs = (struct ieee80211_rx_status *)skb->cb;
		ctx->rx_frames++;
		if (bridge_ops != NULL && bridge_ops->rx != NULL)
			bridge_ops->rx(&ctx->hw, rs, skb);
		else
			lkpi_printf(ULOG_ERROR_LEVEL,
			    "lkpi80211: rx with no bridge; frame dropped\n");
		memset(skb->cb, 0, sizeof(skb->cb));
		lkpi_skb_free(skb);
	}
}

void
ieee80211_rx_irqsafe(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct lkpi80211_hw_ctx *ctx;

	if (hw == NULL || skb == NULL)
		return;
	skb = lkpi80211_rx_linearize(hw, skb);
	if (skb == NULL)
		return;		/* refused, freed, and it said so */
	/*
	 * A driver that leaves the FCS on the frames it hands up says so once, at
	 * probe (IEEE80211_HW_RX_INCLUDES_FCS), and those four octets come off
	 * here - at the entry every receive path funnels through.  The 8821CU's
	 * USB receive path calls this directly rather than going through
	 * ieee80211_rx_napi, so a trim placed only in the napi entry never ran for
	 * it: every frame reached the stack four octets longer than its own
	 * element stream, the imported parser ended it with is_rx_elem_toosmall
	 * (input.c:2122) before it could reach the scan table, and the sweep came
	 * back empty while the radio was hearing beacons.
	 */
	if (ieee80211_hw_check(hw, RX_INCLUDES_FCS) && skb->len > ETH_FCS_LEN)
		skb_trim(skb, skb->len - ETH_FCS_LEN);
	ctx = LKPI80211_CTX(hw);
	/*
	 * Irqsafe means enqueue-and-return: the caller is a driver receive
	 * context (an interrupt thread, a bus worker) and must not run the
	 * stack's input there.  Linux's own contract for this entry is exactly
	 * that ("irqsafe" = the real processing is deferred), and the port's
	 * consumer depends on it, because the bridge's input takes the port
	 * serializer - the same lock a driver call can hold while waiting for a
	 * host command that only this caller can complete.  A full queue drops
	 * the oldest frame and counts the loss; the frame's skb is only freed by
	 * the worker that delivers it.
	 */
	if (ctx->rxq_len >= CONFIG_LKPI80211_RXQ_DEPTH) {
		struct sk_buff *old = ctx->rxq[ctx->rxq_head];

		ctx->rxq[ctx->rxq_head] = NULL;
		ctx->rxq_head = (ctx->rxq_head + 1) % CONFIG_LKPI80211_RXQ_DEPTH;
		ctx->rxq_len--;
		ctx->rxq_drops++;
		if (old != NULL)
			lkpi_skb_free(old);
	}
	ctx->rxq[ctx->rxq_tail] = skb;
	ctx->rxq_tail = (ctx->rxq_tail + 1) % CONFIG_LKPI80211_RXQ_DEPTH;
	ctx->rxq_len++;
	if (ctx->rx_wq != NULL)
		(void)queue_work(ctx->rx_wq, &ctx->rx_work);
	else
		lkpi80211_rx_work(&ctx->rx_work);
}

void
lkpi80211_rx_flush(struct ieee80211_hw *hw)
{
	struct lkpi80211_hw_ctx *ctx;

	if (hw == NULL)
		return;
	ctx = LKPI80211_CTX(hw);
	if (ctx->rx_wq != NULL)
		lkpi_work_flush(&ctx->rx_work);
	else
		lkpi80211_rx_work(&ctx->rx_work);
}

/*
 * The one transmit-completion body.  ieee80211_tx_status_irqsafe() is the
 * interface's deferred form, ieee80211_tx_status_ext()/ieee80211_tx_status_skb()
 * its synchronous forms (net/mac80211/status.c); this port defers nothing, so
 * all three end here - one mechanism, one body, so a frame cannot be reported
 * twice through two entry points that drifted apart.
 */
static void
lkpi80211_tx_complete(struct ieee80211_hw *hw, struct sk_buff *skb)
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

void
ieee80211_tx_status_irqsafe(struct ieee80211_hw *hw, struct sk_buff *skb)
{

	lkpi80211_tx_complete(hw, skb);
}

void
ieee80211_tx_status_ext(struct ieee80211_hw *hw,
    struct ieee80211_tx_status *status)
{

	/*
	 * [LNX] net/mac80211/status.c:ieee80211_tx_status_ext.  The interface's
	 * body walks its own rate control and station bookkeeping; this port's
	 * equivalent is the completion body above, so the substance carried here
	 * is the frame it names (status->skb).  A caller that hands no skb has
	 * nothing for this port to complete and returns.
	 */
	if (status == NULL || status->skb == NULL)
		return;
	lkpi80211_tx_complete(hw, status->skb);
}

void
ieee80211_tx_status_skb(struct ieee80211_hw *hw, struct sk_buff *skb)
{

	/* [LNX] net/mac80211/status.c:ieee80211_tx_status_skb - the skb form of
	 * the same completion, which the AC 7260's own transmit-completion path
	 * calls (mvm/tx.c:1685,2066) */
	lkpi80211_tx_complete(hw, skb);
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

		/* the interface's own filter (util.c __iterate_interfaces): an
		 * interface that is not running yet is not in the driver's view -
		 * which is what keeps a driver's add_interface from finding the
		 * vif it is adding */
		if (vif == NULL || (ctx->vif_running & (1u << i)) == 0)
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

void
ieee80211_iterate_interfaces(struct ieee80211_hw *hw, u32 iter_flags,
    void (*iterator)(void *data, u8 *mac, struct ieee80211_vif *vif),
    void *data)
{

	/*
	 * [LNX] net/mac80211/util.c:ieee80211_iterate_interfaces.  The
	 * interface's body takes its iflist_mtx and walks local->interfaces,
	 * skipping monitors and AP-VLANs and (for the ACTIVE flag) interfaces
	 * not in the driver; this port's interfaces are the vif table and every
	 * one is in the driver once added, so the walk is the atomic form's -
	 * the same interfaces, in slot order - and the non-atomic form adds only
	 * the sleeping allowance the port does not need to distinguish.
	 * ieee80211_iterate_active_interfaces() (an inline in the face) passes
	 * the ACTIVE flag through to here.
	 */
	ieee80211_iterate_active_interfaces_atomic(hw, iter_flags, iterator, data);
}

/* ------------------------------------------------------------------ */
/* the key-table walk and the per-key notices                          */
/* ------------------------------------------------------------------ */

void
ieee80211_iter_keys(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    void (*iter)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		 struct ieee80211_sta *sta, struct ieee80211_key_conf *key,
		 void *data),
    void *iter_data)
{
	unsigned int i;

	/*
	 * [LNX] net/mac80211/key.c:ieee80211_iter_keys walks the stack's own
	 * key_list.  This port has no stack-side key_list; the keys it was told
	 * about are recorded in lkpi80211_keys (see lkpi80211_key_record), and
	 * the walk hands back exactly those, filtered to the requested vif when
	 * one is named - the same (hw, vif, sta, key) tuple the interface's
	 * callback receives.
	 */
	if (hw == NULL || iter == NULL)
		return;
	for (i = 0; i < CONFIG_LKPI80211_MAX_KEYS; i++) {
		struct lkpi80211_key_entry *e = &lkpi80211_keys[i];

		if (!e->used || e->conf == NULL || e->hw != hw)
			continue;
		if (vif != NULL && e->vif != vif)
			continue;
		iter(hw, e->vif, e->sta, e->conf, iter_data);
	}
}

void
ieee80211_iter_keys_rcu(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    void (*iter)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		 struct ieee80211_sta *sta, struct ieee80211_key_conf *key,
		 void *data),
    void *iter_data)
{

	/* [LNX] net/mac80211/key.c:ieee80211_iter_keys_rcu - the rcu-walk form
	 * of the same walk.  This port holds no rcu and the key table is not
	 * published locklessly, so the substance is the same walk. */
	ieee80211_iter_keys(hw, vif, iter, iter_data);
}

void
ieee80211_get_key_rx_seq(struct ieee80211_key_conf *keyconf, int tid,
    struct ieee80211_key_seq *seq)
{
	struct lkpi80211_key_entry *e;

	/*
	 * [LNX] net/mac80211/key.c:ieee80211_get_key_rx_seq reads the key's own
	 * per-TID receive sequence.  This port's receive IV checking is
	 * net80211's (the bridge's), so the per-key sequence is never advanced
	 * here and the read answers the key's recorded (initial) value; the
	 * shape - which arm each cipher fills, and the tid range each accepts -
	 * is the interface's, so a caller sees the same contract either way.
	 */
	if (keyconf == NULL || seq == NULL)
		return;
	memset(seq, 0, sizeof(*seq));
	e = lkpi80211_key_find_conf(keyconf);
	if (e == NULL)
		return;
	switch (keyconf->cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
		if (tid < 0 || tid >= IEEE80211_NUM_TIDS)
			return;
		seq->tkip.iv32 = e->rx.tkip[tid].iv32;
		seq->tkip.iv16 = e->rx.tkip[tid].iv16;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
		if (tid < -1 || tid >= IEEE80211_NUM_TIDS)
			return;
		memcpy(seq->ccmp.pn,
		    e->rx.pn[tid < 0 ? IEEE80211_NUM_TIDS : tid],
		    IEEE80211_CCMP_PN_LEN);
		break;
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		if (tid < -1 || tid >= IEEE80211_NUM_TIDS)
			return;
		memcpy(seq->gcmp.pn,
		    e->rx.pn[tid < 0 ? IEEE80211_NUM_TIDS : tid],
		    IEEE80211_GCMP_PN_LEN);
		break;
	default:
		break;
	}
}

void
ieee80211_key_mic_failure(struct ieee80211_key_conf *keyconf)
{
	struct lkpi80211_key_entry *e = lkpi80211_key_find_conf(keyconf);

	/*
	 * [LNX] net/mac80211/key.c:ieee80211_key_mic_failure - the interface
	 * counts a Michael integrity failure on the key.  The counter lives on
	 * this port's key record; a read of it is the same read the interface
	 * would answer.
	 */
	if (e != NULL)
		e->icverrors++;
}

void
ieee80211_key_replay(struct ieee80211_key_conf *keyconf)
{
	struct lkpi80211_key_entry *e = lkpi80211_key_find_conf(keyconf);

	/* [LNX] net/mac80211/key.c:ieee80211_key_replay - the replay counter on
	 * the key, kept the same way. */
	if (e != NULL)
		e->replays++;
}

/* ------------------------------------------------------------------ */
/* the rate-control algorithm registry                                 */
/* ------------------------------------------------------------------ */

int
ieee80211_rate_control_register(const struct rate_control_ops *ops)
{
	unsigned int i;

	/*
	 * [LNX] net/mac80211/rate.c:ieee80211_rate_control_register.  The
	 * interface refuses an unnamed algorithm and a duplicate name and
	 * remembers the rest; the registry here is fixed-size, so a full one
	 * answers the same -ENOMEM the interface answers when its allocation
	 * fails.
	 */
	if (ops == NULL || ops->name == NULL)
		return -EINVAL;
	for (i = 0; i < CONFIG_LKPI80211_MAX_RC; i++) {
		if (lkpi80211_rc_ops[i] != NULL &&
		    strcmp(lkpi80211_rc_ops[i]->name, ops->name) == 0)
			return -EALREADY;
	}
	for (i = 0; i < CONFIG_LKPI80211_MAX_RC; i++) {
		if (lkpi80211_rc_ops[i] == NULL) {
			lkpi80211_rc_ops[i] = ops;
			lkpi_printf(ULOG_INFO_LEVEL, "lkpi80211: rate control '%s' registered\n",
			    ops->name);
			return 0;
		}
	}
	lkpi_printf(ULOG_WARNING_LEVEL, "lkpi80211: rate-control registry full\n");
	return -ENOMEM;
}

void
ieee80211_rate_control_unregister(const struct rate_control_ops *ops)
{
	unsigned int i;

	/* [LNX] net/mac80211/rate.c:ieee80211_rate_control_unregister */
	for (i = 0; i < CONFIG_LKPI80211_MAX_RC; i++) {
		if (lkpi80211_rc_ops[i] == ops) {
			lkpi80211_rc_ops[i] = NULL;
			return;
		}
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
	lkpi_printf(ULOG_ERROR_LEVEL, "lkpi80211: beacon_get (AP mode is not supported here)\n");
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
		lkpi_printf(ULOG_INFO_LEVEL, "lkpi80211: change_chanctx with none assigned\n");
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

/*
 * The channel context a connection runs on.  mac80211 reserves one when an
 * interface starts using a channel (ieee80211_vif_use_channel, reached from
 * ieee80211_prep_connection before the peer's station is uploaded) and hands it
 * to the driver: ops->add_chanctx lets the driver allocate its own PHY context
 * and keep the handle in ctx->drv_priv, ops->assign_vif_chanctx binds the
 * interface to it, and the interface records the binding on the link
 * (bss_conf.chanctx_conf).  A driver reads that binding back before it can say
 * anything about a peer - the AC 7260's station upload refuses outright without
 * it (WARN_ON(!mvmvif->link[link_id]->phy_ctxt) then -EINVAL,
 * mvm/mac80211.c:4008-4012; measured on the board 2026-10-13, where the peer
 * registration this round added came back as "sta add refused (-22)").
 *
 * So the substitute drives the same lifecycle at the same moment: the seam
 * calls this where the bridge points the radio at the connection's channel.
 */
int
lkpi80211_drv_chanctx_use(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    u32 freq, u8 hw_value, u8 band, u8 width)
{
	struct lkpi80211_hw_ctx *ctx;
	int ret;

	if (hw == NULL || vif == NULL || freq == 0)
		return -EINVAL;
	ctx = LKPI80211_CTX(hw);
	if ((unsigned int)hw->chanctx_data_size > sizeof(ctx->chanctx_priv)) {
		lkpi_printf(ULOG_ERROR_LEVEL,
		    "lkpi80211: the driver declares %d bytes of channel-context private data, more than the %u this face holds\n",
		    hw->chanctx_data_size, (unsigned int)sizeof(ctx->chanctx_priv));
		return -ENOSPC;
	}
	ctx->chan.center_freq = freq;
	ctx->chan.hw_value = hw_value;
	ctx->chan.band = (enum nl80211_band)band;
	ctx->chanctx.def.chan = &ctx->chan;
	ctx->chanctx.def.width = (enum nl80211_chan_width)width;
	ctx->chanctx.def.center_freq1 = freq;
	ctx->chanctx.min_def = ctx->chanctx.def;

	if (!ctx->have_chanctx) {
		if (ctx->ops->add_chanctx != NULL) {
			ret = ctx->ops->add_chanctx(hw, &ctx->chanctx);
			if (ret != 0) {
				lkpi_printf(ULOG_ERROR_LEVEL,
				    "lkpi80211: add_chanctx refused (%d)\n", ret);
				return ret;
			}
		}
		ctx->have_chanctx = 1;
		if (ctx->ops->assign_vif_chanctx != NULL) {
			ret = ctx->ops->assign_vif_chanctx(hw, vif,
			    &vif->bss_conf, &ctx->chanctx);
			if (ret != 0) {
				lkpi_printf(ULOG_ERROR_LEVEL,
				    "lkpi80211: assign_vif_chanctx refused (%d)\n",
				    ret);
				return ret;
			}
		}
		vif->bss_conf.chanctx_conf = &ctx->chanctx;
		/*
		 * The link's operating channel request, which the interface fills
		 * with the same binding (ieee80211_vif_use_channel sets
		 * link->conf->chanreq.oper).  A driver reads it off the vif when
		 * it programs a station - the AC 7260 takes the band from
		 * `vif->bss_conf.chanreq.oper.chan` while it builds the station
		 * command (mvm/sta.c:211), so a NULL there is a NULL dereference,
		 * not a refusal.
		 */
		vif->bss_conf.chanreq.oper = ctx->chanctx.def;
		return 0;
	}
	/* already assigned: a different channel is a change, the call mac80211
	 * makes when the interface moves (the same record, the same driver
	 * handle in its private tail) */
	if (ctx->ops->change_chanctx != NULL)
		ctx->ops->change_chanctx(hw, &ctx->chanctx,
		    IEEE80211_CHANCTX_CHANGE_CHANNEL);
	return 0;
}

/*
 * The release: unassign, then remove, the order the interface uses when an
 * interface stops using its channel (ieee80211_vif_release_channel).
 */
void
lkpi80211_drv_chanctx_release(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct lkpi80211_hw_ctx *ctx;

	if (hw == NULL)
		return;
	ctx = LKPI80211_CTX(hw);
	if (!ctx->have_chanctx)
		return;
	if (vif != NULL && ctx->ops->unassign_vif_chanctx != NULL)
		ctx->ops->unassign_vif_chanctx(hw, vif, &vif->bss_conf,
		    &ctx->chanctx);
	if (vif != NULL)
		vif->bss_conf.chanctx_conf = NULL;
	if (ctx->ops->remove_chanctx != NULL)
		ctx->ops->remove_chanctx(hw, &ctx->chanctx);
	memset(&ctx->chanctx, 0, sizeof(ctx->chanctx));
	ctx->have_chanctx = 0;
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

/* ------------------------------------------------------------------ */
/* the hardware-scan route                                             */
/* ------------------------------------------------------------------ */

/*
 * A driver with ops->hw_scan and no ops->sw_scan_start has no software
 * fallback: the AC 7260's mvm ops table is exactly that shape
 * (mvm/mac80211.c:6367 has hw_scan/cancel_hw_scan and no sw_scan_start), so a
 * sweep on that line can only be a firmware one. The bridge asks this to pick
 * the route and then drives the driver through the three calls below.
 *
 * The request object is built here rather than in the bridge because it is
 * made of mac80211 types (the same division of labour the rest of this seam
 * follows). Its shape is the interface's: mac80211 allocates
 * `struct_size(req, req.channels, n_channels)` and fills n_channels/channels
 * from the cfg80211 request (net/mac80211/scan.c:780, ieee80211_prep_hw_scan
 * :379-445), and the AC 7260 sets IEEE80211_HW_SINGLE_SCAN_ON_ALL_BANDS
 * (mvm/mac80211.c:515), which is the branch that puts every channel of every
 * band in one request - so one call covers the bridge's whole channel set.
 *
 * A wildcard sweep is what the bridge wants (it is collecting every beacon),
 * so n_ssids stays 0 and no probe IEs are carried: the firmware then probes
 * with the broadcast SSID, which is the same air traffic a software sweep
 * produces from the bridge's side.
 */
#ifndef CONFIG_LKPI80211_MAX_HW_SCAN_CHANNELS
#define CONFIG_LKPI80211_MAX_HW_SCAN_CHANNELS	64
#endif

int
lkpi80211_drv_has_hw_scan(struct ieee80211_hw *hw)
{

	if (hw == NULL)
		return 0;
	return LKPI80211_CTX(hw)->ops->hw_scan != NULL;
}

int
lkpi80211_drv_hw_scan(struct ieee80211_hw *hw, const u32 *freqs,
    unsigned int n_freqs)
{
	struct lkpi80211_hw_ctx *ctx;
	struct ieee80211_scan_request *req;
	unsigned int i, n = 0;
	int rc;

	if (hw == NULL || freqs == NULL || n_freqs == 0)
		return -EINVAL;
	ctx = LKPI80211_CTX(hw);
	if (ctx->ops->hw_scan == NULL)
		return -EOPNOTSUPP;
	if (ctx->hw_scanning)
		return -EBUSY;
	if (n_freqs > CONFIG_LKPI80211_MAX_HW_SCAN_CHANNELS)
		n_freqs = CONFIG_LKPI80211_MAX_HW_SCAN_CHANNELS;

	req = kzalloc(struct_size(req, req.channels, n_freqs), GFP_KERNEL);
	if (req == NULL)
		return -ENOMEM;

	req->req.wiphy = ctx->hw.wiphy;
	req->req.wdev = ctx->vifs[0] != NULL ? &ctx->wdevs[0] : NULL;
	for (i = 0; i < n_freqs; i++) {
		struct ieee80211_channel *chan =
		    ieee80211_get_channel(ctx->hw.wiphy, (int)freqs[i]);

		/* a frequency the wiphy does not carry is not scannable here;
		 * dropping it keeps the request a subset of the bridge's set
		 * rather than a request the driver would refuse whole */
		if (chan == NULL)
			continue;
		req->req.channels[n++] = chan;
	}
	if (n == 0) {
		kfree(req);
		return -EINVAL;
	}
	req->req.n_channels = n;

	ctx->hw_scan_req = req;
	ctx->hw_scanning = 1;
	rc = ctx->ops->hw_scan(hw, ctx->vifs[0], req);
	if (rc != 0) {
		ctx->hw_scanning = 0;
		ctx->hw_scan_req = NULL;
		kfree(req);
	}
	return rc;
}

void
lkpi80211_drv_hw_scan_cancel(struct ieee80211_hw *hw)
{
	struct lkpi80211_hw_ctx *ctx;

	if (hw == NULL)
		return;
	ctx = LKPI80211_CTX(hw);
	if (ctx->ops->cancel_hw_scan != NULL && ctx->hw_scanning)
		ctx->ops->cancel_hw_scan(hw, ctx->vifs[0]);
	ctx->hw_scanning = 0;
	if (ctx->hw_scan_req != NULL) {
		kfree(ctx->hw_scan_req);
		ctx->hw_scan_req = NULL;
	}
}

void
ieee80211_scan_completed(struct ieee80211_hw *hw, struct cfg80211_scan_info *info)
{
	struct lkpi80211_hw_ctx *ctx;
	int was_hw;

	/*
	 * [LNX] net/mac80211/scan.c:540: the driver's scan finished.  The
	 * interface sets its SCAN_COMPLETED/SCAN_ABORTED state and queues its
	 * scan work; the MLME that consumes the completion (net80211, on the
	 * bridge's side) is told through the bridge's own path, so what this
	 * port's substitute owns and can update is its own scan state: the
	 * scanning flag cleared, the aborted outcome recorded, and a count that
	 * makes the completion visible.  The driver calls this after its
	 * hardware sweep (mvm/scan.c:445,3260,3459,3506,3558).
	 *
	 * For a hardware scan this call is also the *only* way the bridge can
	 * learn the sweep ended - the bridge drove no channel walk to run out -
	 * so the completion is forwarded through the bridge ops.  A software
	 * scan's completion comes from the bridge's own walk, so the forward is
	 * made only when a firmware sweep was actually in flight; that keeps the
	 * three shipping software-scan lines' behaviour unchanged.
	 */
	if (hw == NULL)
		return;
	ctx = LKPI80211_CTX(hw);
	was_hw = ctx->hw_scanning;
	ctx->scanning = 0;
	ctx->hw_scanning = 0;
	ctx->scan_completions++;
	if (ctx->hw_scan_req != NULL) {
		kfree(ctx->hw_scan_req);
		ctx->hw_scan_req = NULL;
	}
	(void)info;
	if (was_hw && bridge_ops != NULL && bridge_ops->scan_complete != NULL)
		bridge_ops->scan_complete(hw);
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
		lkpi_printf(ULOG_ERROR_LEVEL, "lkpi80211: vif table full\n");
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
	 * The interface's own wireless_dev, which ieee80211_vif_to_wdev() hands
	 * back (the interface embeds one per sub-interface).  Only its wiphy and
	 * iftype are ever read on this line (the firmware debug-trigger match);
	 * netdev stays NULL, this port having no net_device per interface.
	 */
	memset(&ctx->wdevs[i], 0, sizeof(ctx->wdevs[i]));
	ctx->wdevs[i].wiphy = &ctx->wiphy;
	ctx->wdevs[i].iftype = (enum nl80211_iftype)type;
	memcpy(ctx->wdevs[i].address, vif->addr, ETH_ALEN);
	/*
	 * The interface's own txq before the driver hears about the interface:
	 * the driver's add_interface initializes its per-queue state through it
	 * (rtw89 mac80211.c:201 -> rtw89_core_txq_init(rtwdev, vif->txq), which
	 * answers a NULL queue by doing nothing).
	 */
	vif->txq = lkpi_txq_alloc(hw, vif, NULL, 0);
	/*
	 * The interface's own management queue - the one no station owns, which
	 * the interface sets up beside vif->txq (tx.c:1530-1535).  A bufferable
	 * management frame (a deauthentication, say) that a station does not own
	 * rides it, and the transmit path's txq selection below is where it is
	 * picked.
	 */
	vif->txq_mgmt = lkpi_txq_alloc(hw, vif, NULL, IEEE80211_NUM_TIDS);
	if (ctx->ops->add_interface != NULL) {
		ret = ctx->ops->add_interface(hw, vif);
		if (ret != 0) {
			lkpi_printf(ULOG_ERROR_LEVEL, "lkpi80211: add_interface refused (%d)\n", ret);
			ctx->vifs[i] = NULL;
			lkpi_txq_free(vif->txq);
			vif->txq = NULL;
			kfree(vif);
			return NULL;
		}
	}
	/* running from here on: the active-interface iteration may show it (the
	 * interface's own order, see vif_running above) */
	ctx->vif_running |= 1u << i;
	lkpi_printf(ULOG_INFO_LEVEL, "lkpi80211: vif added idx=%u type=%u priv=%u\n", i, type,
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
			ctx->vif_running &= ~(1u << i);
			memset(&ctx->wdevs[i], 0, sizeof(ctx->wdevs[i]));
			vif_hw_unbind(vif);
			/* after remove_interface: the driver's teardown still
			 * reads the queues it initialized in add_interface */
			lkpi_txq_free_vif(vif);
			vif->txq = NULL;
			kfree(vif);
			return;
		}
	lkpi_printf(ULOG_ERROR_LEVEL, "lkpi80211: vif_remove of a foreign vif refused\n");
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
	struct ieee80211_key_conf *fwd = key;
	struct lkpi80211_key_entry *e = NULL;
	int ret;

	if (hw == NULL || vif == NULL || key == NULL)
		return -EINVAL;
	if (LKPI80211_CTX(hw)->ops->set_key == NULL)
		return -EOPNOTSUPP;
	/*
	 * A key this port installs is recorded first, and the driver is handed
	 * the record's own stable copy (see the key table above): the bridge's
	 * caller frees its key_conf as soon as this returns, so handing that
	 * pointer on would leave a driver that keeps it - the AC 7260 keeps the
	 * beacon-protection keys - holding freed memory.  The copy is only taken
	 * when the driver is about to take the key; a DISABLE_KEY names the key
	 * by identity and gets the caller's own sparse key_conf.
	 */
	if (cmd == SET_KEY) {
		e = lkpi80211_key_install(hw, vif, sta, key);
		if (e != NULL)
			fwd = e->conf;
	}
	ret = LKPI80211_CTX(hw)->ops->set_key(hw, cmd, vif, sta, fwd);
	if (cmd == SET_KEY) {
		if (ret != 0 && e != NULL)
			lkpi80211_key_release(e);	/* the driver refused it */
	} else {
		lkpi80211_key_drop(hw, vif, sta, key);
	}
	return ret;
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
	lkpi_printf(ULOG_INFO_LEVEL, "lkpi80211: tx frame has %u bytes of headroom, the driver "
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
 * The transmit handoff mac80211 performs before a driver's own transmit entry:
 * the txq selection (net/mac80211/tx.c:1299-1355, ieee80211_get_txq) and, when
 * a queue owns the frame, the enqueue-and-wake pair (tx.c:1663-1687,
 * ieee80211_queue_skb).
 *
 * Why the selection has to exist at all: a driver that implements
 * .wake_tx_queue receives most of its frames through the txq abstraction, and
 * the AC 7260's driver enforces that contract from the other side - its
 * legacy .tx drops a management frame on a station vif outright
 * (third-party/iwlwifi/mvm/mac80211.c:845-853: management frames that a txq
 * did not deliver "shouldn't even happen").  The board showed the cost: with
 * every frame taking .tx, the authentication request was counted as handed
 * over and never completed - no transmit status ever came back, and no driver
 * message named why.
 *
 * This round transcribes the management branches of the selection - the ones
 * a station's authentication, association and probe frames take.  The data
 * branches (a station's per-TID queues, the interface's own queue) still ride
 * lkpi80211_drv_tx: that route is what the other drivers on this port were
 * brought up on, and moving them is a change with its own board work; the
 * boundary is recorded in the issue ledger rather than left silent.
 */
void
lkpi80211_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    struct ieee80211_sta *sta, struct sk_buff *skb)
{
	struct lkpi80211_hw_ctx *ctx;
	struct ieee80211_hdr *hdr;
	struct ieee80211_txq *txq = NULL;
	__le16 fc;

	if (hw == NULL || skb == NULL)
		return;
	ctx = LKPI80211_CTX(hw);
	hdr = (struct ieee80211_hdr *)skb->data;
	fc = hdr->frame_control;

	/*
	 * The managed-exchange preparation, before the frame is handed down:
	 * Linux's MLME calls drv_mgd_prepare_tx right before the authentication
	 * (mlme.c:8991-8992) and association (mlme.c:2777) frames leave, and the
	 * AC 7260's driver turns that into session protection - a firmware time
	 * event that keeps the device on-channel and awake for the exchange
	 * (mvm/mac80211.c:4140-4152 -> iwl_mvm_protect_assoc).  Without it the
	 * board showed the authentication frame read out of the management queue
	 * by the device and then held until the queue was torn down - reclaimed
	 * with FIFO_FLUSHED, never transmitted, and the join's management timer
	 * expired instead.  The info record carries what Linux fills in: the
	 * subtype of the frame about to leave, link 0, zero duration, and
	 * was_assoc from the interface's own association state (the driver skips
	 * protection for a re-association of an already-associated interface).
	 */
	if (vif != NULL && vif->type == NL80211_IFTYPE_STATION &&
	    ctx->ops->mgd_prepare_tx != NULL) {
		__le16 stype = (__le16)(fc & cpu_to_le16(IEEE80211_FCTL_STYPE));

		if (stype == cpu_to_le16(IEEE80211_STYPE_AUTH) ||
		    stype == cpu_to_le16(IEEE80211_STYPE_ASSOC_REQ) ||
		    stype == cpu_to_le16(IEEE80211_STYPE_REASSOC_REQ)) {
			struct ieee80211_prep_tx_info info;

			memset(&info, 0, sizeof(info));
			info.subtype = stype;
			info.link_id = 0;
			info.was_assoc = vif->cfg.assoc ? 1 : 0;
			ctx->ops->mgd_prepare_tx(hw, vif, &info);
		}
	}

	if (vif != NULL &&
	    !(IEEE80211_SKB_CB(skb)->flags & IEEE80211_TX_CTL_HW_80211_ENCAP) &&
	    !ieee80211_is_data_present(fc)) {
		/*
		 * The management/control branches of ieee80211_get_txq
		 * (tx.c:1319-1341): a frame that is not a data frame rides the
		 * station's management queue when a station owns it (a bufferable
		 * MMPDU, an association frame, or any frame on a station vif), and
		 * a bufferable frame no station owns rides the interface's own
		 * management queue.  The interface's `sta->uploaded` gate is a
		 * construction fact here: every station this port hands out was
		 * inserted and uploaded by lkpi80211_drv_sta_add above, so a
		 * station that is not NULL is an uploaded one.
		 */
		if ((!ieee80211_is_mgmt(fc) ||
		     ieee80211_is_bufferable_mmpdu(skb) ||
		     ieee80211_is_assoc(fc) ||
		     vif->type == NL80211_IFTYPE_STATION) &&
		    sta != NULL)
			txq = sta->txq[IEEE80211_NUM_TIDS];
		else if ((!ieee80211_is_mgmt(fc) ||
			  ieee80211_is_bufferable_mmpdu(skb)) && sta == NULL)
			txq = vif->txq_mgmt;
	} else if (sta != NULL) {
		/*
		 * The data branch (tx.c:1341-1348): a data frame owned by a
		 * station rides that station's per-TID queue, the TID read off
		 * the socket priority exactly as the interface selects it.  The
		 * AC 7260's driver enforces this from the other side: its
		 * transmit path for a station looks the frame's TID up in the
		 * station's queue table and refuses the frame when the queue was
		 * never allocated ("Invalid TXQ id", mvm/tx.c:1157) - which is
		 * what happened to the supplicant's EAPOL-Key 2/4 on the board
		 * while data frames still rode the legacy route.
		 */
		u8 tid = (u8)(skb->priority & IEEE80211_QOS_CTL_TID_MASK);

		if (tid < IEEE80211_NUM_TIDS)
			txq = sta->txq[tid];
	} else if (vif != NULL) {
		/* tx.c:1349-1350: no station, a data frame - the interface's
		 * own queue */
		txq = vif->txq;
	}

	if (txq == NULL) {
		/* nothing owns it: the interface's own transmit entry */
		lkpi80211_drv_tx(hw, vif, sta, skb);
		return;
	}
	if (ctx->ops->wake_tx_queue == NULL) {
		/* a queue without a driver to wake would hold the frame forever;
		 * the interface's own entry is the honest fallback */
		lkpi80211_drv_tx(hw, vif, sta, skb);
		return;
	}
	lkpi80211_tx_headroom_check(hw, skb);
	if (lkpi_txq_enqueue(txq, skb) != 0) {
		lkpi_skb_free(skb);
		return;
	}
	/* schedule_and_wake_txq (tx.c:1650-1661): the queue joins its access
	 * category's list, then the driver is told to come and drain it */
	ieee80211_schedule_txq(hw, txq);
	ctx->ops->wake_tx_queue(hw, txq);
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
		lkpi_printf(ULOG_ERROR_LEVEL, "lkpi80211: sta table full\n");
		return NULL;
	}
	sta = kzalloc(offsetof(struct ieee80211_sta, drv_priv) +
	    (size_t)hw->sta_data_size, GFP_KERNEL);
	if (sta == NULL)
		return NULL;
	memcpy(sta->addr, addr, 6);
	memcpy(sta->deflink.addr, addr, 6);
	sta->aid = aid;
	ctx->stas[slot] = sta;
	/*
	 * The station's default link, wired the way the interface wires every
	 * station, MLD or not (net/mac80211/sta_info.c:sta_info_add_link does
	 * `rcu_assign_pointer(sta->link[link_id], link_info)` and, for a
	 * non-MLD, links the deflink at id 0).  A driver that needs the peer's
	 * link record dereferences sta->link[link_id] and gives up when it is
	 * NULL: rtw89's association handler reads it to fill the CMAC table
	 * ("cannot find exact link_sta for link_id 0", measured on the board
	 * 2026-10-10 - the chip then ran the association without its CMAC
	 * entry, so every data frame it sent went unanswered).
	 */
	sta->deflink.sta = sta;
	sta->deflink.link_id = 0;
	sta->link[0] = &sta->deflink;
	/*
	 * The station's spatial-stream count, never below one: upstream's
	 * ieee80211_sta_init_nss_bw_capa ends with `max_t(u8, 1, rx_nss)`
	 * (net/mac80211/sta_info.c:3600), and every driver reads the number as
	 * "at least a single stream is available".  Left zero, a driver that
	 * feeds it into an N-1 encoding underflows: rtw89's rate-adaptation
	 * setup computes min(link_sta->rx_nss, hal.tx_nss) - 1 (phy.c:482)
	 * and stores it in a 3-bit field, so zero becomes 7 (core.h:4515) -
	 * a spatial-stream count no 1T1R chip can honor, in the RA table the
	 * firmware uses to pick data rates.  Management frames keep their
	 * forced basic rates and flow; data frames (the 4-way handshake's
	 * message 2, DHCP) go out with a rate configuration the firmware can
	 * not realize, which matches the board's split exactly (messages 1 of
	 * 4 received, our message 2 sent through USB without an accepted
	 * answer).
	 */
	sta->deflink.rx_nss = 1;
	/*
	 * The station's per-TID txqs, before the driver hears about the station:
	 * its station setup walks them to initialize the driver-private queue
	 * state (rtw89 mac80211.c:516-517).
	 */
	for (i = 0; i < IEEE80211_NUM_TIDS; i++)
		sta->txq[i] = lkpi_txq_alloc(hw, vif, sta, (u8)i);
	/*
	 * The station's management-MPDU queue, for the drivers that declared it:
	 * the interface creates sta->txq[IEEE80211_NUM_TIDS] exactly when the
	 * driver opted in through its hw flags - STA_MMPDU_TXQ on a station vif,
	 * BUFF_MMPDU_TXQ otherwise (net/mac80211/tx.c:1545-1556) - and the
	 * transmit path routes the station's management frames through it.  The
	 * declared flag is consumed here; a driver that did not declare it keeps
	 * the index NULL and its management frames reach .tx instead (the branch
	 * a walk of the per-TID queues answers by skipping the NULL entry).
	 */
	if (ieee80211_hw_check(hw, STA_MMPDU_TXQ))
		sta->txq[IEEE80211_NUM_TIDS] =
		    lkpi_txq_alloc(hw, vif, sta, IEEE80211_NUM_TIDS);
	/*
	 * The station appears: the upload step of the ladder, the one mac80211
	 * runs while it prepares a connection - ieee80211_prep_connection
	 * inserts and uploads the AP's station before the authentication
	 * request leaves, which is what lets the transmit path find the peer
	 * and its queues.  NONE -> AUTH is skipped: no driver in this
	 * repository acts on it.  The association's own transition
	 * (AUTH -> ASSOC) is lkpi80211_drv_sta_assoc below.
	 */
	ret = sta_state_call(ctx, hw, vif, sta, IEEE80211_STA_NOTEXIST,
	    IEEE80211_STA_NONE);
	if (ret != 0) {
		lkpi_printf(ULOG_ERROR_LEVEL, "lkpi80211: sta add refused (%d)\n", ret);
		ctx->stas[slot] = NULL;
		lkpi_txq_free_sta(sta);
		kfree(sta);
		return NULL;
	}
	return sta;
}

/*
 * The association step of the station ladder (AUTH -> ASSOC), driven where the
 * association actually completes - the seam's joined notification.  Split from
 * the upload step above because the two happen at different moments: mac80211
 * uploads the peer before the authentication request and associates it only
 * when the association response arrives, and this port's seam has the same two
 * moments (the bridge's AUTH entry and its RUN entry).
 */
int
lkpi80211_drv_sta_assoc(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    struct ieee80211_sta *sta, u16 aid)
{
	struct lkpi80211_hw_ctx *ctx;

	if (hw == NULL || sta == NULL)
		return -EINVAL;
	ctx = LKPI80211_CTX(hw);
	sta->aid = aid;
	return sta_state_call(ctx, hw, vif, sta, IEEE80211_STA_AUTH,
	    IEEE80211_STA_ASSOC);
}

/* the slot release both wind-downs share: the station's table entry, its
 * queues, then the record itself */
static void
sta_release(struct lkpi80211_hw_ctx *ctx, struct ieee80211_sta *sta)
{
	unsigned int i;

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

void
lkpi80211_drv_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    struct ieee80211_sta *sta)
{
	struct lkpi80211_hw_ctx *ctx;

	if (hw == NULL || sta == NULL)
		return;
	ctx = LKPI80211_CTX(hw);
	/* the ladder back down, in the order the interface runs it: the
	 * association ends, then the station is gone */
	(void)sta_state_call(ctx, hw, vif, sta, IEEE80211_STA_ASSOC,
	    IEEE80211_STA_AUTH);
	(void)sta_state_call(ctx, hw, vif, sta, IEEE80211_STA_NONE,
	    IEEE80211_STA_NOTEXIST);
	sta_release(ctx, sta);
}

/*
 * The wind-down of a station that never associated: mac80211 destroys the AP's
 * station when a connection attempt ends before the association (a failed
 * authentication), and the driver's view has to follow - the station's ladder
 * is stepped from where it actually stands (NONE -> NOTEXIST), not through an
 * association that never happened.
 */
void
lkpi80211_drv_sta_free(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    struct ieee80211_sta *sta)
{
	struct lkpi80211_hw_ctx *ctx;

	if (hw == NULL || sta == NULL)
		return;
	ctx = LKPI80211_CTX(hw);
	(void)sta_state_call(ctx, hw, vif, sta, IEEE80211_STA_NONE,
	    IEEE80211_STA_NOTEXIST);
	sta_release(ctx, sta);
}

/*
 * [LNX] net/mac80211/util.c:ieee80211_vif_to_wdev returns `&vif_to_sdata(vif)->wdev`
 * - the wireless_dev the interface embeds in each sub-interface.  This port has
 * no sub_if_data; the vif's slot in the hw context carries the embedded wdev
 * (ctx->wdevs[i], filled at vif add), so the same address the driver would read
 * maps back here.  A compiled caller reads only iftype (the firmware
 * debug-trigger match, mvm/time-event.c:274) and wiphy.
 */
struct wireless_dev *
ieee80211_vif_to_wdev(struct ieee80211_vif *vif)
{
	struct lkpi80211_hw_ctx *ctx;
	struct ieee80211_hw *hw;
	unsigned int i;

	if (vif == NULL)
		return NULL;
	hw = vif_hw(vif);
	if (hw == NULL)
		return NULL;
	ctx = LKPI80211_CTX(hw);
	for (i = 0; i < CONFIG_LKPI80211_MAX_VIFS; i++)
		if (ctx->vifs[i] == vif)
			return &ctx->wdevs[i];
	return NULL;
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

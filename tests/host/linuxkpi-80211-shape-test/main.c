/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * main.c - the ops-shape dispatch cases.
 *
 * The interface lets a driver declare its station and interface changes in one
 * of two families: the older .bss_info_changed / .sta_add / .sta_remove, or the
 * newer .vif_cfg_changed + .link_info_changed / .sta_state.  The substitute has
 * to speak both, because it carries one driver of each kind - rtl8xxxu declares
 * the older family (core.c:7604,7617-7618) and rtw89 the newer one
 * (mac80211.c:2024-2030).  What these cases pin is the dispatch itself: which
 * op is called, in what order, and with which bits - the rules upstream states
 * in net/mac80211/driver-ops.c:drv_sta_state and net/mac80211/main.c:203-254.
 *
 * Exit status is 0 only when every case held.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"
#include "linuxkpi_impl.h"
#include "linuxkpi_core.h"
#include "linuxkpi_80211.h"

#include <net/mac80211.h>

/* ------------------------------------------------------------------ */
/* the case harness                                                    */
/* ------------------------------------------------------------------ */

static unsigned int g_fail;
static unsigned int g_cases;

static void
case_line(const char *name, int ok, const char *fmt, ...)
{
	va_list ap;

	g_cases++;
	if (!ok)
		g_fail++;
	printf("SHAPE: %s %s", name, ok ? "PASS" : "FAIL");
	if (fmt != NULL && *fmt != '\0') {
		printf(" ");
		va_start(ap, fmt);
		vprintf(fmt, ap);
		va_end(ap);
	}
	printf("\n");
	fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* the scripted drivers                                                */
/* ------------------------------------------------------------------ */

enum shape {
	SHAPE_73,	/* vif_cfg_changed + link_info_changed + sta_state */
	SHAPE_6X,	/* bss_info_changed + sta_add/sta_remove */
	SHAPE_BOTH,	/* both families at once (the interface allows it) */
};

/* what the private area holds: which table this hw was allocated with */
struct shape_priv {
	enum shape	shape;
};

/* the call log: one entry per driver op the substitute reaches */
enum ev_kind {
	EV_ADD_IFACE,
	EV_REMOVE_IFACE,
	EV_STA_STATE,
	EV_STA_ADD,
	EV_STA_REMOVE,
	EV_VIF_CFG,
	EV_LINK_INFO,
	EV_BSS_INFO,
	EV_MAX
};

static const char *const ev_name[EV_MAX] = {
	"add_interface", "remove_interface", "sta_state", "sta_add",
	"sta_remove", "vif_cfg_changed", "link_info_changed",
	"bss_info_changed",
};

#define LOG_MAX	32

struct ev {
	unsigned int		kind;
	u64			changed;
	int			old_state;
	int			new_state;
	struct ieee80211_bss_conf *conf;
	struct ieee80211_vif	*vif;
};

static struct ev g_log[LOG_MAX];
static unsigned int g_log_n;

static void
log_add(unsigned int kind, struct ieee80211_vif *vif, u64 changed, int old_s,
    int new_s, struct ieee80211_bss_conf *conf)
{

	if (g_log_n >= LOG_MAX)
		return;
	g_log[g_log_n].kind = kind;
	g_log[g_log_n].changed = changed;
	g_log[g_log_n].old_state = old_s;
	g_log[g_log_n].new_state = new_s;
	g_log[g_log_n].conf = conf;
	g_log[g_log_n].vif = vif;
	g_log_n++;
}

static void
log_reset(void)
{

	memset(g_log, 0, sizeof(g_log));
	g_log_n = 0;
}

/* how many times an op was reached */
static unsigned int
log_count(unsigned int kind)
{
	unsigned int i, n = 0;

	for (i = 0; i < g_log_n; i++)
		if (g_log[i].kind == kind)
			n++;
	return n;
}

/* the n-th entry of that kind (0-based), or NULL */
static const struct ev *
log_nth(unsigned int kind, unsigned int n)
{
	unsigned int i;

	for (i = 0; i < g_log_n; i++)
		if (g_log[i].kind == kind) {
			if (n == 0)
				return &g_log[i];
			n--;
		}
	return NULL;
}

static const struct ev *
log_first(unsigned int kind)
{

	return log_nth(kind, 0);
}

/* the position of the first entry of that kind, or -1 */
static int
log_pos(unsigned int kind)
{
	unsigned int i;

	for (i = 0; i < g_log_n; i++)
		if (g_log[i].kind == kind)
			return (int)i;
	return -1;
}

static void
log_dump(void)
{
	unsigned int i;

	for (i = 0; i < g_log_n; i++)
		fprintf(stderr, "  [%u] %s changed=%#llx %d->%d\n", i,
		    ev_name[g_log[i].kind],
		    (unsigned long long)g_log[i].changed,
		    g_log[i].old_state, g_log[i].new_state);
}

/* ------------------------------------------------------------------ */
/* the driver ops                                                      */
/* ------------------------------------------------------------------ */

/*
 * The interface's link address as the driver saw it *inside* add_interface -
 * which is the moment a driver reads it.  rtw89 copies it into its per-link
 * record there and programs the chip's address CAM from it.
 */
static u8 g_add_link_addr[6];
static int g_add_link_addr_seen;

static int
drv_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{

	(void)hw;
	memcpy(g_add_link_addr, vif->bss_conf.addr, 6);
	g_add_link_addr_seen = 1;
	log_add(EV_ADD_IFACE, vif, 0, 0, 0, NULL);
	return 0;
}

static void
drv_remove_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{

	(void)hw;
	log_add(EV_REMOVE_IFACE, vif, 0, 0, 0, NULL);
}

static int
drv_sta_state(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    struct ieee80211_sta *sta, enum ieee80211_sta_state old_state,
    enum ieee80211_sta_state new_state)
{

	(void)hw;
	(void)sta;
	log_add(EV_STA_STATE, vif, 0, (int)old_state, (int)new_state, NULL);
	return 0;
}

static int
drv_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    struct ieee80211_sta *sta)
{

	(void)hw;
	(void)sta;
	log_add(EV_STA_ADD, vif, 0, 0, 0, NULL);
	return 0;
}

static void
drv_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    struct ieee80211_sta *sta)
{

	(void)hw;
	(void)sta;
	log_add(EV_STA_REMOVE, vif, 0, 0, 0, NULL);
}

static void
drv_vif_cfg_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    u64 changed)
{

	(void)hw;
	log_add(EV_VIF_CFG, vif, changed, 0, 0, NULL);
}

static void
drv_link_info_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    struct ieee80211_bss_conf *conf, u64 changed)
{

	(void)hw;
	log_add(EV_LINK_INFO, vif, changed, 0, 0, conf);
}

static void
drv_bss_info_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    struct ieee80211_bss_conf *conf, u64 changed)
{

	(void)hw;
	log_add(EV_BSS_INFO, vif, changed, 0, 0, conf);
}

/* the two families, declared one per table exactly as the drivers do */
static const struct ieee80211_ops ops_73 = {
	.add_interface		= drv_add_interface,
	.remove_interface	= drv_remove_interface,
	.sta_state		= drv_sta_state,
	.vif_cfg_changed	= drv_vif_cfg_changed,
	.link_info_changed	= drv_link_info_changed,
};

static const struct ieee80211_ops ops_6x = {
	.add_interface		= drv_add_interface,
	.remove_interface	= drv_remove_interface,
	.sta_add		= drv_sta_add,
	.sta_remove		= drv_sta_remove,
	.bss_info_changed	= drv_bss_info_changed,
};

static const struct ieee80211_ops ops_both = {
	.add_interface		= drv_add_interface,
	.remove_interface	= drv_remove_interface,
	.sta_state		= drv_sta_state,
	.sta_add		= drv_sta_add,
	.sta_remove		= drv_sta_remove,
	.vif_cfg_changed	= drv_vif_cfg_changed,
	.link_info_changed	= drv_link_info_changed,
	.bss_info_changed	= drv_bss_info_changed,
};

/* ------------------------------------------------------------------ */
/* the fixture                                                         */
/* ------------------------------------------------------------------ */

static const u8 g_ap[6] = { 0x02, 0x00, 0x5e, 0x10, 0x20, 0x30 };
static const u8 g_sta_mac[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };

/*
 * The three bits an association carries in this port: the interface's own
 * association state, the peer's address, and the rate set the driver programs
 * its response-rate register from.
 */
#define ASSOC_CHANGED	(BSS_CHANGED_ASSOC | BSS_CHANGED_BSSID | \
			 BSS_CHANGED_BASIC_RATES)

static struct ieee80211_hw *
fixture_new(enum shape shape)
{
	const struct ieee80211_ops *ops;
	struct ieee80211_hw *hw;

	switch (shape) {
	case SHAPE_73:
		ops = &ops_73;
		break;
	case SHAPE_6X:
		ops = &ops_6x;
		break;
	default:
		ops = &ops_both;
		break;
	}
	hw = ieee80211_alloc_hw(sizeof(struct shape_priv), ops);
	if (hw == NULL)
		return NULL;
	/* the driver's own sizes, as probe would set them before registering */
	hw->vif_data_size = 64;
	hw->sta_data_size = 64;
	hw->txq_data_size = 16;
	return hw;
}

/*
 * One association, driven through the seam exactly as the bridge drives it: the
 * interface appears, the station appears and associates, and the BSS change
 * carries the association's bits.
 */
static struct ieee80211_vif *
join(struct ieee80211_hw *hw, struct ieee80211_sta **sta_out)
{
	struct ieee80211_sta *sta;
	struct ieee80211_vif *vif;

	vif = lkpi80211_drv_vif_add(hw, NL80211_IFTYPE_STATION, g_sta_mac);
	if (vif == NULL)
		return NULL;
	vif->cfg.assoc = true;
	vif->cfg.aid = 1;
	memcpy(vif->cfg.ap_addr, g_ap, 6);
	vif->bss_conf.bssid = g_ap;
	sta = lkpi80211_drv_sta_add(hw, vif, g_ap, 1);
	if (sta == NULL)
		return NULL;
	lkpi80211_drv_bss_changed(hw, vif, ASSOC_CHANGED);
	*sta_out = sta;
	return vif;
}

/* ------------------------------------------------------------------ */
/* the cases                                                           */
/* ------------------------------------------------------------------ */

static void
case_73_association(void)
{
	struct ieee80211_hw *hw = fixture_new(SHAPE_73);
	struct ieee80211_sta *sta = NULL;
	struct ieee80211_vif *vif;
	const struct ev *appears, *assoc, *cfg, *link;
	int ok = 1;

	if (hw == NULL) {
		case_line("73-association", 0, "no hw");
		return;
	}
	log_reset();
	vif = join(hw, &sta);
	if (vif == NULL || sta == NULL) {
		case_line("73-association", 0, "the join did not complete");
		ieee80211_free_hw(hw);
		return;
	}
	/*
	 * The station's ladder, in order: it appears, then it associates.  The
	 * interface runs NOTEXIST -> NONE when the station is created and
	 * AUTH -> ASSOC when it associates (driver-ops.c:drv_sta_state); this
	 * port has no separate discovery event, so both are driven by the one
	 * station-add the bridge makes.
	 */
	appears = log_nth(EV_STA_STATE, 0);
	assoc = log_nth(EV_STA_STATE, 1);
	if (log_count(EV_STA_STATE) != 2 || appears == NULL || assoc == NULL)
		ok = 0;
	else if (appears->old_state != IEEE80211_STA_NOTEXIST ||
	    appears->new_state != IEEE80211_STA_NONE ||
	    assoc->old_state != IEEE80211_STA_AUTH ||
	    assoc->new_state != IEEE80211_STA_ASSOC)
		ok = 0;
	/* the station before the association notice, not after it */
	if (ok && log_pos(EV_STA_STATE) > log_pos(EV_VIF_CFG))
		ok = 0;
	/* the two families, each with its own half of the mask */
	cfg = log_first(EV_VIF_CFG);
	link = log_first(EV_LINK_INFO);
	if (log_count(EV_VIF_CFG) != 1 || log_count(EV_LINK_INFO) != 1 ||
	    cfg == NULL || link == NULL)
		ok = 0;
	else if (cfg->changed != BSS_CHANGED_ASSOC ||
	    link->changed != (u64)(BSS_CHANGED_BSSID | BSS_CHANGED_BASIC_RATES) ||
	    link->conf != &vif->bss_conf)
		ok = 0;
	/* the older family and the older station hooks stay silent */
	if (log_count(EV_BSS_INFO) != 0 || log_count(EV_STA_ADD) != 0 ||
	    log_count(EV_STA_REMOVE) != 0)
		ok = 0;
	/* and the single-link bookkeeping the link family reads */
	if (vif->bss_conf.link_id != 0 || vif->link_conf[0] != &vif->bss_conf ||
	    vif->valid_links != 0)
		ok = 0;
	case_line("73-association", ok, "sta_state=%u cfg=%u link=%u",
	    log_count(EV_STA_STATE), log_count(EV_VIF_CFG),
	    log_count(EV_LINK_INFO));
	if (!ok)
		log_dump();
	ieee80211_free_hw(hw);
}

static void
case_73_leave(void)
{
	struct ieee80211_hw *hw = fixture_new(SHAPE_73);
	struct ieee80211_sta *sta = NULL;
	struct ieee80211_vif *vif;
	const struct ev *down;
	int ok = 1;

	if (hw == NULL) {
		case_line("73-leave", 0, "no hw");
		return;
	}
	vif = join(hw, &sta);
	if (vif == NULL || sta == NULL) {
		case_line("73-leave", 0, "the join did not complete");
		ieee80211_free_hw(hw);
		return;
	}
	log_reset();
	lkpi80211_drv_sta_remove(hw, vif, sta);
	down = log_nth(EV_STA_STATE, 0);
	if (log_count(EV_STA_STATE) != 2 || down == NULL)
		ok = 0;
	else if (down->old_state != IEEE80211_STA_ASSOC ||
	    down->new_state != IEEE80211_STA_AUTH)
		ok = 0;
	if (log_count(EV_STA_REMOVE) != 0 || log_count(EV_STA_ADD) != 0)
		ok = 0;
	case_line("73-leave", ok, "sta_state=%u sta_remove=%u",
	    log_count(EV_STA_STATE), log_count(EV_STA_REMOVE));
	if (!ok)
		log_dump();
	ieee80211_free_hw(hw);
}

static void
case_6x_association(void)
{
	struct ieee80211_hw *hw = fixture_new(SHAPE_6X);
	struct ieee80211_sta *sta = NULL;
	struct ieee80211_vif *vif;
	const struct ev *bss;
	int ok = 1;

	if (hw == NULL) {
		case_line("6x-association", 0, "no hw");
		return;
	}
	log_reset();
	vif = join(hw, &sta);
	if (vif == NULL || sta == NULL) {
		case_line("6x-association", 0, "the join did not complete");
		ieee80211_free_hw(hw);
		return;
	}
	/*
	 * The older family gets the whole mask in one call, and the older
	 * station hook fires at AUTH -> ASSOC - which is where the interface
	 * calls sta_add for a driver that has no sta_state (driver-ops.c).
	 */
	bss = log_first(EV_BSS_INFO);
	if (log_count(EV_BSS_INFO) != 1 || bss == NULL)
		ok = 0;
	else if (bss->changed != (u64)ASSOC_CHANGED)
		ok = 0;
	if (log_count(EV_STA_ADD) != 1 || log_count(EV_STA_STATE) != 0 ||
	    log_count(EV_VIF_CFG) != 0 || log_count(EV_LINK_INFO) != 0)
		ok = 0;
	case_line("6x-association", ok, "bss_info=%u sta_add=%u sta_state=%u",
	    log_count(EV_BSS_INFO), log_count(EV_STA_ADD),
	    log_count(EV_STA_STATE));
	if (!ok)
		log_dump();
	ieee80211_free_hw(hw);
}

static void
case_6x_leave(void)
{
	struct ieee80211_hw *hw = fixture_new(SHAPE_6X);
	struct ieee80211_sta *sta = NULL;
	struct ieee80211_vif *vif;
	int ok = 1;

	if (hw == NULL) {
		case_line("6x-leave", 0, "no hw");
		return;
	}
	vif = join(hw, &sta);
	if (vif == NULL || sta == NULL) {
		case_line("6x-leave", 0, "the join did not complete");
		ieee80211_free_hw(hw);
		return;
	}
	log_reset();
	lkpi80211_drv_sta_remove(hw, vif, sta);
	if (log_count(EV_STA_REMOVE) != 1 || log_count(EV_STA_STATE) != 0)
		ok = 0;
	case_line("6x-leave", ok, "sta_remove=%u sta_state=%u",
	    log_count(EV_STA_REMOVE), log_count(EV_STA_STATE));
	if (!ok)
		log_dump();
	ieee80211_free_hw(hw);
}

static void
case_both_families(void)
{
	struct ieee80211_hw *hw = fixture_new(SHAPE_BOTH);
	struct ieee80211_sta *sta = NULL;
	struct ieee80211_vif *vif;
	const struct ev *bss;
	int ok = 1;

	if (hw == NULL) {
		case_line("both-families", 0, "no hw");
		return;
	}
	log_reset();
	vif = join(hw, &sta);
	if (vif == NULL || sta == NULL) {
		case_line("both-families", 0, "the join did not complete");
		ieee80211_free_hw(hw);
		return;
	}
	/*
	 * The interface does not treat the families as alternatives: a driver
	 * declaring both is called through both (main.c:238-254).  A substitute
	 * that picked one family by shape detection would drop a call here.
	 */
	bss = log_first(EV_BSS_INFO);
	if (log_count(EV_VIF_CFG) != 1 || log_count(EV_LINK_INFO) != 1 ||
	    log_count(EV_BSS_INFO) != 1 || bss == NULL)
		ok = 0;
	else if (bss->changed != (u64)ASSOC_CHANGED)
		ok = 0;
	/* with sta_state present, the older station hooks are not reached */
	if (log_count(EV_STA_STATE) != 2 || log_count(EV_STA_ADD) != 0)
		ok = 0;
	case_line("both-families", ok, "cfg=%u link=%u bss=%u sta_state=%u",
	    log_count(EV_VIF_CFG), log_count(EV_LINK_INFO),
	    log_count(EV_BSS_INFO), log_count(EV_STA_STATE));
	if (!ok)
		log_dump();
	ieee80211_free_hw(hw);
}

static void
case_link_bookkeeping(void)
{
	struct ieee80211_hw *hw = fixture_new(SHAPE_6X);
	struct ieee80211_vif *vif;
	int ok;

	if (hw == NULL) {
		case_line("link-bookkeeping", 0, "no hw");
		return;
	}
	vif = lkpi80211_drv_vif_add(hw, NL80211_IFTYPE_STATION, g_sta_mac);
	if (vif == NULL) {
		case_line("link-bookkeeping", 0, "no vif");
		ieee80211_free_hw(hw);
		return;
	}
	/*
	 * A vif carries one default link whose conf is its own bss_conf and
	 * whose link id is 0 (link.c:ieee80211_link_init with link_id -1), and
	 * valid_links stays 0 - a non-MLO vif, which is what
	 * ieee80211_vif_is_mld() answers from.
	 */
	ok = vif->bss_conf.link_id == 0 &&
	    vif->link_conf[0] == &vif->bss_conf &&
	    vif->valid_links == 0 && vif->active_links == 0;
	case_line("link-bookkeeping", ok, "link_id=%d conf0=%u valid=%u",
	    vif->bss_conf.link_id, vif->link_conf[0] == &vif->bss_conf,
	    vif->valid_links);
	lkpi80211_drv_vif_remove(hw, vif);
	ieee80211_free_hw(hw);
}

static void
case_default_link_address(void)
{
	struct ieee80211_hw *hw = fixture_new(SHAPE_73);
	struct ieee80211_vif *vif;
	int ok;

	if (hw == NULL) {
		case_line("default-link-address", 0, "no hw");
		return;
	}
	g_add_link_addr_seen = 0;
	memset(g_add_link_addr, 0, sizeof(g_add_link_addr));
	vif = lkpi80211_drv_vif_add(hw, NL80211_IFTYPE_STATION, g_sta_mac);
	if (vif == NULL) {
		case_line("default-link-address", 0, "no vif");
		ieee80211_free_hw(hw);
		return;
	}
	/*
	 * The default link carries the interface's own address, and the driver
	 * already had it while taking the interface: a driver reads the link
	 * address there, not vif->addr, and programs the chip's own address
	 * filter from it (rtw89: mac80211.c:137-138 -> cam.c:874-879).  Left
	 * zero, that filter matches nothing and every unicast frame to this
	 * interface is dropped in hardware.
	 */
	ok = memcmp(vif->bss_conf.addr, g_sta_mac, 6) == 0 &&
	    memcmp(vif->link_conf[0]->addr, g_sta_mac, 6) == 0 &&
	    vif->bss_conf.vif == vif &&
	    g_add_link_addr_seen && memcmp(g_add_link_addr, g_sta_mac, 6) == 0;
	case_line("default-link-address", ok,
	    "link_addr=%02x:%02x:%02x:%02x:%02x:%02x seen=%d",
	    vif->bss_conf.addr[0], vif->bss_conf.addr[1], vif->bss_conf.addr[2],
	    vif->bss_conf.addr[3], vif->bss_conf.addr[4], vif->bss_conf.addr[5],
	    g_add_link_addr_seen);
	lkpi80211_drv_vif_remove(hw, vif);
	ieee80211_free_hw(hw);
}

static void
case_zero_change_is_silent(void)
{
	struct ieee80211_hw *hw = fixture_new(SHAPE_73);
	struct ieee80211_vif *vif;
	int ok;

	if (hw == NULL) {
		case_line("zero-change-is-silent", 0, "no hw");
		return;
	}
	vif = lkpi80211_drv_vif_add(hw, NL80211_IFTYPE_STATION, g_sta_mac);
	if (vif == NULL) {
		case_line("zero-change-is-silent", 0, "no vif");
		ieee80211_free_hw(hw);
		return;
	}
	log_reset();
	lkpi80211_drv_bss_changed(hw, vif, 0);
	ok = g_log_n == 0;
	case_line("zero-change-is-silent", ok, "calls=%u", g_log_n);
	lkpi80211_drv_vif_remove(hw, vif);
	ieee80211_free_hw(hw);
}

/* ------------------------------------------------------------------ */
/* the run                                                             */
/* ------------------------------------------------------------------ */

static void
app_task(void *arg)
{

	(void)arg;
	case_73_association();
	case_73_leave();
	case_6x_association();
	case_6x_leave();
	case_both_families();
	case_link_bookkeeping();
	case_default_link_address();
	case_zero_change_is_silent();
	printf("SHAPE: summary cases=%u failed=%u\n", g_cases, g_fail);
	fflush(stdout);
	cmsis2_host_end_scheduler();
}

int
main(void)
{
	osThreadAttr_t attr = { 0 };
	osThreadId_t t;

	if (osKernelInitialize() != osOK) {
		printf("SHAPE: kernel init failed\n");
		return 2;
	}
	lkpi_core_init();
	attr.name = "lkpi-80211-shape";
	attr.stack_size = 16384u;
	attr.priority = osPriorityNormal;
	t = osThreadNew(app_task, NULL, &attr);
	if (t == NULL) {
		printf("SHAPE: cannot create the case task\n");
		return 2;
	}
	if (osKernelStart() != osOK)
		return 2;
	return g_fail == 0 ? 0 : 1;
}

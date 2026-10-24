/* SPDX-License-Identifier: GPL-2.0 */
/*
 * @file wlan_wext.c
 * @brief The WEXT (wireless extensions) face of the net80211 kernelmod line.
 *
 * The userland wpa_supplicant's -Dwext driver is the client: it scans through
 * SIOCSIWSCAN/SIOCGIWSCAN, associates through SIOCSIWGENIE/SIOCSIWESSID/
 * SIOCSIWAP, and installs the 4-way keys through SIOCSIWENCODEEXT.  Every one
 * of those lands on the bridge's supplicant hooks - the same scan table, join
 * (RSN IE carried in ic_opt_ie) and key install the in-image vendor backend
 * would use - so the EAPOL frames themselves ride the ordinary netdev data
 * path (0x888e ethernet frames to the supplicant's packet socket).
 *
 * Event layout: the stream is the struct layout of a 64-bit build, which is
 * what cfg80211 also produces here: an 8-byte len/cmd header (the iw_event
 * union is 8-byte aligned, so four bytes of padding follow the pair) and, for
 * the point events (essid/encode/genie), length/flags at offset 12 and the
 * payload at 16.  The range therefore reports we_version_compiled 21: back
 * when it reported 18 the fixed events were written with the 4-byte packed
 * header while the point events stayed in the struct layout, iwlist coped and
 * wpa_supplicant's length walk did not - it parsed a whole 4470-byte table as
 * one BSS and then never found the WPA2 network.  The pure packed layout was
 * rejected earlier for the same class of reason ("ESSID:off/any/hidden" on
 * every cell).
 *
 * Threading: the ioctl handlers run on the caller's thread and take the state
 * mutex; the port's event callback may run in the driver/USB worker and only
 * takes the event spinlock and schedules the event work (wireless_send_event
 * sleeps, so it must not run there).
 *
 * The tick pump: the imported stack has no callout thread, so a sweep only
 * advances while someone hands it ieee80211_next_scan() at the dwell period,
 * and its management timers only run while someone calls the watchdog at the
 * 1 Hz cadence they assume.  The sysfs control face drives both from its own
 * blocking loops; a supplicant has no such loop, so without this face owning a
 * pump a SIOCSIWSCAN would enter S_SCAN and stay there - no channel walk, no
 * scan-done event, and therefore no association (the scan-completed event is
 * what wakes the supplicant's scan result fetch).
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/wireless.h>
/*
 * Event header size.  The packed (WE<=18) layout puts the payload right after
 * the 4-byte len/cmd pair; on a 64-bit build the iw_event union is 8-byte
 * aligned, so the struct layout cfg80211 uses has four bytes of padding there
 * (IW_EV_LCP_LEN == 8).  This face encodes the point events in that struct
 * layout (16 bytes before the data), so the fixed events have to use the same
 * header - a mixed stream parses under iwlib but derails wpa_supplicant's
 * length walk after the first point event (measured: "1 BSSes" out of a 4470
 * byte table, then "No suitable network found").
 */
#define WEXT_EV_HDR	8

#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/errno.h>

#include <net/iw_handler.h>

#include <stdint.h>
#include <stddef.h>

#include "wlan_netdev.h"
#include "wlan_wext.h"

#include <wlan_bridge.h>

#include "port/port.h"

/* IEEE80211_CAPINFO_PRIVACY as a scalar: the net80211 header must not cross
 * into this translation unit */
#define WEXT_CAP_PRIVACY	0x0010

/* the point-event data starts at the end of the event (WE-18 layout) */
#define WEXT_POINT_LEN		(sizeof(struct iw_event))

static struct wlan_wext *wext_of(struct net_device *dev)
{
	return &((struct wlan_netdev *)netdev_priv(dev))->wext;
}

/* ------------------------------------------------------------------ */
/* the scan-result encoding                                            */
/* ------------------------------------------------------------------ */

/*
 * Append one fixed-size event. The point events reuse this with the extra
 * bytes appended after the event; the caller sets u.data.length/flags.
 */
static int wext_put(char **pos, char *end, unsigned int cmd,
    const union iwreq_data *u, const void *extra, size_t extra_len)
{
	const size_t total = WEXT_POINT_LEN + extra_len;
	struct iw_event *e;

	if ((size_t)(end - *pos) < total)
		return -E2BIG;
	e = (struct iw_event *)*pos;
	memset(e, 0, WEXT_POINT_LEN);
	e->len = total;
	e->cmd = cmd;
	if (u != NULL)
		memcpy(&e->u, u, sizeof(*u));
	if (extra_len > 0)
		memcpy(*pos + WEXT_POINT_LEN, extra, extra_len);
	*pos += total;
	return 0;
}

/*
 * Point events (ESSID, GENIE, ENCODE) do not carry the iw_point pointer in
 * the stream: the kernel's iwe_stream_add_point() copies only length/flags
 * after IW_EV_LCP_LEN and puts the payload at IW_EV_POINT_LEN.  On 64-bit
 * IW_EV_POINT_LEN (16) is smaller than sizeof(struct iw_event) (24), so the
 * fixed-event helper above writes the payload 8 bytes too far and iwlist
 * reads the padding as the ESSID ("off/any/hidden").  Fixed-size events
 * (AP/MODE/FREQ/QUAL) keep using wext_put().
 */
static int wext_put_point(char **pos, char *end, unsigned int cmd,
    const union iwreq_data *u, const void *extra, size_t extra_len)
{
	const size_t total = IW_EV_POINT_LEN + extra_len;
	struct iw_event *e;

	if ((size_t)(end - *pos) < total)
		return -E2BIG;
	e = (struct iw_event *)*pos;
	memset(e, 0, IW_EV_POINT_LEN);
	e->len = total;
	e->cmd = cmd;
	if (u != NULL) {
		memcpy((char *)e + IW_EV_LCP_LEN,
		    (const char *)u + IW_EV_POINT_OFF, 4);
	}
	if (extra_len > 0)
		memcpy((char *)e + IW_EV_POINT_LEN, extra, extra_len);
	*pos += total;
	return 0;
}

/*
 * SIOCGIWRATE is special: the supplicant reads a list of iw_param records
 * starting right after the length/command word, so the event is
 * IW_EV_LCP_LEN + sizeof(iw_param) bytes and only the max rate matters.
 */
static int wext_put_rate(char **pos, char *end, u16 rate_500k)
{
	const size_t total = WEXT_EV_HDR + sizeof(struct iw_param);
	struct iw_param p = {};

	if ((size_t)(end - *pos) < total)
		return -E2BIG;
	p.value = (__s32)rate_500k * 500000;
	p.fixed = 1;
	((struct iw_event *)*pos)->len = total;
	((struct iw_event *)*pos)->cmd = SIOCGIWRATE;
	memcpy(*pos + WEXT_EV_HDR, &p, sizeof(p));
	*pos += total;
	return 0;
}

/*
 * WE<=18 wire format for scan events (the range reports we_version_compiled
 * 18): a 4-byte len/cmd header, then the fixed payload or - for point events -
 * eight bytes without the pointer, length/flags at offset 12 and the payload
 * at offset 16.  iwlist's iw_extract_event_stream() reads exactly this layout
 * for we_version <= 18; the struct-shaped events used before put the payload
 * where the parser looks for the pointer, which is why every ESSID printed as
 * "off/any/hidden".
 */
static int wext_scan_fixed(char **pos, char *end, unsigned int cmd,
    const void *data, size_t len)
{
	const size_t total = WEXT_EV_HDR + len;
	struct iw_event *e;

	if ((size_t)(end - *pos) < total)
		return -E2BIG;
	e = (struct iw_event *)*pos;
	memset(e, 0, total);
	e->len = total;
	e->cmd = cmd;
	if (data != NULL && len != 0)
		memcpy(*pos + WEXT_EV_HDR, data, len);
	*pos += total;
	return 0;
}

static int wext_scan_point(char **pos, char *end, unsigned int cmd,
    const union iwreq_data *u, const void *extra, size_t extra_len)
{
	const size_t total = 16 + extra_len;
	struct iw_event *e;

	if ((size_t)(end - *pos) < total)
		return -E2BIG;
	e = (struct iw_event *)*pos;
	memset(e, 0, 16);
	e->len = total;
	e->cmd = cmd;
	if (u != NULL) {
		memcpy(*pos + 12, &u->data.length, 2);
		memcpy(*pos + 14, &u->data.flags, 2);
	}
	if (extra_len > 0)
		memcpy(*pos + 16, extra, extra_len);
	*pos += total;
	return 0;
}

/* max supported rate from the rates / extended-rates elements, in 500 kb/s */
static u16 wext_max_rate(const u8 *ies, size_t len)
{
	u16 max = 0;

	while (ies != NULL && len >= 2 && (size_t)ies[1] + 2 <= len) {
		if (ies[0] == 1 || ies[0] == 50) {
			size_t i;

			for (i = 2; i < (size_t)ies[1] + 2; i++) {
				u8 r = ies[i] & 0x7f;

				if (r > max)
					max = r;
			}
		}
		len -= (size_t)ies[1] + 2;
		ies += (size_t)ies[1] + 2;
	}
	return max != 0 ? max : 108;	/* default: 54 Mb/s */
}

static int wext_scan_one(char **pos, char *end,
    const struct wlan_bridge_scan_res *r)
{
	union iwreq_data u;
	int rc;

	memset(&u, 0, sizeof(u));
	u.ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(u.ap_addr.sa_data, r->bssid, 6);
	if ((rc = wext_scan_fixed(pos, end, SIOCGIWAP, &u.ap_addr,
	    sizeof(u.ap_addr))) < 0)
		return rc;

	memset(&u, 0, sizeof(u));
	u.mode = IW_MODE_INFRA;
	if ((rc = wext_scan_fixed(pos, end, SIOCGIWMODE, &u.mode,
	    sizeof(u.mode))) < 0)
		return rc;

	/*
	 * The frequency in the canonical WEXT encoding: MHz with an exponent of
	 * 6 (cfg80211_wext_giwfreq does exactly this).  The earlier form here
	 * multiplied the MHz value into the exponent, which the supplicant
	 * rejects outright ("Invalid freq in scan results ... m=0 e=24620",
	 * runs/2026-10-22-wpa2) and then leaves every BSS at freq 0.
	 */
	memset(&u, 0, sizeof(u));
	u.freq.m = (__s32)r->freq;
	u.freq.e = 6;
	if ((rc = wext_scan_fixed(pos, end, SIOCGIWFREQ, &u.freq,
	    sizeof(u.freq))) < 0)
		return rc;

	memset(&u, 0, sizeof(u));
	u.qual.level = (__u8)r->level;
	u.qual.updated = IW_QUAL_LEVEL_UPDATED | IW_QUAL_DBM;
	if ((rc = wext_scan_fixed(pos, end, IWEVQUAL, &u.qual,
	    sizeof(u.qual))) < 0)
		return rc;

	memset(&u, 0, sizeof(u));
	u.data.length = 0;
	u.data.flags = (r->caps & WEXT_CAP_PRIVACY) != 0 ? 0 :
	    IW_ENCODE_DISABLED;
	if ((rc = wext_scan_point(pos, end, SIOCGIWENCODE, &u, NULL, 0)) < 0)
		return rc;

	memset(&u, 0, sizeof(u));
	u.essid.length = (__u16)r->ssid_len;
	u.essid.flags = 1;
	if ((rc = wext_scan_point(pos, end, SIOCGIWESSID, &u, r->ssid,
	    r->ssid_len)) < 0)
		return rc;

	if ((rc = wext_put_rate(pos, end, wext_max_rate(r->ies,
	    r->ies_len))) < 0)
		return rc;

	if (r->ies != NULL && r->ies_len > 0) {
		memset(&u, 0, sizeof(u));
		u.data.length = (__u16)r->ies_len;
		u.data.flags = 0;
		if ((rc = wext_scan_point(pos, end, IWEVGENIE, &u, r->ies,
		    r->ies_len)) < 0)
			return rc;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* the port event callback and the deferred work                       */
/* ------------------------------------------------------------------ */

static void wlan_wext_ev_work(struct work_struct *work)
{
	struct wlan_wext *w = container_of(work, struct wlan_wext, ev_work);
	union iwreq_data wrqu;
	unsigned long flags;
	bool assoc, disc, scan;
	u8 bssid[6];

	spin_lock_irqsave(&w->ev_lock, flags);
	assoc = w->assoc;
	disc = w->ev_disc;
	scan = w->scan_done;
	memcpy(bssid, w->assoc_bssid, 6);
	w->ev_disc = false;
	w->scan_done = false;
	spin_unlock_irqrestore(&w->ev_lock, flags);

	if (assoc || disc) {
		memset(&wrqu, 0, sizeof(wrqu));
		wrqu.ap_addr.sa_family = ARPHRD_ETHER;
		if (assoc)
			memcpy(wrqu.ap_addr.sa_data, bssid, 6);
		wireless_send_event(w->dev, SIOCGIWAP, &wrqu, NULL);
	}
	if (scan) {
		memset(&wrqu, 0, sizeof(wrqu));
		wireless_send_event(w->dev, SIOCGIWSCAN, &wrqu, NULL);
	}
}

static void wlan_wext_event(enum wlan_port_event event, const uint8_t *addr,
    void *arg)
{
	struct wlan_wext *w = arg;
	unsigned long flags;

	if (w == NULL)
		return;
	spin_lock_irqsave(&w->ev_lock, flags);
	switch (event) {
	case WLAN_PORT_ASSOC:
		w->assoc = true;
		if (addr != NULL)
			memcpy(w->assoc_bssid, addr, 6);
		break;
	case WLAN_PORT_DISASSOC:
		w->assoc = false;
		memset(w->assoc_bssid, 0, 6);
		w->ev_disc = true;
		break;
	case WLAN_PORT_SCAN_DONE:
		w->scan_done = true;
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(&w->ev_lock, flags);
	queue_work(system_dfl_wq, &w->ev_work);
}

static void wlan_wext_join_work(struct work_struct *work)
{
	struct wlan_wext *w = container_of(work, struct wlan_wext,
	    join_work.work);
	int rc = 0;

	mutex_lock(&w->lock);
	if (w->ssid_len > 0) {
		rc = wlan_bridge_join(w->ssid, w->ssid_len,
		    w->bssid_set ? w->bssid : NULL,
		    w->genie_len > 0 ? w->genie : NULL, w->genie_len);
		pr_info("wlan: wext join ssid_len=%zu bssid=%pM genie=%zu rc=%d\n",
		    w->ssid_len, w->bssid, w->genie_len, rc);
	}
	mutex_unlock(&w->lock);
}

/* ------------------------------------------------------------------ */
/* the tick pump                                                       */
/* ------------------------------------------------------------------ */

/*
 * One tick is 100 ms (the mgmt tick's granularity: it only acts inside the
 * auth window, and its own retry period is the bridge's). A dwell every
 * second tick gives 200 ms per channel - the recipe the sysfs sweep was
 * measured with - and the watchdog runs on every tenth tick, because it
 * decrements the stack's management timer once per call and that timer
 * assumes a one-second cadence.
 *
 * The dwell gate is scan_pending(), not scan_active(): a join can pull the
 * stack out of S_SCAN between two ticks, and only the dwell's own entry
 * check closes the sweep's bookkeeping - a pump that merely idles then
 * leaves it armed and every later scan_start refuses with -EBUSY.
 *
 * Ownership: the pump starts on the first supplicant request and never
 * stops until the face detaches, so an image driven only through the sysfs
 * parameters keeps that path's timing untouched. Running a sysfs scan/join
 * while the pump is up would halve the stack's 5 s management window (two
 * tickers, one timer), which is why the regression arm reloads the module.
 */
#define WEXT_PUMP_MS		100
#define WEXT_PUMP_DWELL_EVERY	2
#define WEXT_PUMP_WD_EVERY	10

static void wlan_wext_pump_work(struct work_struct *work)
{
	struct wlan_wext *w = container_of(to_delayed_work(work),
	    struct wlan_wext, pump);

	w->pump_ticks++;
	wlan_bridge_mgmt_tick();
	if ((w->pump_ticks % WEXT_PUMP_DWELL_EVERY) == 0 &&
	    wlan_bridge_scan_pending())
		(void)wlan_bridge_scan_dwell();
	if ((w->pump_ticks % WEXT_PUMP_WD_EVERY) == 0)
		wlan_bridge_watchdog();
	if (!w->pump_on)
		return;
	mod_delayed_work(system_dfl_wq, &w->pump,
	    msecs_to_jiffies(WEXT_PUMP_MS));
}

static void wlan_wext_pump_start(struct wlan_wext *w)
{
	if (w->pump_on)
		return;
	/* the ioctl handlers all run under rtnl_lock, so this is not racy */
	w->pump_on = true;
	w->pump_ticks = 0;
	pr_info("wlan: wext pump up (mgmt %u ms dwell %u ms wdog %u ms)\n",
	    WEXT_PUMP_MS, WEXT_PUMP_MS * WEXT_PUMP_DWELL_EVERY,
	    WEXT_PUMP_MS * WEXT_PUMP_WD_EVERY);
	queue_delayed_work(system_dfl_wq, &w->pump, 0);
}

/* ------------------------------------------------------------------ */
/* scan and connect                                                    */
/* ------------------------------------------------------------------ */

static int wlan_wext_siwscan(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct wlan_wext *w = wext_of(dev);
	int rc;

	wlan_wext_pump_start(w);
	if (wrqu->data.length == sizeof(struct iw_scan_req) && extra != NULL) {
		const struct iw_scan_req *req =
		    (const struct iw_scan_req *)extra;

		/*
		 * A directed scan: the stack's match then requires a candidate
		 * to carry this SSID. A wildcard request (the supplicant's
		 * first sweep) leaves the filter alone - there is no bridge
		 * call that clears it, so setting it here would only ever
		 * narrow later sweeps.
		 */
		if (req->essid_len > 0 && req->essid_len <= 32)
			(void)wlan_bridge_set_ssid(req->essid,
			    req->essid_len);
	}
	rc = wlan_bridge_scan_start();
	if (rc == -EBUSY)
		return -EAGAIN;
	return rc;
}

static int wlan_wext_giwscan(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	char *pos = extra;
	char *end;
	int idx, rc;

	if (extra == NULL || wrqu->data.length < sizeof(struct iw_event))
		return -EINVAL;
	/*
	 * A hardware scan returns from SIOCSIWSCAN as soon as the request is
	 * in the firmware; the table fills later (the completion arrives as a
	 * notification).  cfg80211 answers -EAGAIN while a sweep is running and
	 * iwlist/supplicant retry on it (iwlist.c:829), so report the same
	 * thing here - otherwise the caller reads a half-filled table and joins
	 * against no candidate.
	 */
	if (wlan_bridge_scan_pending())
		return -EAGAIN;
	end = extra + wrqu->data.length;
	for (idx = 0; ; idx++) {
		struct wlan_bridge_scan_res r;

		if (wlan_bridge_scan_fetch(idx, &r) != 0)
			break;
		rc = wext_scan_one(&pos, end, &r);
		if (rc < 0)
			return rc;
	}
	wrqu->data.length = (__u16)(pos - extra);
	return 0;
}

/*
 * The exact length arrives already corrected: hostap's wext driver sends a
 * WE<21 "length includes the trailing NUL" buffer (driver_wext.c), and
 * ioctl_standard_iw_point's essid_compat strips that one byte before the
 * handler runs - so `len` is the SSID length, never 33.
 *
 * A 32-byte value with no candidate behind it is hostap's own teardown hack
 * (wpa_driver_wext_disconnect sets 32 random bytes to force the driver to drop
 * a link); it lands here as an ordinary set and the join that follows fails
 * with -ENOENT. That is the intended outcome - the deauthentication itself
 * arrives earlier as SIOCSIWMLME - so the failure is left to be logged rather
 * than special-cased.
 */
static int wlan_wext_siwessid(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct wlan_wext *w = wext_of(dev);
	size_t len = wrqu->essid.length;

	if (len > 32)
		return -EINVAL;
	wlan_wext_pump_start(w);
	mutex_lock(&w->lock);
	if (len == 0 || extra == NULL) {
		w->ssid_len = 0;
		mutex_unlock(&w->lock);
		/* IEEE80211_REASON_ASSOC_LEAVE: the supplicant cleared the SSID */
		if (len == 0)
			wlan_bridge_disconnect(3);
		return 0;
	}
	memcpy(w->ssid, extra, len);
	w->ssid[len] = 0;
	w->ssid_len = len;
	mod_delayed_work(system_dfl_wq, &w->join_work, msecs_to_jiffies(50));
	mutex_unlock(&w->lock);
	return 0;
}

static int wlan_wext_giwessid(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct wlan_wext *w = wext_of(dev);
	size_t len;

	mutex_lock(&w->lock);
	len = w->ssid_len;
	if (len > wrqu->essid.length)
		len = wrqu->essid.length;
	if (extra != NULL && len > 0)
		memcpy(extra, w->ssid, len);
	wrqu->essid.length = (__u16)len;
	wrqu->essid.flags = len > 0 ? 1 : 0;
	mutex_unlock(&w->lock);
	return 0;
}

static int wlan_wext_siwgenie(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct wlan_wext *w = wext_of(dev);
	size_t len = wrqu->data.length;

	if (len > sizeof(w->genie))
		return -EINVAL;
	mutex_lock(&w->lock);
	if (len > 0 && extra != NULL) {
		memcpy(w->genie, extra, len);
		w->genie_len = len;
	} else {
		w->genie_len = 0;
	}
	/*
	 * Store only. The supplicant sends the IE before the SSID and the BSSID
	 * (driver_wext.c's associate order), so a join armed here would fire
	 * against the previous SSID - at startup that is the teardown hack's
	 * random one, a guaranteed -ENOENT. The join belongs to siwessid/siwap.
	 */
	mutex_unlock(&w->lock);
	return 0;
}

static int wlan_wext_giwgenie(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct wlan_wext *w = wext_of(dev);
	size_t len;

	mutex_lock(&w->lock);
	len = w->genie_len;
	if (len > wrqu->data.length)
		len = wrqu->data.length;
	if (extra != NULL && len > 0)
		memcpy(extra, w->genie, len);
	wrqu->data.length = (__u16)len;
	mutex_unlock(&w->lock);
	return 0;
}

static int wlan_wext_siwap(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct wlan_wext *w = wext_of(dev);

	if (wrqu->ap_addr.sa_family != ARPHRD_ETHER)
		return -EINVAL;
	wlan_wext_pump_start(w);
	mutex_lock(&w->lock);
	memcpy(w->bssid, wrqu->ap_addr.sa_data, 6);
	w->bssid_set = !is_zero_ether_addr(w->bssid) &&
	    !is_broadcast_ether_addr(w->bssid);
	mod_delayed_work(system_dfl_wq, &w->join_work, msecs_to_jiffies(50));
	mutex_unlock(&w->lock);
	return 0;
}

static int wlan_wext_giwap(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct wlan_wext *w = wext_of(dev);
	unsigned long flags;

	memset(&wrqu->ap_addr, 0, sizeof(wrqu->ap_addr));
	wrqu->ap_addr.sa_family = ARPHRD_ETHER;
	spin_lock_irqsave(&w->ev_lock, flags);
	if (w->assoc)
		memcpy(wrqu->ap_addr.sa_data, w->assoc_bssid, 6);
	spin_unlock_irqrestore(&w->ev_lock, flags);
	return 0;
}

static int wlan_wext_siwfreq(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct wlan_wext *w = wext_of(dev);
	s32 m = wrqu->freq.m;
	s16 e = wrqu->freq.e;
	s32 freq = 0;

	/*
	 * Three shapes reach a driver here: "MHz with exponent 6" (what this
	 * face reports and what a supplicant echoes back), a 2.4 GHz channel
	 * number with no exponent (iwconfig), and a 4.9 GHz channel number.
	 * Anything else carries no frequency.
	 */
	if (e == 6) {
		freq = m;			/* MHz */
	} else if (e == 0 && m >= 1 && m <= 13) {
		freq = 2407 + 5 * m;		/* 2.4 GHz channel */
	} else if (e == 0 && m == 14) {
		freq = 2484;
	} else if (e == 0 && m >= 183 && m <= 196) {
		freq = 4000 + 5 * m;		/* 4.9 GHz channel */
	}
	mutex_lock(&w->lock);
	w->req_freq = freq > 0 ? (u32)freq : 0;
	mutex_unlock(&w->lock);
	return 0;
}

static int wlan_wext_giwfreq(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct wlan_wext *w = wext_of(dev);
	struct wlan_bridge_counters c;
	u32 freq;

	memset(&c, 0, sizeof(c));
	wlan_bridge_stats(&c);
	mutex_lock(&w->lock);
	freq = c.tuned_freq != 0 ? c.tuned_freq : w->req_freq;
	mutex_unlock(&w->lock);
	memset(&wrqu->freq, 0, sizeof(wrqu->freq));
	if (freq != 0) {
		wrqu->freq.m = (__s32)freq;	/* MHz, exponent 6: cfg80211's form */
		wrqu->freq.e = 6;
	}
	return 0;
}

static int wlan_wext_siwmlme(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct iw_mlme *mlme = (struct iw_mlme *)extra;

	if (mlme == NULL)
		return -EINVAL;
	wlan_bridge_disconnect(mlme->reason_code);
	return 0;
}

/*
 * WEXT carries the key index 1-based in the encoding flags and hostap sends
 * "key_idx + 1" (driver_wext.c), so the flag value minus one is the stack's
 * zero-based slot. Values past the fourth slot are not a malformed request:
 * at startup the supplicant walks every slot it knows and clears it (0..5 on
 * the bench, see runs/2026-10-22-wpa2), and clearing a slot the stack does not
 * have is a no-op, not an error. Failing there makes the supplicant print
 * "Driver did not support SIOCSIWENCODEEXT" and count the call as failed.
 *
 * IW_ENCODE_ALG_PMK is the cfg80211-only PMK hand-over. This stack derives the
 * PTK itself, so the value is accepted and ignored; the current supplicant does
 * not send it (its capability flags do not advertise the 4-way offload), but a
 * failure here would fail the whole association request.
 */
static int wlan_wext_siwencodeext(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
	int key_idx, pairwise, set_tx;

	if (ext == NULL || wrqu->encoding.length < sizeof(*ext))
		return -EINVAL;
	key_idx = wrqu->encoding.flags & IW_ENCODE_INDEX;
	if (key_idx > 0)
		key_idx--;
	pairwise = !(ext->ext_flags & IW_ENCODE_EXT_GROUP_KEY);
	set_tx = (ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) != 0;

	if (ext->alg == IW_ENCODE_ALG_PMK)
		return 0;
	if (ext->alg == IW_ENCODE_ALG_NONE) {
		if (!pairwise && key_idx > 3)
			return 0;	/* no such slot: already clear */
		wlan_bridge_key_clear(pairwise, key_idx);
		return 0;
	}
	if (key_idx > 3)
		return -EINVAL;
	if (ext->alg != IW_ENCODE_ALG_CCMP)
		return -EINVAL;
	if (ext->key_len == 0 || ext->key_len > 16)
		return -EINVAL;
	return wlan_bridge_key_install(pairwise, key_idx, set_tx,
	    WLAN_SEAM_CIPHER_CCMP, ext->key, ext->key_len);
}

/* ------------------------------------------------------------------ */
/* the simple gets / no-op sets                                        */
/* ------------------------------------------------------------------ */

static int wlan_wext_giwname(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	strscpy(wrqu->name, "IEEE 802.11", sizeof(wrqu->name));
	return 0;
}

static int wlan_wext_giwrange(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	static const s32 rates[] = {
		1000000, 2000000, 5500000, 11000000, 6000000, 9000000,
		12000000, 18000000, 24000000, 36000000, 48000000, 54000000,
	};
	struct iw_range *r = (struct iw_range *)extra;
	size_t i;

	if (extra == NULL)
		return -EINVAL;
	memset(r, 0, sizeof(*r));
	/*
	 * 21, not 18: the event stream below is the struct layout (8-byte
	 * header for fixed events, 16 for point events), which is what
	 * cfg80211 reports and produces on this kernel.  The packed WE-18
	 * layout would need an 8-byte point header and a 4-byte fixed header;
	 * mixing the two is what broke the supplicant's scan parse.
	 */
	r->we_version_compiled = 21;
	r->we_version_source = 21;
	r->scan_capa = IW_SCAN_CAPA_ESSID;
	r->max_qual.qual = 100;
	r->max_qual.level = 255;
	r->avg_qual = r->max_qual;
	r->num_bitrates = ARRAY_SIZE(rates);
	for (i = 0; i < ARRAY_SIZE(rates); i++)
		r->bitrate[i] = rates[i];
	/*
	 * iw_freq_t is (m * 10^e) Hz.  The channel list must use the same
	 * encoding as the scan's SIOCGIWFREQ event (MHz with e=6): iwlist's
	 * iw_freq_to_channel() compares the two floats, so a channel index in
	 * m (the previous shape) never matches and every cell printed
	 * "Channel:0".
	 */
	r->num_frequency = 14;
	for (i = 0; i < 13; i++) {
		r->freq[i].m = (__s32)(2412 + 5 * i);
		r->freq[i].e = 6;
	}
	r->freq[13].m = 2484;
	r->freq[13].e = 6;
	r->enc_capa = IW_ENC_CAPA_WPA | IW_ENC_CAPA_WPA2 |
	    IW_ENC_CAPA_CIPHER_TKIP | IW_ENC_CAPA_CIPHER_CCMP;
	wrqu->data.length = sizeof(*r);
	return 0;
}

static int wlan_wext_giwmode(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	wrqu->mode = IW_MODE_INFRA;
	return 0;
}

static int wlan_wext_siwmode(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	return wrqu->mode == IW_MODE_INFRA ? 0 : -EOPNOTSUPP;
}

static int wlan_wext_giwrate(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	memset(&wrqu->bitrate, 0, sizeof(wrqu->bitrate));
	wrqu->bitrate.fixed = 0;
	return 0;
}

static int wlan_wext_siwrate(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	return 0;
}

static int wlan_wext_giwtxpow(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	memset(&wrqu->txpower, 0, sizeof(wrqu->txpower));
	wrqu->txpower.disabled = 1;
	return 0;
}

static int wlan_wext_siwtxpow(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	return 0;
}

static int wlan_wext_giwpower(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	memset(&wrqu->power, 0, sizeof(wrqu->power));
	return 0;
}

static int wlan_wext_siwpower(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	return 0;
}

static int wlan_wext_giwretry(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	memset(&wrqu->retry, 0, sizeof(wrqu->retry));
	return 0;
}

static int wlan_wext_siwretry(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	return 0;
}

static int wlan_wext_giwrts(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	memset(&wrqu->rts, 0, sizeof(wrqu->rts));
	return 0;
}

static int wlan_wext_siwrts(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	return 0;
}

static int wlan_wext_giwfrag(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	memset(&wrqu->frag, 0, sizeof(wrqu->frag));
	return 0;
}

static int wlan_wext_siwfrag(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	return 0;
}

static int wlan_wext_giwnwid(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	memset(&wrqu->nwid, 0, sizeof(wrqu->nwid));
	return 0;
}

static int wlan_wext_siwnwid(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	return -EOPNOTSUPP;
}

static int wlan_wext_giwsens(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	memset(&wrqu->sens, 0, sizeof(wrqu->sens));
	return 0;
}

static int wlan_wext_siwsens(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	return 0;
}

static int wlan_wext_giwencode(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	memset(&wrqu->encoding, 0, sizeof(wrqu->encoding));
	wrqu->encoding.flags = IW_ENCODE_DISABLED;
	return 0;
}

static int wlan_wext_siwencode(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	/* the WEP face is not used; WPA goes through SIOCSIWENCODEEXT */
	return 0;
}

static int wlan_wext_giwauth(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	memset(&wrqu->param, 0, sizeof(wrqu->param));
	return 0;
}

static int wlan_wext_siwauth(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	return 0;
}

static int wlan_wext_giwencodeext(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	return -EOPNOTSUPP;
}

static int wlan_wext_giwspy(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	return -EOPNOTSUPP;
}

static int wlan_wext_siwspy(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	return 0;
}

static int wlan_wext_giwthrspy(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	return -EOPNOTSUPP;
}

static int wlan_wext_siwthrspy(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	return 0;
}

static int wlan_wext_giwpriv(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	return -EOPNOTSUPP;
}

static int wlan_wext_siwpriv(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	return -EOPNOTSUPP;
}

static int wlan_wext_siwcommit(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	return 0;
}

static struct iw_statistics wlan_wext_iwstats;

static struct iw_statistics *wlan_wext_get_stats(struct net_device *dev)
{
	memset(&wlan_wext_iwstats, 0, sizeof(wlan_wext_iwstats));
	wlan_wext_iwstats.discard.code = dev->stats.rx_errors;
	wlan_wext_iwstats.discard.misc = dev->stats.tx_errors;
	return &wlan_wext_iwstats;
}

static int wlan_wext_giwstats(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct iw_statistics *st = wlan_wext_get_stats(dev);

	if (extra == NULL)
		return -EINVAL;
	memcpy(extra, st, sizeof(*st));
	wrqu->data.length = sizeof(*st);
	return 0;
}

/* ------------------------------------------------------------------ */
/* the handler table                                                   */
/* ------------------------------------------------------------------ */

static const iw_handler wlan_wext_std[SIOCIWLAST - SIOCIWFIRST + 1] = {
	[SIOCSIWCOMMIT - SIOCIWFIRST] = wlan_wext_siwcommit,
	[SIOCGIWNAME - SIOCIWFIRST] = wlan_wext_giwname,
	[SIOCSIWNWID - SIOCIWFIRST] = wlan_wext_siwnwid,
	[SIOCGIWNWID - SIOCIWFIRST] = wlan_wext_giwnwid,
	[SIOCSIWFREQ - SIOCIWFIRST] = wlan_wext_siwfreq,
	[SIOCGIWFREQ - SIOCIWFIRST] = wlan_wext_giwfreq,
	[SIOCSIWMODE - SIOCIWFIRST] = wlan_wext_siwmode,
	[SIOCGIWMODE - SIOCIWFIRST] = wlan_wext_giwmode,
	[SIOCSIWSENS - SIOCIWFIRST] = wlan_wext_siwsens,
	[SIOCGIWSENS - SIOCIWFIRST] = wlan_wext_giwsens,
	[SIOCGIWRANGE - SIOCIWFIRST] = wlan_wext_giwrange,
	[SIOCSIWPRIV - SIOCIWFIRST] = wlan_wext_siwpriv,
	[SIOCGIWPRIV - SIOCIWFIRST] = wlan_wext_giwpriv,
	[SIOCGIWSTATS - SIOCIWFIRST] = wlan_wext_giwstats,
	[SIOCSIWSPY - SIOCIWFIRST] = wlan_wext_siwspy,
	[SIOCGIWSPY - SIOCIWFIRST] = wlan_wext_giwspy,
	[SIOCSIWTHRSPY - SIOCIWFIRST] = wlan_wext_siwthrspy,
	[SIOCGIWTHRSPY - SIOCIWFIRST] = wlan_wext_giwthrspy,
	[SIOCSIWAP - SIOCIWFIRST] = wlan_wext_siwap,
	[SIOCGIWAP - SIOCIWFIRST] = wlan_wext_giwap,
	[SIOCSIWMLME - SIOCIWFIRST] = wlan_wext_siwmlme,
	[SIOCSIWSCAN - SIOCIWFIRST] = wlan_wext_siwscan,
	[SIOCGIWSCAN - SIOCIWFIRST] = wlan_wext_giwscan,
	[SIOCSIWESSID - SIOCIWFIRST] = wlan_wext_siwessid,
	[SIOCGIWESSID - SIOCIWFIRST] = wlan_wext_giwessid,
	[SIOCSIWRATE - SIOCIWFIRST] = wlan_wext_siwrate,
	[SIOCGIWRATE - SIOCIWFIRST] = wlan_wext_giwrate,
	[SIOCSIWRTS - SIOCIWFIRST] = wlan_wext_siwrts,
	[SIOCGIWRTS - SIOCIWFIRST] = wlan_wext_giwrts,
	[SIOCSIWFRAG - SIOCIWFIRST] = wlan_wext_siwfrag,
	[SIOCGIWFRAG - SIOCIWFIRST] = wlan_wext_giwfrag,
	[SIOCSIWTXPOW - SIOCIWFIRST] = wlan_wext_siwtxpow,
	[SIOCGIWTXPOW - SIOCIWFIRST] = wlan_wext_giwtxpow,
	[SIOCSIWRETRY - SIOCIWFIRST] = wlan_wext_siwretry,
	[SIOCGIWRETRY - SIOCIWFIRST] = wlan_wext_giwretry,
	[SIOCSIWENCODE - SIOCIWFIRST] = wlan_wext_siwencode,
	[SIOCGIWENCODE - SIOCIWFIRST] = wlan_wext_giwencode,
	[SIOCSIWPOWER - SIOCIWFIRST] = wlan_wext_siwpower,
	[SIOCGIWPOWER - SIOCIWFIRST] = wlan_wext_giwpower,
	[SIOCSIWGENIE - SIOCIWFIRST] = wlan_wext_siwgenie,
	[SIOCGIWGENIE - SIOCIWFIRST] = wlan_wext_giwgenie,
	[SIOCSIWENCODEEXT - SIOCIWFIRST] = wlan_wext_siwencodeext,
	[SIOCGIWENCODEEXT - SIOCIWFIRST] = wlan_wext_giwencodeext,
	[SIOCSIWAUTH - SIOCIWFIRST] = wlan_wext_siwauth,
	[SIOCGIWAUTH - SIOCIWFIRST] = wlan_wext_giwauth,
};

static const struct iw_handler_def wlan_wext_handler_def = {
	.standard = wlan_wext_std,
	.num_standard = SIOCIWLAST - SIOCIWFIRST + 1,
	.get_wireless_stats = wlan_wext_get_stats,
};

/* ------------------------------------------------------------------ */
/* lifecycle                                                           */
/* ------------------------------------------------------------------ */

int wlan_wext_attach(struct net_device *dev)
{
	struct wlan_netdev *wn = netdev_priv(dev);
	struct wlan_wext *w = &wn->wext;

	memset(w, 0, sizeof(*w));
	w->dev = dev;
	w->wn = wn;
	mutex_init(&w->lock);
	spin_lock_init(&w->ev_lock);
	INIT_DELAYED_WORK(&w->pump, wlan_wext_pump_work);
	INIT_DELAYED_WORK(&w->join_work, wlan_wext_join_work);
	INIT_WORK(&w->ev_work, wlan_wext_ev_work);
	/* the bench kernel has CONFIG_CFG80211_WEXT but not CONFIG_WIRELESS_EXT,
	 * so the WEXT face hangs off the wiphy, not the net_device */
	wn->wdev.wiphy->wext = &wlan_wext_handler_def;

	/* supplicant-driven MLME: the sweep reports without joining, and the
	 * stack must parse RSN IEs before any sweep the supplicant reads */
	wlan_bridge_set_manual(1);
	wlan_bridge_set_wpa(1);
	wlan_port_set_event_handler(wlan_wext_event, w);
	pr_info("wlan: wext face registered on %s\n", dev->name);
	return 0;
}

void wlan_wext_detach(struct net_device *dev)
{
	struct wlan_netdev *wn = netdev_priv(dev);
	struct wlan_wext *w = &wn->wext;

	wn->wdev.wiphy->wext = NULL;
	wlan_port_set_event_handler(NULL, NULL);
	/* stop the pump first: it re-arms itself, the others do not */
	w->pump_on = false;
	cancel_delayed_work_sync(&w->pump);
	cancel_delayed_work_sync(&w->join_work);
	cancel_work_sync(&w->ev_work);
}

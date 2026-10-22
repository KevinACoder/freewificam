/* SPDX-License-Identifier: GPL-2.0 */
/*
 * @file wlan_wext.h
 * @brief WEXT (wireless extensions) face of the net80211 kernelmod line: the
 *        state the ioctl handlers keep and the entry points the netdev calls.
 *
 * The userland wpa_supplicant's -Dwext driver drives this face; the bridge's
 * supplicant hooks (scan table, join with the RSN IE, key install) are the
 * same ones the in-image vendor backend uses. The extra data of the point
 * events sits at the end of the iw_event (the WE-18 layout), which is why
 * the range reports we_version_compiled 18.
 */

#ifndef WLAN_WEXT_H
#define WLAN_WEXT_H

#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

struct wlan_netdev;

struct wlan_wext {
	struct net_device	*dev;
	struct wlan_netdev	*wn;

	struct mutex		lock;		/* ioctl state vs the join work */
	spinlock_t		ev_lock;	/* port event vs the event work */

	u8			ssid[33];
	size_t			ssid_len;
	u8			genie[256];
	size_t			genie_len;
	u8			bssid[6];
	bool			bssid_set;
	bool			assoc;
	u8			assoc_bssid[6];
	bool			ev_disc;
	bool			scan_done;
	u32			req_freq;

	/*
	 * The supplicant arm's tick pump (see wlan_wext.c): the stack's sweep
	 * only advances on dwell calls and its timers only run on the 1 Hz
	 * watchdog, and a supplicant has no loop to drive either.  It starts on
	 * the first supplicant request, so the sysfs control plane - which
	 * ticks from its own blocking loops - never sees a second ticker.
	 */
	bool			pump_on;
	unsigned int		pump_ticks;
	struct delayed_work	pump;

	struct delayed_work	join_work;
	struct work_struct	ev_work;
};

int	wlan_wext_attach(struct net_device *dev);
void	wlan_wext_detach(struct net_device *dev);

#endif /* WLAN_WEXT_H */

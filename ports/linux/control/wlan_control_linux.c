/*
 * @file
 * @brief The kernelmod line's first control plane: scan and open-join through sysfs.
 *
 * K1/K2 had no way to ask the bridge for anything: the driver probed, the
 * bridge attached and received frames, but the net80211 state machine stayed in
 * INIT because nothing started a sweep or a join. mac80211 normally exposes
 * that through cfg80211/nl80211; the kernelmod line's first version deliberately has no
 * cfg80211 registration, so this file is the control face - the same role the
 * RTOS images' console commands play.
 *
 *   echo 1 > /sys/module/wlan_ko/parameters/scan
 *       set the desired SSID, start a software sweep, run the dwell pump
 *       (200 ms per channel plus the stack watchdog), then print every scan
 *       table row to dmesg.
 *   echo lab8189 > /sys/module/wlan_ko/parameters/join
 *       sweep if needed, then issue an open join against the AP and poll the
 *       stack state (0 INIT .. 4 RUN), running the watchdog so auth/assoc
 *       retries and timers advance.
 *   cat /sys/module/wlan_ko/parameters/state
 *       the current net80211 state, for scripted checks.
 *
 * Everything runs from a workqueue: the sysfs store callback only schedules
 * work, and all bridge calls happen in process context (the port allocates with
 * GFP_KERNEL and takes its serializer).
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/string.h>

#include <stdint.h>
#include <stddef.h>

#include <wlan_bridge.h>

#include "wlan_linux.h"

#define WLAN_CTL_SSID		"lab8189"
#define WLAN_CTL_BSSID		{ 0x24, 0xec, 0x99, 0xb9, 0x8a, 0x00 }
#define WLAN_CTL_DWELL_MS	200
#define WLAN_CTL_SCAN_MAX	40	/* 40 x 200 ms = 8 s, all channels */
#define WLAN_CTL_JOIN_MAX	120	/* 120 x 100 ms = 12 s */

/*
 * The join parameter names the AP to associate with. It used to be a stub that
 * always used the lab AP above, which made the two-radio bench impossible (the
 * open AP now lives on another card, under its own SSID); the value written to
 * the parameter decides the SSID, and the scan table's own lookup finds the
 * BSSID.
 */
/*
 * One control operation at a time: the sweep and the join both drive the
 * stack's state machine and its management timers, and a join issued while a
 * sweep is still running leaves the stack in SCAN (the join never enters AUTH,
 * the auth frame never leaves).  The two sysfs parameters are independent
 * files, so the serialization lives here, around the whole operation.
 */
static DEFINE_MUTEX(wlan_ctl_lock);

static char wlan_ctl_ssid[33] = WLAN_CTL_SSID;
static const uint8_t wlan_ctl_bssid[] = WLAN_CTL_BSSID;

static unsigned int scan_rows;

static void wlan_scan_row(const uint8_t bssid[6], const uint8_t *ssid,
    size_t ssid_len, uint32_t freq, int rssi, void *arg)
{
	char essid[33];
	size_t n = ssid_len;

	(void)arg;
	if (n > 32) {
		n = 32;
	}
	memcpy(essid, ssid, n);
	essid[n] = '\0';
	scan_rows++;
	pr_info("wlan_ko: bss %02x:%02x:%02x:%02x:%02x:%02x ssid=%s freq=%u rssi=%d\n",
	    bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
	    essid, freq, rssi);
}

/* one sweep, from the same recipe the RTOS scan case uses; lock held */
static int wlan_ctl_scan_body(void)
{
	unsigned int i;
	int rc;

	wlan_bridge_set_wpa(0);
	wlan_bridge_set_manual(1);
	rc = wlan_bridge_set_ssid((const uint8_t *)wlan_ctl_ssid,
	    strlen(wlan_ctl_ssid));
	if (rc != 0) {
		pr_info("wlan_ko: set_ssid refused rc=%d\n", rc);
		return rc;
	}
	for (i = 0; i < 30 && wlan_bridge_scan_active(); i++) {
		msleep(100);
	}
	rc = wlan_bridge_scan_start();
	if (rc != 0) {
		pr_info("wlan_ko: scan_start refused rc=%d\n", rc);
		return rc;
	}
	pr_info("wlan_ko: scan start\n");
	for (i = 0; i < WLAN_CTL_SCAN_MAX && wlan_bridge_scan_pending(); i++) {
		msleep(WLAN_CTL_DWELL_MS);
		(void)wlan_bridge_scan_dwell();
		/*
		 * The stack's watchdog assumes a one-second cadence: it
		 * decrements ic_mgt_timer by one per call. Calling it every
		 * dwell (200 ms) burns a 5-second management timer in one
		 * second and aborts the join. Five dwells = one second.
		 */
		if ((i % 5) == 4) {
			wlan_bridge_watchdog();
		}
	}
	scan_rows = 0;
	(void)wlan_bridge_scan_walk(wlan_scan_row, NULL);
	pr_info("wlan_ko: scan done rows=%u state=%d\n", scan_rows,
	    wlan_bridge_state());
	return scan_rows == 0 ? -ENOENT : 0;
}

static int wlan_ctl_scan(void)
{
	int rc;

	mutex_lock(&wlan_ctl_lock);
	rc = wlan_ctl_scan_body();
	mutex_unlock(&wlan_ctl_lock);
	return rc;
}

static int wlan_ctl_join(const char *ssid_arg)
{
	unsigned int i;
	int rc;

	mutex_lock(&wlan_ctl_lock);
	wlan_bridge_set_manual(1);
	rc = wlan_bridge_set_ssid((const uint8_t *)ssid_arg, strlen(ssid_arg));
	if (rc != 0) {
		pr_info("wlan_ko: join set_ssid rc=%d\n", rc);
		mutex_unlock(&wlan_ctl_lock);
		return rc;
	}
	/*
	 * The join needs a candidate in the table. Try the table the caller
	 * already filled first: the sweep is the slow, lossy half (a 200 ms
	 * dwell per channel can miss a beacon), and re-sweeping on every join
	 * both loses the candidate the previous sweep found and re-runs the
	 * join's timing window. Only a join with no candidate pays for the
	 * sweep, and a second lookup after it.
	 */
	rc = wlan_bridge_join((const uint8_t *)ssid_arg, strlen(ssid_arg),
	    wlan_ctl_bssid, NULL, 0);
	/*
	 * The firmware sweep is lossy on the AP's channel (the offload scan
	 * misses the ch5 beacon roughly every other sweep on the bench), so a
	 * missing candidate pays for up to three sweeps before the join gives
	 * up.  A candidate already in the table costs nothing.
	 */
	{
		unsigned int sweep;

		for (sweep = 0; sweep < 3 && rc == -ENOENT &&
		    wlan_bridge_state() != 4; sweep++) {
			(void)wlan_ctl_scan_body();
			rc = wlan_bridge_join((const uint8_t *)ssid_arg,
			    strlen(ssid_arg), wlan_ctl_bssid, NULL, 0);
		}
	}
	pr_info("wlan_ko: join rc=%d state=%d\n", rc, wlan_bridge_state());
	for (i = 0; i < WLAN_CTL_JOIN_MAX; i++) {
		msleep(100);
		/* one watchdog per second, not per poll: see wlan_ctl_scan */
		if ((i % 10) == 9) {
			wlan_bridge_watchdog();
		}
		if (wlan_bridge_state() == 4) {
			break;
		}
	}
	pr_info("wlan_ko: join done state=%d waited=%ums bad_auth=%u\n",
	    wlan_bridge_state(), i * 100, wlan_bridge_stat_bad_auth());
	rc = wlan_bridge_state() == 4 ? 0 : -ETIMEDOUT;
	mutex_unlock(&wlan_ctl_lock);
	return rc;
}

static void wlan_scan_work_fn(struct work_struct *work)
{
	(void)work;
	(void)wlan_ctl_scan();
}

static void wlan_join_work_fn(struct work_struct *work)
{
	(void)work;
	(void)wlan_ctl_join(wlan_ctl_ssid);
}

static DECLARE_WORK(wlan_scan_work, wlan_scan_work_fn);
static DECLARE_WORK(wlan_join_work, wlan_join_work_fn);

static int wlan_scan_set(const char *val, const struct kernel_param *kp)
{
	(void)val;
	(void)kp;
	schedule_work(&wlan_scan_work);
	return 0;
}

static const struct kernel_param_ops wlan_scan_ops = {
	.set = wlan_scan_set,
};

static int wlan_join_set(const char *val, const struct kernel_param *kp)
{
	size_t n;

	(void)kp;
	if (val != NULL) {
		n = strcspn(val, "\r\n \t");
		if (n > 32)
			n = 32;
		if (n != 0) {
			memcpy(wlan_ctl_ssid, val, n);
			wlan_ctl_ssid[n] = '\0';
			pr_info("wlan_ko: join target '%.*s'\n", (int)n,
			    wlan_ctl_ssid);
		}
	}
	schedule_work(&wlan_join_work);
	return 0;
}

static const struct kernel_param_ops wlan_join_ops = {
	.set = wlan_join_set,
};

static int wlan_state_get(char *buffer, const struct kernel_param *kp)
{
	(void)kp;
	return scnprintf(buffer, PAGE_SIZE, "%d\n", wlan_bridge_state());
}

static const struct kernel_param_ops wlan_state_ops = {
	.get = wlan_state_get,
};

/*
 * The bridge's counters, one line, for scripted joinst: data_tx counts the
 * frames the bridge handed to the driver's .tx, tx_done the transmit status
 * the driver reported back. A data plane that hands frames over but never
 * completes them reads differently from one that never hands them over.
 */
static int wlan_stats_get(char *buffer, const struct kernel_param *kp)
{
	struct wlan_bridge_counters c;

	(void)kp;
	memset(&c, 0, sizeof(c));
	wlan_bridge_stats(&c);
	return scnprintf(buffer, PAGE_SIZE,
	    "attached=%d mgt_tx=%u data_tx=%u rx=%u tx_done=%u scan=%u/%u\n",
	    c.attached, c.mgt_tx, c.data_tx, c.rx_frames, c.tx_completes,
	    c.scan_starts, c.scan_completes);
}

static const struct kernel_param_ops wlan_stats_ops = {
	.get = wlan_stats_get,
};

module_param_cb(scan, &wlan_scan_ops, NULL, 0200);
MODULE_PARM_DESC(scan, "write 1 to run a software scan (lab8189)");
module_param_cb(join, &wlan_join_ops, NULL, 0200);
MODULE_PARM_DESC(join, "write the SSID to run an open join");
module_param_cb(state, &wlan_state_ops, NULL, 0444);
MODULE_PARM_DESC(state, "current net80211 state (0 INIT .. 4 RUN)");
module_param_cb(stats, &wlan_stats_ops, NULL, 0444);
MODULE_PARM_DESC(stats, "bridge counters, one line");

void wlan_control_exit(void)
{
	cancel_work_sync(&wlan_scan_work);
	cancel_work_sync(&wlan_join_work);
}

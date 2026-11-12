/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    hostap_ap.h
 * @brief   The AP case's face on the imported hostapd stack: start, stop,
 *          status, and the RX queue the cfg80211_rx_mgmt_ext wrap feeds.
 *
 * The AP is driven exactly as the NXP wpa_supplicant-rtos port drives it:
 * start_wpa_supplicant() brings up the hostapd daemon task (config built in C
 * by freertos/src/supp_main.c), the config below is applied to the daemon's
 * interface, and hostapd_enable_iface() runs the AP MLME (beacon via set_ap,
 * auth/assoc via ieee802_11.c).  None of that logic lives here; this header is
 * the port-side entry and the seam for one copied management frame.
 */
#ifndef LINUX80211_HOSTAP_AP_H
#define LINUX80211_HOSTAP_AP_H

struct lkpi_hostap_cfg {
	const char	*ifname;	/* "wlan0" */
	const char	*ssid;
	unsigned int	 channel;	/* 11 */
	unsigned int	 beacon_int;	/* 100 */
	unsigned int	 dtim;		/* 2 */
};

/* 0 on success, negative errno otherwise.  The interface must already exist
 * and the stack's nl80211 AP start must be able to reach it. */
int lkpi_hostap_start(const struct lkpi_hostap_cfg *cfg);
void lkpi_hostap_stop(void);

/* 1 when hostapd reports HAPD_IFACE_ENABLED, 0 otherwise. */
int lkpi_hostap_ready(void);

/* Stations the AP MLME currently holds (hostapd's own table). */
int lkpi_hostap_sta_count(void);
int lkpi_hostap_mgmt_tx_status(const void *frame, unsigned int len, int ack);

/* transmit-path health, read by the AP phase census (components/liblinux80211/
 * tx_health.c) */
struct lkpi_tx_health {
	unsigned long	tx_status;
	unsigned long	stop_queue;
	unsigned long	wake_queue;
	unsigned long	dequeue_calls;
	unsigned long	dequeue_frames;
	unsigned long	status_mgmt;
	unsigned long	status_data;
	unsigned long	iwl_tx;
	unsigned long	hcmd;
	unsigned long	hcmd_blockq;
	unsigned long	hcmd_async;
	unsigned long	mgmt_tx;
	unsigned long	mgmt_beacon;
};
void lkpi_tx_health_get(struct lkpi_tx_health *out);
void lkpi_tx_health_mgmt(const void *frame, unsigned int len);

/* RX path (interrupt-masked, from the --wrap shim): copy one management frame
 * into the port queue.  Never sleeps; returns 1 when queued, 0 when the queue
 * is full, or a negative errno for a frame the seam refuses. */
int lkpi_hostap_rx_push(const void *frame, unsigned int len, int freq);

/* Task context: hand queued frames to hostapd's event queue (which the
 * imported eloop task then dispatches to hostapd_event()).  Returns the number
 * of frames delivered, or a negative errno. */
int lkpi_hostap_poll(void);

/* Task context, one frame: build the NXP event and post it.  Exposed for the
 * host bench; lkpi_hostap_poll() is the image's path. */
int lkpi_hostap_mgmt_rx(const void *frame, unsigned int len, int freq);

#endif /* LINUX80211_HOSTAP_AP_H */

/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   ap_mlme.h
 * @brief  The port's minimal open-AP MLME: the userspace half hostapd plays on
 *         Linux, reduced to what an open HT20 AP needs.
 *
 * Why this exists: this kernel's mac80211 has no in-kernel AP MLME.  An AP
 * vif's probe request / auth / assoc request are handed to userspace through
 * cfg80211_rx_mgmt_ext() (stack/mac80211/rx.c:3999), and with no registration
 * the frame is dropped (rx.c:4156).  The RTOS image has no netlink socket and
 * no hostapd, so the port supplies the missing half here.
 *
 * The split is deliberate:
 *   - this module is pure 802.11 logic (parse, build, state), no stack types,
 *     no sleeping, so it runs on the host bench;
 *   - ap_mlme_rx_wrap.c is the link seam that copies frames out of the RX
 *     path (the only stack-typed piece);
 *   - the caller supplies the three nl80211 actions through ops, because
 *     NL80211_CMD_FRAME / NEW_STATION / SET_STATION must run in task context.
 *
 * The MLME never blocks and never sends a frame itself: rx_push() is called
 * from the RX path (interrupt-masked section), poll() drains one frame at a
 * time from the caller's task and runs the actions there.
 *
 * Anchors are not printed here; the caller's emit() receives one preformatted
 * line per event (see iwl7260_ap.c for the prefix).
 */
#ifndef LINUX80211_AP_MLME_H
#define LINUX80211_AP_MLME_H

#include <stdint.h>

#define LKPI_AP_MLME_FRAME_MAX		512u
#define LKPI_AP_MLME_QUEUE_LEN		16u
#define LKPI_AP_MLME_STA_MAX		8u
#define LKPI_AP_MLME_SSID_MAX		32u
#define LKPI_AP_MLME_HEAD_MAX		96u
#define LKPI_AP_MLME_TAIL_MAX		512u

/* The parsed assoc request handed to ops->new_station().  The pointers point
 * into the request frame and are valid only for the duration of the call. */
struct lkpi_ap_sta_add {
	unsigned char mac[6];
	uint16_t aid;
	uint16_t listen_interval;
	uint16_t capab;
	const unsigned char *rates;	/* IE 1 followed by IE 50, raw */
	unsigned int rates_len;
	const unsigned char *ht_capa;	/* IE 45 body (26 B) or NULL */
	const unsigned char *wmm;	/* IE 221 body or NULL */
};

struct lkpi_ap_mlme_ops {
	/* NL80211_CMD_FRAME: one complete 802.11 mgmt frame. */
	int (*mgmt_tx)(const unsigned char *frame, unsigned int len);
	/* NL80211_CMD_NEW_STATION (+ the WME flag from the request). */
	int (*new_station)(const struct lkpi_ap_sta_add *sta);
	/* NL80211_CMD_SET_STATION with AUTHORIZED. */
	int (*set_authorized)(const unsigned char mac[6]);
	/* One preformatted line per event (no trailing newline). */
	void (*emit)(const char *line);
};

struct lkpi_ap_mlme_stats {
	unsigned int probe_req;
	unsigned int probe_resp;
	unsigned int probe_fail;
	unsigned int auth_req;
	unsigned int auth_resp;
	unsigned int auth_fail;
	unsigned int assoc_req;
	unsigned int assoc_resp;
	unsigned int assoc_fail;
	unsigned int sta_add;
	unsigned int sta_add_fail;
	unsigned int sta_auth;
	unsigned int sta_auth_fail;
	unsigned int q_full;
	unsigned int drop_bad;
	unsigned int drop_unsupported;
	unsigned int tx_fail;
};

/*
 * Copy the SSID and the AP's beacon head/tail into the MLME.  `head` is the
 * hostapd-style head (802.11 header + fixed fields + SSID + supported rates +
 * DS parameter); `tail` is every IE after the TIM.  Both stay untouched; the
 * MLME only reads them.  Returns 0 or a negative errno.
 */
int lkpi_ap_mlme_configure(const char *ssid, const unsigned char *head,
			   unsigned int head_len, const unsigned char *tail,
			   unsigned int tail_len,
			   const struct lkpi_ap_mlme_ops *ops);

/* A second configure() without a stop() is refused. */
void lkpi_ap_mlme_stop(void);

/* Response generation is off until the BSS is active (START_AP returned 0). */
void lkpi_ap_mlme_enable(int on);
int lkpi_ap_mlme_enabled(void);

/* RX path: copy one management frame into the queue.  Never sleeps. */
int lkpi_ap_mlme_rx_push(const unsigned char *frame, unsigned int len);

/* Task context: handle up to one queued frame.  Returns 1 when a frame was
 * handled, 0 when the queue was empty, or a negative errno for a frame that
 * could not be parsed (counted in drop_bad; the queue still advances). */
int lkpi_ap_mlme_poll(void);

/* The state machine for one frame, synchronously (used by poll and the host
 * bench).  Returns 0 when the frame was handled, -ENOTSUP for a subtype the
 * MLME does not own, -EINVAL for a malformed frame. */
int lkpi_ap_mlme_handle_frame(const unsigned char *frame, unsigned int len);

void lkpi_ap_mlme_stats_get(struct lkpi_ap_mlme_stats *out);

/* Number of stations the MLME has added (the authoritative table is the
 * stack's; this is the MLME's own bookkeeping for the anchors). */
unsigned int lkpi_ap_mlme_sta_count(void);

#endif /* LINUX80211_AP_MLME_H */

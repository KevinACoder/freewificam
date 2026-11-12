/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   sta_wpa.h
 * @brief  The STA supplicant's plain-C face: start one WPA2-PSK network and
 *         report the state the board case and the C6 throughput case need.
 *
 * The imported supplicant lives in the hostap world, with its own headers and
 * types; the board case lives in the stack world and must not see those.  This
 * header is the only contact: SSID / passphrase / BSSID / frequency in, state
 * and counters out.
 */
#ifndef LINUX80211_STA_WPA_H
#define LINUX80211_STA_WPA_H

struct lkpi_sta_cfg {
	const char	*ssid;		/* required, 1..32 bytes */
	const char	*psk;		/* passphrase 8..63; NULL = open */
	const unsigned char *bssid;	/* optional, 6 bytes */
	int		freq_mhz;	/* 0 = any */
};

/* Start the supplicant task, add the one network and select it.  Returns 0
 * when the interface exists and the network is configured; the connection
 * itself is asynchronous and is observed with lkpi_sta_state().  A NULL psk
 * configures an open network: the supplicant still owns the association, it
 * just has no 4-way handshake to run. */
int lkpi_sta_start(const struct lkpi_sta_cfg *cfg);

/* The supplicant's enum wpa_states value, or -1 before the interface exists. */
int lkpi_sta_state(void);

/* 1 when the supplicant reports WPA_COMPLETED. */
int lkpi_sta_ready(void);

/* Counters for the case's anchors.  Any output may be NULL. */
void lkpi_sta_counts(unsigned *eapol_tx, unsigned *eapol_rx, unsigned *drop,
		     unsigned *ptk, unsigned *gtk, unsigned *fail);

/* Stop the daemon task and clear the EAPOL sink. */
void lkpi_sta_stop(void);

/* Port-internal counters: l2_packet and the driver table report here so the
 * case can print one line instead of parsing supplicant logs. */
void lkpi_sta_note_eapol_tx(int rc);
void lkpi_sta_note_eapol_rx(void);
void lkpi_sta_note_key(int pairwise, int default_key, int rc);

#endif /* LINUX80211_STA_WPA_H */

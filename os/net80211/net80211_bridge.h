/*
 * net80211_bridge.h - the shell-facing API of the rtw88/net80211 bridge
 * (os/net80211/lkpi_bridge.c). Consumed by port/adapters/rtw88's wlanrtw
 * command so the shell world never touches net80211 types.
 */

#ifndef NET80211_BRIDGE_H
#define NET80211_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

int	net80211_bridge_up(void);
int	net80211_bridge_scan(const uint8_t *ssid, size_t len);
/* join the named BSS from the scan table (W4 open: SSID must be in the
 * table, privacy flags cleared, ni_authmode OPEN) */
int	net80211_bridge_connect(const uint8_t *ssid, size_t len);
int	net80211_bridge_get_hwaddr(uint8_t addr[6]);
/* D-3 diagnostic: override the local MAC before up() - the mesh-AP
 * stale-session fork (fresh identity vs same-MAC silence).  Must run
 * before net80211_bridge_up(); add_interface/sw_scan_start/frame
 * builder all read ic_myaddr live. */
void	net80211_bridge_set_mac(const uint8_t addr[6]);
void	net80211_bridge_status(void);
void	net80211_bridge_scan_dump(void);

/* ------------------------------------------------------------------ */
/* W4/W5 data-plane face (consumed by the wpa backend + lwip netif)    */
/* ------------------------------------------------------------------ */

/* MANUAL roaming: the supplicant drives BSS selection/joint itself
 * (ROAMING_AUTO's end_scan matcher would race it otherwise) */
void	net80211_bridge_set_manual(int on);

/* WPA mode: raise F_WPA1|F_WPA2 for the whole supplicant session.  Must
 * run BEFORE any scan - the flag gates the RSN/WPA IE parse path, and a
 * table scanned without it carries no ni_wpa_ie, which makes the
 * supplicant blind to PSK networks and loop "SCANNING" forever (R4
 * board round 1) */
void	net80211_bridge_set_wpa(int on);
/* scan-table sensor for the board rounds: total nodes + rows that carry
 * a saved WPA/RSN IE */
void	net80211_bridge_scan_sensor(unsigned int *rows,
	    unsigned int *wpa_rows);

/* WPA join: policy + direct sta_join on a scan-table node, the prior
 * project's board-proven MANUAL shape.  wpa_ie = the RSN IE the assoc
 * request appends verbatim (ic_opt_ie); bssid NULL selects by SSID. */
int	net80211_bridge_wpa_join(const uint8_t *ssid, size_t len,
	    const uint8_t *bssid, const uint8_t *wpa_ie, size_t ie_len);

/* key install: net80211 bookkeeping first (crypto_newkey/setkey under
 * the serializer - the encap/decap route), then the rtw88 CAM write
 * (drv.set_key; USB control sleep, outside the serializer).  pairwise=0
 * installs GTK at key_idx; set_tx marks it the default tx key. */
int	net80211_bridge_key_install(int pairwise, int key_idx, int set_tx,
	    const uint8_t *key, size_t len);
void	net80211_bridge_key_clear(int pairwise, int key_idx);
/* open the 802.1X port after the PTK landed (RX gate) */
void	net80211_bridge_authorize(void);
/* disconnect: INIT + node leave (AP kicks, supplicant reconnects) */
void	net80211_bridge_disconnect(int reason);

/* pull-model scan table (hostap-free shape): idx walks the table;
 * ies/ies_len point into a per-call static buffer (rates IE + the raw
 * WPA/RSN IE) valid until the next call. */
struct net80211_bridge_scan {
	uint8_t bssid[6];
	uint8_t ssid[33];
	size_t	ssid_len;
	int	freq;
	uint16_t beacon_int;
	uint16_t caps;
	int	level;
	const uint8_t *ies;
	size_t	ies_len;
};
int	net80211_bridge_scan_fetch(int idx, struct net80211_bridge_scan *out);

/* single data/EAPOL TX entry (S8 FR-12 one channel): copies the linear
 * ethernet frame into a pool mbuf, enqueues if_snd and kicks if_start;
 * net80211 encap + crypto + the driver relay run under the serializer */
int	net80211_bridge_tx_raw(const uint8_t *frame, size_t len);
/* TX health for the board rounds */
void	net80211_bridge_tx_stats(unsigned int *queued, unsigned int *sent,
	    unsigned int *dropped, unsigned int *no_peer);
/* RX/assoc health for the board rounds (W4W5 forensics) */
void	net80211_bridge_rx_stats(unsigned int *frames, unsigned int *prot,
	    unsigned int *decrypted, unsigned int *auth_events);

#endif /* NET80211_BRIDGE_H */

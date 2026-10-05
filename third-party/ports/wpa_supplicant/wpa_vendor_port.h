/*
 * @file
 * @brief Vendor driver wrapper contract ([S7a] FR-8, spec §3.2.2).
 *
 * The supplicant's single "embox" driver slot is served by
 * driver_vendor.c (this directory, skeleton included): every ops call
 * forwards to the table registered here. The consumer line (S5a
 * rtl8821cu / S5b rtl8851bu, on their feat branches) implements the
 * table and registers it once its driver attach completed - BEFORE
 * `wpa start` (ensure_up may run on the shell thread, never on the
 * supplicant thread).
 *
 * This header is intentionally vendor-neutral: no hostap types and no
 * vendor driver types leak across it. The skeleton does the enum
 * translation on the supplicant side.
 */

#ifndef WPA_VENDOR_PORT_H_
#define WPA_VENDOR_PORT_H_

#include <stddef.h>
#include <stdint.h>

/* one scan-table entry as the vendor scan table reports it (raw
 * fields; the wpa_scan_res/IE packing lives in driver_vendor.c) */
struct wpa_scan_res_ret {
	uint8_t bssid[6];
	uint8_t ssid[33];
	size_t ssid_len;
	int freq;		/* MHz, 0 = unknown */
	uint16_t beacon_int;	/* TU, 0 = unknown */
	uint16_t caps;		/* 802.11 capability info */
	int level;		/* RSSI, driver units */
	const uint8_t *ies;	/* beacon IEs (RSN/WPA live here) */
	size_t ies_len;
};

/* connect request. wpa_ie != NULL means a PSK network (the supplicant
 * only builds the RSN/WPA IE for those); NULL = open ([S7a] §3.2.2
 * ap_scan=1 shape). */
struct wpa_vendor_conn {
	const uint8_t *ssid;
	size_t ssid_len;
	const uint8_t *bssid;	/* 6 bytes when bssid_set */
	int bssid_set;
	const uint8_t *wpa_ie;
	size_t wpa_ie_len;
};

/* key material for set_key */
#define WPA_VENDOR_ALG_NONE 0
#define WPA_VENDOR_ALG_CCMP 1

struct wpa_vendor_key {
	int alg;		/* WPA_VENDOR_ALG_* */
	int key_idx;
	const uint8_t *addr;	/* NULL or multicast bit = group key */
	int set_tx;
	const uint8_t *key;
	size_t key_len;
	const uint8_t *seq;	/* PN, big-endian bytes */
	size_t seq_len;
};

/* the ops table a consumer line must provide - every entry non-NULL
 * (registered once, before wpa_port_start) */
struct wpa_vendor_port {
	/* chip bring-up; may block on USB workers - the port layer only
	 * calls it from the shell thread */
	int (*ensure_up)(void);
	/* station MAC (efuse), for l2_packet own_addr */
	int (*get_hwaddr)(uint8_t addr[6]);
	/* trigger a site survey; ssid NULL/len 0 = all channels */
	int (*scan_start)(const uint8_t *ssid, size_t len);
	/* pull-model scan table: fill *out for idx, return 0; != 0 (or
	 * idx past the end) ends the iteration */
	int (*scan_fetch)(int idx, struct wpa_scan_res_ret *out);
	/* issue MLME connect: security state + wpa_ie + auth/assoc */
	int (*connect)(const struct wpa_vendor_conn *p);
	/* MLME disconnect (deauth with reason) */
	int (*disconnect)(uint16_t reason);
	/* install/clear a key (pairwise or group) into the driver/CAM */
	int (*set_key)(const struct wpa_vendor_key *k);
	/* EAPOL frame out (Ethernet header built here, not by the
	 * caller); len is the 802.1X payload, not counting the header */
	int (*eapol_xmit)(const uint8_t dst[6], const uint8_t *buf,
	    size_t len);
	/* register the driver-side event pump. fn runs in a driver/USB
	 * worker context and may ONLY call wpa_send_event (deep-copy
	 * queue); events:
	 *   WPA_VENDOR_EVENT_SCAN_DONE  - survey done (addr/buf NULL)
	 *   WPA_VENDOR_EVENT_ASSOC      - associated (addr = BSSID)
	 *   WPA_VENDOR_EVENT_DISASSOC   - lost/forced disassoc
	 *   WPA_VENDOR_EVENT_EAPOL_RX   - buf/len = 802.1X payload,
	 *                                 addr = source MAC */
	void (*set_event_cb)(void (*fn)(int event, const uint8_t *addr,
				 const uint8_t *buf, size_t len, void *arg),
			     void *arg);
};

int wpa_vendor_port_register(const struct wpa_vendor_port *ops);

/* internal: the registered backend, NULL while none (driver_vendor.c
 * definition; the port layer reads it for the ensure_up routing) */
const struct wpa_vendor_port *wpa_vendor_backend(void);

#define WPA_VENDOR_EVENT_SCAN_DONE 1
#define WPA_VENDOR_EVENT_ASSOC 2
#define WPA_VENDOR_EVENT_DISASSOC 3
#define WPA_VENDOR_EVENT_EAPOL_RX 4

#endif /* WPA_VENDOR_PORT_H_ */

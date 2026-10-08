/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    wlan_bridge.h
 * @brief   The net80211-world face of the bridge: attach/detach driven by the
 *          driver's side, the receive entry the driver's side calls, the scan
 *          dwell and the counters the port shell and the tests read.
 *
 * @design two-worlds adapter, one translation layer per world. The imported
 *         net80211 stack and the linuxkpi mac80211 substitute must never share a
 *         translation unit: both define identically named structures with
 *         different layouts (ieee80211_channel is the classic example), which is
 *         why ports/net80211/seam.h carries only C scalars, byte arrays and
 *         opaque handles. The bridge therefore comes in two halves that talk
 *         through that seam:
 *           - this half (wlan_bridge.c, net80211 world) owns the ieee80211com,
 *             drains the stack's queues and injects received frames;
 *           - the driver half (wlan_seam_adapter.c, linuxkpi world) implements
 *             the seam vtable by calling the mac80211 substitute's helpers.
 *         This header is the net80211-world declaration set: it includes seam.h
 *         (scalars only) and never a linuxkpi header, so either half - and any
 *         presentation layer - can include it without pulling the other world
 *         in.
 *         Reference for the entry-set shape: the tested substitute's install
 *         helper (tests/host/net80211-frame-test/vwifi.c:404-411) and the port
 *         interface it feeds (components/net80211/port/port.h).
 * @date    08.10.2026
 */

#ifndef WLAN_BRIDGE_H
#define WLAN_BRIDGE_H

#include <stdint.h>
#include <stddef.h>

#include "../seam.h"

/* the presentation layer's ifops (declared by the component's port.h; only the
 * pointer crosses this header, so the declaration is enough) */
struct wlan_port_ifops;

/* the adapter name the port shell answers to */
#define WLAN_BRIDGE_ADAPTER_NAME	"rtl8xxxu"

/* the bridge's counters (one snapshot struct; the port shell prints it) */
struct wlan_bridge_counters {
	unsigned int	mgt_tx;
	unsigned int	data_tx;
	unsigned int	rx_frames;
	unsigned int	tx_completes;
	unsigned int	scan_starts;
	unsigned int	scan_completes;
	unsigned int	filter_sets;
	unsigned int	filter_clears;
	int		attached;
};

/*
 * Attach: the driver half calls this when the Linux driver registered its hw
 * (so its ops table is live and the substitute can drive it). `drv` is the
 * driver half's opaque handle, `ops` its seam vtable, `mac` the interface
 * address the driver reported. Returns 0, or a negative error.
 */
int	wlan_bridge_attach(struct wlan_seam_drv *drv,
	    const struct wlan_seam_drv_ops *ops, const uint8_t mac[6]);
void	wlan_bridge_detach(void);

/*
 * Receive: one 802.11 frame (no FCS) from the driver half, borrowed - the
 * bridge copies it into the stack's own buffer before returning. `rssi` is the
 * driver's signal report (0 when it has none).
 */
int	wlan_bridge_input(const uint8_t *frame, size_t len, int rssi);

/* A transmitted frame completed (the driver half's tx-status callback). */
void	wlan_bridge_tx_complete(void);

/*
 * The scan group. start programs the receive filter promiscuous first (a chip
 * that is not promiscuous reports no unknown beacon, so a sweep before the bit
 * is set records nothing), asks the driver to prepare its radio and drives the
 * stack into its scan state. dwell is the driver's "this channel is done"
 * report: the port worker calls it when the dwell expires; when the stack ends
 * the sweep, the filter goes back and the driver is told the scan completed.
 */
int	wlan_bridge_scan_start(void);
int	wlan_bridge_scan_dwell(void);

/* Tune the driver's radio to the channel the stack currently sits on (the scan
 * walk advances the stack's channel; the driver must follow). */
int	wlan_bridge_tune_current(void);

/* Configure the desired SSID (the joiner's step before a sweep; the stack's
 * match then requires a candidate to carry it). Returns 0 or a negative error. */
int	wlan_bridge_set_ssid(const uint8_t *ssid, size_t len);

/*
 * The supplicant-driven connect path. The bridge is put into MANUAL roaming
 * first (wlan_bridge_set_manual): the stack then reports a finished sweep
 * without joining on its own, and the supplicant's decision arrives here as
 * an explicit join - the SSID and (for a PSK network) the RSN IE the
 * supplicant built, against a candidate the sweep already recorded.
 *   - `bssid` locks the candidate (NULL = match by SSID);
 *   - `wpa_ie` non-NULL marks a PSK network: F_WPA2 (or F_WPA1 for a WPA IE)
 *     and F_PRIVACY go up, the IE is carried in the association request, and
 *     the node is put under 802.1X so RUN does not auto-authorize - the
 *     pairwise key install afterwards is what opens the port.
 * Returns 0 or a negative error.
 */
int	wlan_bridge_join(const uint8_t *ssid, size_t len, const uint8_t *bssid,
	    const uint8_t *wpa_ie, size_t ie_len);

/* Disconnect with the given IEEE reason code (the stack returns to INIT). */
void	wlan_bridge_disconnect(int reason);

/*
 * Roaming control. AUTO (the attach default) lets end_scan join the best
 * candidate by itself; MANUAL makes the sweep report and stop, which is the
 * mode a supplicant needs (its own decision must not race the stack's).
 */
void	wlan_bridge_set_manual(int on);

/* WPA mode gate: the stack parses RSN/WPA IEs (and records them on scan
 * candidates) only while a WPA flag is set, so this must precede any sweep a
 * supplicant will read the table from. */
void	wlan_bridge_set_wpa(int on);

/*
 * Pull one scan-table row (the supplicant's get_scan_results2 reads rows until
 * a non-zero return). The IEs the row carries - rates plus the RSN/WPA element
 * the candidate advertised - are assembled into a bridge-owned static buffer
 * and borrowed by the caller until the next call. Returns 0 or a negative
 * error (including "past the end").
 */
struct wlan_bridge_scan_res {
	uint8_t	bssid[6];
	uint8_t	ssid[33];
	size_t	ssid_len;
	int	freq;			/* MHz, 0 unknown */
	uint16_t beacon_int;
	uint16_t caps;			/* 802.11 capability info */
	int	level;			/* RSSI, driver units */
	const uint8_t *ies;		/* borrowed until the next fetch */
	size_t	ies_len;
};
int	wlan_bridge_scan_fetch(int idx, struct wlan_bridge_scan_res *out);

/*
 * Install a key the way a supplicant's set_key does: the stack-side
 * bookkeeping first (the cipher is attached to the slot, so encap/decrypt
 * start using it), then the driver's CAM write through the seam - both legs
 * belong to one key, which is why this is one call and not two. A pairwise
 * install also marks the node authorized (the port opening the 802.1X gate
 * waits for). `cipher` uses the seam's WLAN_SEAM_CIPHER_* numbering.
 */
int	wlan_bridge_key_install(int pairwise, int key_idx, int set_tx,
	    int cipher, const uint8_t *key, size_t key_len);

/* Walk the scan table, one callback per candidate, scalars only (the caller may
 * live in the other world). Returns the number of candidates seen. */
unsigned int wlan_bridge_scan_walk(void (*cb)(const uint8_t bssid[6],
	    const uint8_t *ssid, size_t ssid_len, uint32_t freq, int rssi,
	    void *arg), void *arg);

/* The stack's current state value (the imported numbering), -1 when detached. */
int	wlan_bridge_state(void);

/* Is a sweep in flight? (the radio's dwell pump watches this) */
int	wlan_bridge_scan_active(void);

/* One ethernet frame from the presentation layer: queued to the interface, so
 * net80211's own encapsulation and crypto run on the way out. */
int	wlan_bridge_tx_raw(const uint8_t *frame, size_t len);

/* The key path: the crypto policy decides, the driver programs; the bridge is
 * the wire. Cipher values are the seam's WLAN_SEAM_CIPHER_* numbering. */
int	wlan_bridge_key_set(int pairwise, int key_idx, int cipher,
	    const uint8_t *key, size_t key_len);
int	wlan_bridge_key_clear(int pairwise, int key_idx);

/* The driver's up/down chain (its ops->start / stop, through the seam). */
int	wlan_bridge_up(void);
void	wlan_bridge_down(void);

int	wlan_bridge_hwaddr(uint8_t addr[6]);
void	*wlan_bridge_ic(void);
void	wlan_bridge_stats(struct wlan_bridge_counters *out);

/*
 * The presentation layer's up/down notification (port.h's wlan_port_ifops):
 * the bridge calls these when net80211 moves between INIT and RUN, which is
 * where a netif starts and stops its DHCP client. No setter exists in the
 * imported port.h, so the entry lives here rather than being added to the
 * component.
 */
void	wlan_bridge_set_ifops(const struct wlan_port_ifops *ops, void *if_priv);

/*
 * Firmware lookup, implemented by the driver half (the compiled-in registry is
 * a linuxkpi component, so the net80211 half cannot reach it). Borrowed bytes;
 * the caller does not free them. Returns 0 or a negative error.
 */
int	wlan_bridge_fw_lookup(const char *name, const uint8_t **data,
	    size_t *size);

/*
 * The driver half's entry point: installs the linuxkpi-side bridge and the
 * seam's driver handle. Called at start-up by whoever owns the driver (the
 * board's WLAN bring-up, or a host harness). Returns 0 or a negative error.
 */
int	wlan_seam_adapter_init(void);

#endif /* WLAN_BRIDGE_H */

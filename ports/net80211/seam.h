/*
 * @file
 * @brief The driver-side seam of the net80211 port: a plain-C scalar vtable.
 *
 * net80211's state machine and a Linux-family driver (rtw88 and friends) live in
 * two worlds that both define identically named structures with different
 * layouts (ieee80211_channel is the classic example). They must never share a
 * struct: this header carries only C scalars, byte arrays and opaque handles,
 * so the two sides can hold different definitions of everything else and still
 * talk.
 *
 * Execution model (owned by the bridge, later milestone): the driver side runs
 * on a workqueue; timers only flag and queue work, they never sleep. The
 * primitive names come from the repository's linuxkpi contract headers
 * (components/linuxkpi/linuxkpi_core.h: workqueue lkpi_alloc_workqueue and
 * lkpi_queue_work, timer lkpi_mod_timer and lkpi_del_timer, mutex lkpi_mutex_*).
 * This header deliberately does not include those headers or any component
 * header: the seam stays free of both worlds' types.
 *
 * @design plain-C scalar vtable / opaque-handle ABI, re-derived from the
 * previous line's seam shape (components/net80211/linuxkpi/linuxkpi_net80211.h
 * on its stabilize branch, which states "the ABI carries only C scalar and
 * byte-array types" rule). Re-derived, not copied: this repository writes its
 * own bytes and the callback set below is the minimum the state machine needs,
 * not the previous line's full surface.
 *
 * Revisions (the ABI was frozen at its first consumer; each change is recorded
 * here with its reason):
 *   - v2 (bridge round): the `filter` callback joins. A software scan cannot be
 *     correct without it - the chip's receive filter drops other BSS's beacons
 *     until the promiscuous bit is programmed - and the ordering (before the
 *     sweep, cleared after) is a property of the scan flow this seam serves, so
 *     the callback belongs on the seam rather than behind a driver-private
 *     path. The vtable is extended at the same point as the scan pair where the
 *     ordering applies (see WLAN_SEAM_FIF_BCN_PRBRESP_PROMISC). Consumers
 *     implement the new slot before the scan flow runs.
 */

#ifndef _WLAN_NET80211_SEAM_H_
#define _WLAN_NET80211_SEAM_H_

#include <stddef.h>
#include <stdint.h>

/* The driver adapter handle: created by the driver side, opaque here. */
struct wlan_seam_drv;

enum wlan_seam_cipher {
	WLAN_SEAM_CIPHER_NONE = 0,
	WLAN_SEAM_CIPHER_WEP = 1,
	WLAN_SEAM_CIPHER_TKIP = 2,
	WLAN_SEAM_CIPHER_CCMP = 3,
};

enum wlan_seam_iftype {
	WLAN_SEAM_IFTYPE_STATION = 2,
	WLAN_SEAM_IFTYPE_AP = 3,
};

/* Configuration bits passed to config(): a channel change is the only one the
 * state machine needs today; the bitmask leaves room without changing the ABI. */
#define WLAN_SEAM_CONFIG_CHANNEL 0x00000001u

/* Receive-filter bits passed to filter(): the chip's receive filter is a
 * hardware fact the stack cannot see, and the beacon/probe-response
 * promiscuous bit is the one a software scan depends on - without it a chip
 * drops beacons from every BSS it does not already know, and the scan table
 * comes back empty (board evidence; the Linux core programs this same bit
 * around its own software scan). The value is the chip-side selector, not a
 * net80211 flag. */
#define WLAN_SEAM_FIF_BCN_PRBRESP_PROMISC	0x00000010u

struct wlan_seam_drv_ops {
	/* Bring the chip up/down. Called from the port worker with the
	 * serializer held; must not sleep on driver-internal waits that need
	 * the interface to be running. */
	int (*start)(struct wlan_seam_drv *drv);
	int (*stop)(struct wlan_seam_drv *drv);

	/* One virtual interface, identified by its hardware address. */
	int (*add_interface)(struct wlan_seam_drv *drv, const uint8_t mac[6],
	    int iftype);

	/* Channel/state configuration; `changed` is a WLAN_SEAM_CONFIG_* mask,
	 * `freq` the target centre frequency in MHz (0 = no channel change). */
	int (*config)(struct wlan_seam_drv *drv, uint32_t changed,
	    uint32_t freq);

	/* Receive-filter programming: `changed` names the bits that moved
	 * (WLAN_SEAM_FIF_*), `total` carries every bit currently set. The
	 * ordering contract lives with the caller: a software scan sets the
	 * beacon-promiscuous bit before sw_scan_start and clears it after the
	 * sweep completes. */
	int (*filter)(struct wlan_seam_drv *drv, uint32_t changed,
	    uint32_t total);

	/* Software scan: start asks the device to sweep, complete is the
	 * "results are in" notification. Both must return promptly. */
	int (*sw_scan_start)(struct wlan_seam_drv *drv);
	void (*sw_scan_complete)(struct wlan_seam_drv *drv);

	/*
	 * Hardware scan. A driver whose ops table carries hw_scan and no
	 * sw_scan_start has no software route at all (the AC 7260's mvm path),
	 * so the sweep has to be asked for as a firmware one: the bridge hands
	 * its channel set down, the device owns the dwell, and the end arrives
	 * back up through the bridge's scan-complete notification instead of
	 * from the bridge's own channel walk.
	 *
	 * `has_hw_scan` is the capability question the bridge asks to choose
	 * between this route and the software one - a query, not a state
	 * change. `hw_scan_start` returns 0 when the device accepted the sweep.
	 * `freqs` are centre frequencies in MHz, `n_freqs` their count.
	 */
	int (*has_hw_scan)(struct wlan_seam_drv *drv);
	int (*hw_scan_start)(struct wlan_seam_drv *drv, const uint32_t *freqs,
	    size_t n_freqs);
	void (*hw_scan_cancel)(struct wlan_seam_drv *drv);

	/* Transmit one 802.11 frame. is_mgmt separates management frames (the
	 * driver may need to wait for them) from data frames. */
	int (*tx)(struct wlan_seam_drv *drv, const uint8_t *frame, size_t len,
	    int is_mgmt);

	/* Key management: key material is a byte array, everything else is a
	 * scalar (pairwise/group key, index, cipher). */
	int (*set_key)(struct wlan_seam_drv *drv, int pairwise, int key_idx,
	    int cipher, const uint8_t *key, size_t key_len);
	int (*clear_key)(struct wlan_seam_drv *drv, int pairwise, int key_idx);

	/* BSS state change: joined != 0 means associated to bssid on freq MHz
	 * with association id aid (raw, no 0xc000 marking) and the peer's
	 * rate set (rates_len values in 500 kbit/s units, the 0x80 flag marking
	 * basic rates - the net80211 on-air encoding); joined == 0 means the
	 * connection went away (aid 0, rates ignored). The aid and rates are
	 * what a mac80211 driver's association branch reads out of vif->cfg and
	 * the station - without them it runs its NOT-associated leg. */
	void (*bss_info)(struct wlan_seam_drv *drv, int joined,
	    const uint8_t bssid[6], uint32_t freq, uint16_t aid,
	    const uint8_t *rates, size_t rates_len);

	/* BSSID programming for the pre-association exchange. A chip's receive
	 * filter compares frames against this register, so it must hold the
	 * AP's address BEFORE the auth request goes out or the AP's answers
	 * are dropped in hardware (mac80211 programs it for the same reason;
	 * this port measured the absence as an auth timeout with the requests
	 * leaving). NULL clears it. */
	int (*bssid)(struct wlan_seam_drv *drv, const uint8_t bssid[6]);
};

#endif /* _WLAN_NET80211_SEAM_H_ */

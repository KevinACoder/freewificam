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
 * on its stabilize branch, which states the "the ABI carries only C scalar and
 * byte-array types" rule). Re-derived, not copied: this repository writes its
 * own bytes and the callback set below is the minimum the state machine needs,
 * not the previous line's full surface.
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

	/* Software scan: start asks the device to sweep, complete is the
	 * "results are in" notification. Both must return promptly. */
	int (*sw_scan_start)(struct wlan_seam_drv *drv);
	void (*sw_scan_complete)(struct wlan_seam_drv *drv);

	/* Transmit one 802.11 frame. is_mgmt separates management frames (the
	 * driver may need to wait for them) from data frames. */
	int (*tx)(struct wlan_seam_drv *drv, const uint8_t *frame, size_t len,
	    int is_mgmt);

	/* Key management: key material is a byte array, everything else is a
	 * scalar (pairwise/group key, index, cipher). */
	int (*set_key)(struct wlan_seam_drv *drv, int pairwise, int key_idx,
	    int cipher, const uint8_t *key, size_t key_len);
	int (*clear_key)(struct wlan_seam_drv *drv, int pairwise, int key_idx);

	/* BSS state change: joined != 0 means associated to bssid on freq MHz;
	 * joined == 0 means the connection went away. */
	void (*bss_info)(struct wlan_seam_drv *drv, int joined,
	    const uint8_t bssid[6], uint32_t freq);
};

#endif /* _WLAN_NET80211_SEAM_H_ */

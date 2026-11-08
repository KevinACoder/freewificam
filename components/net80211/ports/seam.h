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
 *   - v3 (hardware-CCMP round): the `tx` callback carries the key identity
 *     the stack selected (`hw_crypto`, `pairwise`, `key_idx`). A driver that
 *     offloads CCMP must put the stable driver-side key object matching that
 *     identity into the transmit descriptor; the stack's own key is not that
 *     object. The vtable still carries scalars only - the object stays on the
 *     driver side of the seam.
 *   - v4 (uplink round): `tx` tells its refusals apart (WLAN_SEAM_TX_ERR_*).
 *     The bridge used to read every negative answer as the same error and
 *     drop the frame; a frame refused because the socket pool was empty is
 *     still good and can be handed down later, while one refused because the
 *     peer or the key is not in place can never leave the host. The codes
 *     are the seam's own (not errno) so both sides name the same three cases
 *     without agreeing on a C library.
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

/* Channel widths the optional config_ht() call uses (MHz).  A port that
 * cannot run 40 MHz leaves config_ht NULL and the bridge keeps the
 * association at 20 MHz. */
#define WLAN_SEAM_CHAN_WIDTH_20	20u
#define WLAN_SEAM_CHAN_WIDTH_40	40u

/* Receive-filter bits passed to filter(): the chip's receive filter is a
 * hardware fact the stack cannot see, and the beacon/probe-response
 * promiscuous bit is the one a software scan depends on - without it a chip
 * drops beacons from every BSS it does not already know, and the scan table
 * comes back empty (board evidence; the Linux core programs this same bit
 * around its own software scan). The value is the chip-side selector, not a
 * net80211 flag. */
#define WLAN_SEAM_FIF_BCN_PRBRESP_PROMISC	0x00000010u

/*
 * The port's observation snapshot (wlan_seam_drv_ops.port_obs): what the
 * adapter's own layers below the seam are doing, as scalars, so a watchdog on
 * the stack side can put a frozen data path on the right layer without linking
 * against the driver's world.  Every field is a gauge or a counter sampled at
 * snapshot time; nothing here is written by the stack.  A stall is localized
 * by which of these stops advancing and which keeps going:
 *
 *   usb_drain_done flat, rx_ep_qlen flat, tx_ep_* flat
 *	-> nothing is being completed: the transport stopped, not the stack
 *   usb_drain_phase stuck at 3 (and usb_drain_done flat)
 *	-> the drain thread is inside a driver completion callback
 *   rx_ep_completions rising, rx_ep_enqueued flat
 *	-> the driver's re-arm stopped (the transfer was completed but no
 *	   substitute for it was submitted back)
 *   wq_full rising
 *	-> the deferred-work queue refused the driver's receive work
 *   skb_alloc_fail rising
 *	-> the driver's own buffer pool refused a receive buffer
 */
struct wlan_seam_port_obs {
	/* the transport's completion engine */
	unsigned int	usb_drain_phase;	/* 1 idle, 2 arm, 3 callback, 4 free */
	unsigned int	usb_drain_state;	/* 0 none, 1 run, 2 ready, 3 block, 4 term */
	unsigned int	usb_drain_done;		/* completions carried so far */
	unsigned int	usb_polls;		/* lost-interrupt polls run */
	unsigned int	usb_poll_drained;	/* completions they recovered */
	unsigned int	usb_kicks;
	unsigned int	usb_completions;
	unsigned int	usb_kills;		/* urbs the driver cancelled */
	unsigned int	usb_parks;
	unsigned int	usb_parked_now;
	unsigned int	usb_errors;
	/* the bulk-in endpoint: the receive stream */
	unsigned int	rx_ep_addr;
	unsigned int	rx_ep_qlen;		/* urbs queued behind the armed one */
	unsigned int	rx_ep_in_flight;
	unsigned int	rx_ep_enqueued;		/* submits accepted (driver arms) */
	unsigned int	rx_ep_completions;
	/* the bulk-out endpoint: the transmit stream */
	unsigned int	tx_ep_addr;
	unsigned int	tx_ep_qlen;
	unsigned int	tx_ep_in_flight;
	unsigned int	tx_ep_enqueued;
	unsigned int	tx_ep_completions;
	/* the deferred-work pump the driver's receive re-arm rides */
	unsigned int	wq_depth;
	unsigned int	wq_max;
	unsigned int	wq_runs;
	unsigned int	wq_direct;		/* ran inline: the queue was full */
	unsigned int	wq_full;		/* submissions refused: work lost */
	unsigned int	wq_state;		/* the worker thread: 1 run, 2 ready,
						 * 3 block, 4 term, 5 other, 0 none */
	/* the driver's own buffer pool (skb stand-in) */
	unsigned int	skb_used;
	unsigned int	skb_high;
	unsigned int	skb_alloc_fail;
	/*
	 * The controller's health, read straight out of the host controller's
	 * registers rather than inferred from the transfers.  When a transfer
	 * never completes, these separate "the controller is still running and
	 * the completion was lost" from "the controller stopped" and from "the
	 * root port went away":
	 *   usb_irqN      the controller's interrupt line was serviced N times
	 *   ehciN_frame   FRINDEX of USB2 root N: it advances once per 125 us
	 *                 while the controller's frame list runs
	 *   ehciN_status  USBSTS of root N: bit 0 USBINT, bit 2 PCD (port
	 *                 change), bit 12 HCHalted, bit 14 HSE
	 *   ehciN_portsc  root N's PORTSC0: bit 0 CCS (connect), bit 2 PED
	 *                 (enabled), bit 3 PEDC, bit 7 PS (suspend)
	 */
	unsigned int	usb_irq0;
	unsigned int	usb_irq1;
	unsigned int	usb_irq2;
	unsigned int	usb_irq3;
	unsigned int	ehci0_frame;
	unsigned int	ehci0_status;
	unsigned int	ehci0_portsc;
	unsigned int	ehci1_frame;
	unsigned int	ehci1_status;
	unsigned int	ehci1_portsc;
	/* the xHCI (bus 3, the one the radio sits on) has no frame index:
	 * its liveness is the status word, the command ring and the port. */
	/* the SoC side of the same bus: the CRU gates and soft-reset bits the
	 * board bring-up released. These blocks answer while a controller is
	 * wedged, so they are safe to sample from the ledger. */
	unsigned int	soc_cru_gate;	/* CRU_CLKGATE_CON10 (USB3OTG clocks) */
	unsigned int	soc_cru_rst;	/* CRU_SOFTRST_CON9 (USB3OTG resets) */
	unsigned int	soc_pmucru_gate;/* PMUCRU_CLKGATE_CON2 (PHY ref gates) */
};

/*
 * Why `tx` refused a frame (v4). A negative return from the transmit
 * callback is one of these, or another negative value for a refusal the
 * caller cannot act on:
 *
 *   NOSKB   the seam could not allocate the frame's socket buffer. The frame
 *           is intact and the condition is transient (the pool drains as the
 *           device completes), so the caller keeps it and retries.
 *   NOPEER  a data frame with no station registered. Nothing will deliver it
 *           until the connection is rebuilt; retrying the same frame is a
 *           loop, so the caller drops it and counts it.
 *   NOKEY   hardware crypto was selected but the driver never registered the
 *           key. Sending it plaintext would leak and encrypting it here would
 *           double-seal once the CAM entry appears; the frame is dropped.
 */
#define WLAN_SEAM_TX_ERR_NOSKB		(-2)
#define WLAN_SEAM_TX_ERR_NOPEER		(-3)
#define WLAN_SEAM_TX_ERR_NOKEY		(-4)

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
	 * driver may need to wait for them) from data frames. hw_crypto means
	 * the stack key is hardware-offloaded (the stack wrote the CCMP header
	 * but not the payload); pairwise/key_idx name that key so the driver
	 * can select the same entry it wrote at set_key. Returns the frame
	 * length when the driver took it, and a WLAN_SEAM_TX_ERR_* code when it
	 * refused (v4). */
	int (*tx)(struct wlan_seam_drv *drv, const uint8_t *frame, size_t len,
	    int is_mgmt, int hw_crypto, int pairwise, int key_idx);

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

	/*
	 * The peer's 11n declaration, delivered BEFORE bss_info(joined=1) and
	 * before the station's association step.  In Linux this is not a seam
	 * call at all: mac80211 parses the (re)association response's HT
	 * Capabilities element straight into sta->deflink.ht_cap
	 * (ieee80211_ht_cap_ie_to_sta_ht_cap), and every driver reads the
	 * station's capability from there when it builds its rate table and
	 * picks a transmit rate.  An imported stack with no HT state (this
	 * net80211) leaves the station looking like a 54 Mbit/s peer: the AP
	 * grants HT, the driver never hears it, and the uplink stays at legacy
	 * rates - measured on the board 2026-11-04 (AP `rx bitrate 54.0
	 * MBit/s`, association response `htcap=1`, driver's station
	 * `ht_supported = 0`).
	 *
	 * cap = HT Capabilities Info, ampdu = the A-MPDU Parameters byte,
	 * mcs0 = the first four bytes of the Supported MCS Set.  Optional: a
	 * port whose driver is handed the capability elsewhere leaves it NULL
	 * and the bridge uploads nothing.
	 */
	void (*sta_ht)(struct wlan_seam_drv *drv, const uint8_t bssid[6],
	    uint16_t cap, uint8_t ampdu, uint32_t mcs0);

	/* BSSID programming for the pre-association exchange, carrying the AP's
	 * advertised timing as the interface fills it while it prepares the
	 * connection (ieee80211_prep_connection takes bss_conf->beacon_int from
	 * the AP's beacon and defaults the DTIM period to 1).  A driver reads
	 * those off the vif before it will take a station: the AC 7260 refuses
	 * one whose beacon interval is below its minimum ("Beacon interval 0
	 * for AP ... is too small", mvm/mac80211.c:3702).  A NULL bssid clears
	 * the target.  A chip's receive
	 * filter compares frames against this register, so it must hold the
	 * AP's address BEFORE the auth request goes out or the AP's answers
	 * are dropped in hardware (mac80211 programs it for the same reason;
	 * this port measured the absence as an auth timeout with the requests
	 * leaving). NULL clears it. */
	int (*bssid)(struct wlan_seam_drv *drv, const uint8_t bssid[6],
	    uint16_t beacon_int, uint8_t dtim_period);

	/*
	 * Beacon timing for the driver's TBTT bookkeeping.  mac80211 updates
	 * these three values on every beacon (it sets
	 * IEEE80211_HW_TIMING_BEACON_ONLY, which mvm relies on) and notifies
	 * BSS_CHANGED_BEACON_INFO for the first beacon after the association;
	 * mvm turns them into the MAC context's dtim_tsf / dtim_time /
	 * assoc_beacon_arrive_time (mvm/mac-ctxt.c iwl_mvm_set_fw_dtim_tbtt).
	 * Without them the firmware has no TBTT anchor and counts every beacon
	 * as missed.
	 *
	 * `tsf` is the beacon's own timestamp field (the AP's TSF at
	 * transmission), `device_ts` the device clock at reception, and
	 * `dtim_count` the TIM element's countdown.  `notify` is non-zero only
	 * on the first beacon of an association, the one that makes the driver
	 * re-send its MAC context.  Optional: a port whose driver keeps no
	 * such bookkeeping leaves it NULL.
	 */
	int (*beacon_sync)(struct wlan_seam_drv *drv, const uint8_t bssid[6],
	    uint64_t tsf, uint32_t device_ts, uint8_t dtim_count, int notify);

	/*
	 * Transmit backpressure: the driver's own queue state, asked before
	 * the bridge hands over another frame.  0 means the driver has stopped
	 * its transmit queues and does not want another frame until it wakes
	 * (rtl8xxxu stops them when its tx-urb pool crosses the low water and
	 * wakes them above the high water, core.c:5119/:5139).  Without this
	 * question the bridge keeps feeding a driver that has already said
	 * "full"; the driver's only remaining option is to drop the frame,
	 * which is what the board measured as "Unable to allocate tx urb".
	 *
	 * NULL = the port has no such state and is always ready (the other
	 * arms keep the old behavior).
	 */
	int (*queue_ready)(struct wlan_seam_drv *drv);

	/*
	 * Observation snapshot (struct wlan_seam_port_obs): scalars only, no
	 * state change, safe to call from a watchdog thread while the port is
	 * running.  NULL = the port has no such state (the other arms), and a
	 * caller must then leave the snapshot zeroed rather than guess.
	 */
	void (*port_obs)(struct wlan_seam_drv *drv,
	    struct wlan_seam_port_obs *out);

	/*
	 * One controller register, for a probe that runs only after a stall:
	 * which 0=command, 1=status, 2=command ring, 3=event ring dequeue,
	 * 4=port 1, 5=port 2 on the radio's bus.  This read is NOT safe from
	 * a periodic sampler - a controller that has stopped answering can
	 * hold the read forever - so the ledger never calls it; a probe
	 * thread prints a marker before the call, and the marker without a
	 * value is the answer.  Returns 0 when the port has no such surface.
	 */
	unsigned int (*port_probe)(struct wlan_seam_drv *drv,
	    unsigned int which);

	/*
	 * The controller driver's own view of a stalled port, printed by the
	 * stack it belongs to: the event ring's consumer position and cycle,
	 * the interrupt/todo/transfer-event counters, and for every device
	 * the endpoint rings with a transfer outstanding.  Called once by a
	 * probe thread after a stall was declared; it must stay read-only.
	 */
	void (*port_stall_dump)(struct wlan_seam_drv *drv);

	/*
	 * Run the controller's completion path once from the caller's thread:
	 * the same drain the interrupt performs.  This exists because the
	 * controller (or the line to the CPU) can drop an event interrupt and
	 * leave the ring undrained forever - the drain is idempotent and
	 * guarded against re-entry by the port, so a thread may run it when
	 * no interrupt has arrived for a while.
	 */
	void (*port_poll)(struct wlan_seam_drv *drv);

	/*
	 * Optional 40 MHz programming for the association the bridge is about
	 * to start.  The imported net80211 stack has no HT state, so the bridge
	 * is the only half that can read the AP's HT Operation element and pick
	 * the secondary channel; this call tells the driver half the width and
	 * centre the association will run on.  It is invoked before
	 * ieee80211_sta_join(), while the radio is already tuned to `freq`.
	 * Return 0 when the device accepted the width; a negative answer makes
	 * the bridge fall back to 20 MHz and keep the association at HT20.
	 * NULL = the port has no 40 MHz support.
	 */
	int (*config_ht)(struct wlan_seam_drv *drv, uint32_t freq,
	    uint32_t width, uint32_t center_freq1);

	/*
	 * The device's own HT capability, encoded as the association request's
	 * HT Capabilities element body (26 bytes: HT Capabilities Info, A-MPDU
	 * Parameters, Supported MCS Set, then the three zeroed capability
	 * groups).  mac80211 builds that element from sband->ht_cap
	 * (net/mac80211/util.c:ieee80211_ie_build_ht_cap) after registration
	 * normalized the SM Power Save field to DISABLED
	 * (net/mac80211/main.c:1360) - the device's own declaration, not a
	 * hand-written one.  The bridge's fall-back table (1x1, HT20, no MPDU
	 * density) is a stand-in for this call being absent.
	 *
	 * Returns 0 when `out` was filled, a negative value when the port has
	 * no HT capability to declare for `freq`'s band (the bridge then keeps
	 * its stand-in).  NULL = no support.
	 */
	int (*local_ht_cap)(struct wlan_seam_drv *drv, uint32_t freq,
	    uint8_t out[26]);
};

#endif /* _WLAN_NET80211_SEAM_H_ */

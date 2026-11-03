/* SPDX-License-Identifier: BSD-3-Clause */
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
 *         why components/net80211/ports/seam.h carries only C scalars, byte arrays and
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

/*
 * What the image calls itself and which firmware its driver loads.  Both are
 * image facts, not bridge facts: an image carries one driver, and these two
 * names belong to that driver.  The values below are the first image built on
 * this bridge (the rtl8xxxu line); another image overrides both on its own
 * compile line, so the bridge never grows a second hardcoded name.  The
 * firmware name must be the one the driver asks for first - rtw89 walks its
 * format numbers downwards from the chip's maximum, so 8851B asks for the -1
 * form before the bare one (fw.c:1006-1010, fw.h:4654-4661).
 */
#ifndef WLAN_BRIDGE_ADAPTER_NAME
#define WLAN_BRIDGE_ADAPTER_NAME	"rtl8xxxu"
#endif
#ifndef WLAN_BRIDGE_FIRMWARE_NAME
#define WLAN_BRIDGE_FIRMWARE_NAME	"rtlwifi/rtl8188eufw.bin"
#endif

/*
 * The auth retry discipline (see wlan_bridge_mgmt_tick): how long to wait for
 * an auth response before re-sending, and how many re-sends to allow - the
 * same shape mac80211 uses (mlme.c:36-40, IEEE80211_AUTH_MAX_TRIES = 3 sends).
 * A port that never retries pays the stack's whole 5 s transaction timer for
 * one dropped frame; this is the difference between a ~200 ms hiccup and a
 * failed join.
 */
#define WLAN_BRIDGE_AUTH_RETRY_MS	200
#define WLAN_BRIDGE_AUTH_MAX_TRIES	2	/* 1 first send + 2 retries */

/* how the last sweep ended (see struct wlan_bridge_counters.scan_close) */
#define WLAN_BRIDGE_SCAN_CLOSE_NONE	0	/* no sweep has run yet */
#define WLAN_BRIDGE_SCAN_CLOSE_STATE	1	/* the stack left S_SCAN */
#define WLAN_BRIDGE_SCAN_CLOSE_WALK	2	/* the walk ran out of channels */
#define WLAN_BRIDGE_SCAN_CLOSE_NOENTRY	3	/* the stack never entered S_SCAN */
#define WLAN_BRIDGE_SCAN_CLOSE_HW	4	/* the device reported its sweep done */

/* the bridge's counters (one snapshot struct; the port shell prints it) */
struct wlan_bridge_counters {
	unsigned int	mgt_tx;
	unsigned int	data_tx;
	/* The driver's backpressure as the bridge saw it: how often a drain was
	 * refused because the driver had stopped its queue (tx_gated), and how
	 * often the driver's wake brought the bridge back (tx_wakes).  A gated
	 * count rising with no wakes is a driver that never recovers; gates
	 * with wakes are the designed pair working. */
	unsigned int	tx_gated;
	unsigned int	tx_wakes;
	/* The seam's held frame and its accounting.  When the seam refuses a
	 * data frame for want of a socket buffer the bridge keeps that one
	 * frame (encapsulated, sealed, in order) instead of freeing it, stops
	 * draining, and lets the next completion reopen the queue.  tx_stash
	 * counts the refusals the held frame waited through, tx_stash_ok the
	 * held frames the seam later took, tx_perm_drop the refusals no wake
	 * can fix (no peer, no key), tx_pending the frame held right now, and
	 * txq_high the deepest the interface queue was seen. */
	unsigned int	tx_stash;
	unsigned int	tx_stash_ok;
	unsigned int	tx_perm_drop;
	unsigned int	tx_pending;
	unsigned int	txq_high;
	unsigned int	rx_frames;
	unsigned int	tx_completes;
	unsigned int	scan_starts;
	unsigned int	scan_completes;
	unsigned int	filter_sets;
	unsigned int	filter_clears;
	/* the management retry family: how many auth requests this attach had to
	 * re-send, and how many answers came back. A join that fails with
	 * auth_retries > 0 and no answer is the "AP heard us and stayed silent"
	 * shape; one with auth_retries == 0 never got a frame out. */
	unsigned int	auth_retries;
	unsigned int	auth_answers;
	/* the discriminator family: which management answers actually cross
	 * the radio, split by kind and by "from the BSSID this join targets",
	 * plus the frames the chip flagged as FCS-failed (the wrong-channel
	 * signature: a parked-off-channel radio receives garbage, a right
	 * channel receives valid beacons). */
	unsigned int	rx_beacon;
	unsigned int	rx_beacon_tgt;
	unsigned int	rx_probersp;
	unsigned int	rx_fcs_bad;
	/* Frames refused because they do not fit the buffer that was to carry
	 * them (rx and tx both allocate through the same helper).  A frame
	 * larger than a legal 802.11 MPDU means the lower layer handed up a
	 * broken length or a misparsed aggregate; it is dropped counted rather
	 * than copied past the mbuf's writable area. */
	unsigned int	oversize_drops;
	/* The stack's own reasons for dropping a received frame, which the stack
	 * counts and this port only reports.  `chanmismatch` is the one that
	 * matters for a sweep: net80211 silently discards a beacon or probe
	 * response whose DS-parameter channel differs from the channel it was
	 * received on (ieee80211_input.c:2213), so a sweep whose radio and whose
	 * channel bookkeeping disagree produces an empty table with every frame
	 * "received". */
	unsigned int	rx_chanmismatch;	/* stack: rx channel mismatch */
	unsigned int	rx_badchan;	/* stack: rx on an inactive channel */
	unsigned int	rx_badbintval;	/* stack: bogus beacon interval */
	/* The two drop reasons that sit *before* the channel checks: a beacon or
	 * probe response that arrives with no scan window up is discarded by the
	 * beacon handler's own state gate (ieee80211_input.c:2092), and one from a
	 * BSSID the interface is not on is discarded by the station address filter
	 * (ieee80211_input.c:567).  A sweep that reports an empty table needs these
	 * to separate "the window did not cover the arrival" from "the frame was
	 * rejected after the window check". */
	unsigned int	rx_mgtdiscard;	/* stack: mgmt frame dropped in the state gate */
	unsigned int	rx_wrongbss;	/* stack: frame from a BSSID we are not on */
	/* The sweep's coverage, and the stack's own verdict on its scan table.
	 * net80211 keys that table on a frame's transmitter address
	 * (ieee80211_add_scan), so a sweep that heard many distinct transmitters
	 * and still recorded one row has a table or allocation fault - and
	 * `rx_nodealloc` is the stack's own counter for a refused entry.  A sweep
	 * that heard a single transmitter never had the chance to record more.
	 * `rx_elem_bad` is the IE parser's own refusals, summed: a beacon whose
	 * elements fail to parse is stored with no SSID and cannot be joined. */
	unsigned int	scan_bss_seen;	/* distinct transmitters the sweep heard */
	unsigned int	scan_bss_seen_ovf;	/* ... beyond the coverage table */
	unsigned int	rx_nodealloc;	/* stack: the scan table refused an entry */
	/* The element parser's refusals, split by kind, because only two of them
	 * end the frame: `missing` (a mandatory element is absent, input.c:2047)
	 * and `toosmall` (an element runs past the frame's end, input.c:2066)
	 * return before the frame reaches the scan table, while `unknown` and
	 * `toobig` only count and continue.  A sweep that heard many transmitters
	 * and recorded nothing has to be told apart between those two families. */
	unsigned int	rx_elem_unknown;	/* stack: element id not handled */
	unsigned int	rx_elem_missing;	/* stack: mandatory element absent */
	unsigned int	rx_elem_toobig;	/* stack: element longer than allowed */
	unsigned int	rx_elem_toosmall;	/* stack: element past the frame end */
	/* The scan table's own high-water mark during the sweep, and its size
	 * when the snapshot is taken.  The pair separates "the sweep never
	 * inserted anything" from "it inserted and the entries went away": a
	 * high-water mark well above the snapshot is a lifetime problem, one
	 * equal to it is an insertion problem. */
	unsigned int	scan_nodes_max;	/* scan-table entries, high-water mark */
	unsigned int	scan_nodes_now;	/* scan-table entries at snapshot time */
	unsigned int	tune_calls;
	unsigned int	tune_fails;
	/* How the last sweep ended.  A sweep whose window was shorter than one
	 * dwell closes with the stack never having entered S_SCAN; a normal one
	 * ends when the walk runs out of channels.  Reported so "rows=0" always
	 * names why the window was empty. */
	unsigned int	scan_entered;	/* sweeps that did reach S_SCAN */
	unsigned int	scan_close;	/* WLAN_BRIDGE_SCAN_CLOSE_* of the last sweep */
	/* The data-plane discriminator family: the protection state of data
	 * frames on the way out and on the way in, plus the stack's own crypto
	 * verdicts. A frame that leaves in the clear, or leaves sealed under a
	 * key id the peer does not index for it (a group-addressed frame under
	 * the pairwise key), is dropped by the peer with no answer; a frame
	 * that arrives and fails to decrypt lands in the crypto counters
	 * instead. The pair says which side of the radio the fault is on. */
	unsigned int	data_tx_enc;	/* data frames sent protected */
	unsigned int	data_tx_plain;	/* data frames sent in the clear */
	unsigned int	tx_noencap;	/* data frames dropped before the radio */
	unsigned int	rx_data;	/* data frames received */
	unsigned int	rx_data_enc;	/* ... that arrived protected */
	unsigned int	rx_data_hwdec;	/* ... the chip already decrypted */
	unsigned int	rx_data_hwdec_prot;	/* ... protected and chip-decrypted */
	unsigned int	rx_wepfail;	/* stack: receive decrypt failed */
	unsigned int	rx_ccmpformat;	/* stack: CCMP header malformed */
	unsigned int	rx_ccmpreplay;	/* stack: CCMP sequence violation */
	unsigned int	rx_ccmpmic;	/* stack: CCMP MIC mismatch */
	unsigned int	tx_nodefkey;	/* stack: no transmit key to seal with */
	unsigned int	tx_errors;	/* interface output errors */
	/* The interface transmit queue's depth and the driver's backpressure
	 * answer at snapshot time (wlan_bridge_stats only; not accumulated):
	 * `txq_len` is how many frames are waiting for the driver, and
	 * `tx_stopped` is non-zero when the driver's queue is stopped, i.e. the
	 * wake (ieee80211_wake_queues -> seam wake -> wlan_bridge_tx_wake) has
	 * not arrived yet.  A rising txq_len with tx_stopped set and tx_wakes
	 * frozen is the "driver stopped its queue and nothing woke it" shape. */
	unsigned int	txq_len;
	unsigned int	tx_stopped;
	/* The block-ack transport the HT/BA round added: the action frames
	 * this bridge sent (ADDBA request/response, DELBA) and the BA/HT
	 * fingerprints a judgment reads.  A downlink that stays at plain ACKs
	 * has mgmt_action_tx == 0 or ba_rx_start == 0; one that negotiated BA
	 * has both rising. */
	unsigned int	mgmt_action_tx;
	unsigned int	ba_rx_start;
	unsigned int	ba_tx_start;
	unsigned int	ba_tx_operational;
	unsigned int	ba_stop;
	unsigned int	ht40_joins;
	unsigned int	ht40_fallbacks;
	unsigned int	reorder_drops;
	uint32_t	tuned_freq;
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
 * driver's signal report (0 when it has none); `fcs_bad` says the chip flagged
 * the frame's checksum (it still arrives - nothing between the driver and here
 * drops it - but it is noise, not an answer); `hw_dec` says the chip already
 * decrypted the frame itself (the driver reports it per frame). The stack is a
 * software-crypto one, so a frame that arrives already decrypted is one the
 * stack will decrypt a second time and fail - which is exactly why the flag
 * crosses this boundary: it is the only way that failure becomes visible.
 *
 * `rx_freq` is the channel the frame was received on, in MHz, as the driver's
 * receive status reports it (0 when the driver reports none).  It is NOT
 * decoration: the imported beacon parser takes the frame's receive channel from
 * the interface's *current* channel and rejects any beacon whose DS-parameter
 * channel differs from it (ieee80211_input.c:2118, :2213).  With a synchronous
 * driver the two coincide; this port's receive path runs on its own thread and
 * a dwell may have moved the current channel before the frame is processed, so
 * the frame's own channel has to travel with it or every frame delivered across
 * a channel change is discarded as off-channel and the scan table stays empty.
 *
 * `device_ts` is the device's own clock at reception (microseconds; 0 when the
 * driver reports none).  It travels with the frame because the TBTT anchor a
 * driver uploads with its MAC context pairs the AP's beacon timestamp with the
 * device clock of that same reception (mac80211's bss->device_ts_beacon ->
 * bss_conf->sync_device_ts; iwlwifi's phy_info->system_timestamp).
 */
int	wlan_bridge_input(const uint8_t *frame, size_t len, int rssi,
	    int fcs_bad, int hw_dec, uint32_t rx_freq, uint32_t device_ts);

/* A transmitted frame completed (the driver half's tx-status callback). */
void	wlan_bridge_tx_complete(void);

/*
 * Whether the transmit path has anything waiting on the driver: a frame the
 * seam refused and the bridge held, or frames still in the interface queue.
 * A completion asks this before it asks for a wake, so a completion with an
 * empty path costs nothing.
 */
int	wlan_bridge_tx_waiting(void);

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

/*
 * The device reported its hardware sweep finished. Only the hw_scan route calls
 * this: there the device owned the dwell, so the bridge's own walk never runs
 * out and this is what closes the window. It is a no-op when no sweep is armed.
 */
void	wlan_bridge_scan_hw_complete(void);

/* Tune the driver's radio to the channel the stack currently sits on (the scan
 * walk advances the stack's channel; the driver must follow). */
int	wlan_bridge_tune_current(void);

/* Diagnostic: hold the receive filter's promiscuous bit without arming a sweep.
 * A join whose answer only arrives while the bit is up is a chip receive-filter
 * fault; one that never arrives either way is not. Test-only. */
int	wlan_bridge_filter_promisc(int on);

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
	/*
	 * The elements are COPIED into the caller's own structure, not borrowed
	 * from the node: the fetch releases the port serializer before it
	 * returns, and a scan that completes or restarts frees the nodes the
	 * pointers would have named.  Measured on the board: reading the
	 * borrowed view while the table churned produced an event stream whose
	 * first entry carried a garbage length - iwlist still coped, the
	 * supplicant parsed the whole buffer as one BSS ("1 BSSes" out of
	 * 4470 bytes) and then never found the WPA2 network.
	 */
	uint8_t	ie_store[512];
	const uint8_t *ies;		/* == ie_store (kept for the readers) */
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
/* The auth retry tick: call from the same place the 1 Hz watchdog is driven
 * from, at a finer cadence (>= 100 ms) - see wlan_bridge_mgmt_tick. */
void wlan_bridge_mgmt_tick(void);

unsigned int wlan_bridge_scan_walk(void (*cb)(const uint8_t bssid[6],
	    const uint8_t *ssid, size_t ssid_len, uint32_t freq, int rssi,
	    void *arg), void *arg);

/* The stack's current state value (the imported numbering), -1 when detached. */
int	wlan_bridge_state(void);

/* Is a sweep in flight? (the stack state and the bookkeeping both standing) */
int	wlan_bridge_scan_active(void);

/* Does the sweep's bookkeeping stand, whatever the stack's state? The dwell
 * pump dwells on THIS one: the dwell's entry check closes a sweep the stack
 * already left (a cancelled scan on the way into a join), so gating the pump
 * on scan_active() alone can strand the bookkeeping - and with it, every
 * later scan_start - on a -EBUSY forever. */
int	wlan_bridge_scan_pending(void);

/* The 1 Hz stack tick: management-frame timeout and node inactivity. Without
 * it a lost auth/assoc response is never retried (the countdown that restarts
 * the scan is driven here). The radio pump calls it once a second. */
void	wlan_bridge_watchdog(void);

/* Diagnostic: the imported path's auth-response-in-wrong-state counter (see
 * the definition; the join judgment reads it to split arrival from discard). */
uint32_t wlan_bridge_stat_bad_auth(void);

/* One ethernet frame from the presentation layer: queued to the interface, so
 * net80211's own encapsulation and crypto run on the way out. */
int	wlan_bridge_tx_raw(const uint8_t *frame, size_t len);

/*
 * Send one raw 802.11 management/action frame (header + body, no FCS) through
 * the same ic_mgtq drain the stack's own management frames use.  This is the
 * transport the block-ack state machine needs: ADDBA Request/Response and
 * DELBA are action frames the imported net80211 stack neither builds nor
 * understands, so the mac80211 substitute hands the finished frame here.
 * Returns the frame length, or a negative error.  Takes the serializer
 * itself; must be called from a sleepable context.
 */
int	wlan_bridge_mgmt_tx(const uint8_t *frame, size_t len);

/*
 * The last 802.11 sequence number the bridge handed the driver for this TID
 * (0 when none was sent).  The block-ack state machine uses last + 1 as the
 * ADDBA start sequence number, which is what mac80211's
 * ieee80211_tx_ba_session_handle_start() does from sta->tid_seq[tid].
 */
uint16_t wlan_bridge_last_tx_seq(int tid);

/* Block-ack fingerprint counters, incremented by the mac80211 substitute
 * when a session changes state.  The numbers are read back by
 * wlan_bridge_stats(); they are the host-side half of the on-air BA count. */
#define WLAN_BRIDGE_BA_RX_START		0
#define WLAN_BRIDGE_BA_TX_START		1
#define WLAN_BRIDGE_BA_TX_OPERATIONAL	2
#define WLAN_BRIDGE_BA_STOP		3
#define WLAN_BRIDGE_BA_REORDER_DROP	4
void	wlan_bridge_ba_note(int evt);

/*
 * The driver's wake notification: its transmit queues have room again, so the
 * bridge may drain what the queue gate held back.  Runs in a work context (a
 * USB completion's work, not the completion itself), takes the port serializer
 * itself, and is a no-op unless the interface is attached and RUN.
 */
void	wlan_bridge_tx_wake(void);

/*
 * The receive half of the data plane: a complete ethernet frame leaves the
 * stack and is handed to the presentation layer. This is the scalar spelling
 * of the port's wlan_port_set_data_rx() so a kernel-world netdev can register
 * without including the net80211 world's headers.
 */
typedef void (*wlan_bridge_data_rx_fn)(const uint8_t *frame, size_t len,
    void *arg);
void	wlan_bridge_set_data_rx(wlan_bridge_data_rx_fn fn, void *arg);

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
 * The port's observation snapshot (see struct wlan_seam_port_obs): what the
 * adapter's transport layer is doing, for a watchdog on the lwIP side.  The
 * bridge only forwards; with a port whose seam has no port_obs, or before
 * attach, the snapshot comes back zeroed.  Safe from any thread, no locks.
 */
struct wlan_seam_port_obs;
void	wlan_bridge_port_obs(struct wlan_seam_port_obs *out);
unsigned int wlan_bridge_port_probe(unsigned int which);
void	wlan_bridge_port_stall_dump(void);
void	wlan_bridge_port_poll(void);

/*
 * The port serializer's state at snapshot time (the splnet() replacement every
 * stack entry takes).  `depth` is the current owner's hold count (0 = free),
 * `owner` the holder's thread name (NULL when free or unnamed) and `held_ms`
 * how long the outermost take has been held.  A frozen data path with depth
 * != 0 names the layer that is stuck; one with depth == 0 puts the fault
 * somewhere that is not waiting on this lock.  Available only when the port
 * was built with CONFIG_NET_WATCH.
 */
struct wlan_bridge_lock_obs {
	unsigned int	depth;
	const char	*owner;
	unsigned int	held_ms;
};
void	wlan_bridge_lock_obs(struct wlan_bridge_lock_obs *out);

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
 * The link face, implemented by the driver half (the receive descriptor and
 * the driver's station object live on the linuxkpi side). `dump` logs one
 * `wlan: link ...` line per bucket; `reset` zeroes the counters so a phase can
 * be measured on its own.
 */
void	wlan_bridge_link_dump(void);
void	wlan_bridge_link_reset(void);

/*
 * The driver half's entry point: installs the linuxkpi-side bridge and the
 * seam's driver handle. Called at start-up by whoever owns the driver (the
 * board's WLAN bring-up, or a host harness). Returns 0 or a negative error.
 */
int	wlan_seam_adapter_init(void);

/*
 * The transmit handoff's cost, one sample per frame the seam hands to the
 * driver. The receive side already had a histogram (`80211 stack`); this is
 * its transmit counterpart, and without both a frame's transmit cost cannot be
 * told apart from a boot's receive cost. Compiled in only with the census
 * (CONFIG_LKPI_CENSUS), like every other instrument here.
 */
void	wlan_seam_tx_census_reset(void);
void	wlan_seam_tx_census_print(void);

#endif /* WLAN_BRIDGE_H */

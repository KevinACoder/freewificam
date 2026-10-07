/*
 * @file    vwifi.h
 * @brief   The virtual driver of the mock suite: TX capture, RX injection and
 *          the scripted AP responder.
 *
 * The stack under test is the imported net80211 component; this file is the
 * harness side of the driver boundary: the stack queues its management frames
 * on ic_mgtq and calls if_start_lock(ifp), which lands in the capture routine
 * below; the responder answers a probe request, an open-system authentication
 * request and an association request. Every frame the responder synthesises is
 * built from the scenario constants of this file only - never from stack state.
 *
 * @design state container - ref net80211:driver/urtwn/if_urtwn.c:515
 * @design registry - ref net80211:driver/urtwn/if_urtwn.c:4873
 */

#ifndef VWIFI_H
#define VWIFI_H

#include <stddef.h>
#include <stdint.h>

struct ieee80211com;
struct ifnet;

/* The "not too real" single-instance world: one AP, one channel, open system. */
#define VWIFI_AP_MAC_BYTES  { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 }
#define VWIFI_STA_MAC_BYTES { 0x02, 0x00, 0x00, 0x00, 0x00, 0x02 }
#define VWIFI_SSID          "vwifi-ap"
#define VWIFI_SSID_LEN      8
#define VWIFI_CHAN_IEEE     1
#define VWIFI_CHAN_FREQ     2412u
#define VWIFI_BINTVAL       100u
#define VWIFI_AID           1u
#define VWIFI_RSSI          58

#define VWIFI_FRAME_MAX     512
#define VWIFI_FIFO_DEPTH    8

struct vwifi_frame {
	size_t len;
	uint8_t data[VWIFI_FRAME_MAX];
};

struct vwifi_stats {
	unsigned tx_probe_req, tx_auth_req, tx_assoc_req, tx_data, tx_other;
	unsigned rx_probe_resp, rx_auth_resp, rx_assoc_resp;
	unsigned data_rx_frames, eapol_rx_frames;
	unsigned ev_scan_done, ev_assoc, ev_disassoc;
	unsigned script_unknown;	/* outside the script: counted, never silent */
	unsigned inject_fail;
};

struct vwifi {
	struct ieee80211com *ic;
	struct ifnet *ifp;

	/* frames captured by the driver's start routine, drained by the harness */
	struct vwifi_frame fifo[VWIFI_FIFO_DEPTH];
	unsigned fifo_head, fifo_tail;

	/* the last captured frames, kept for field assertions */
	struct vwifi_frame last_probe_req;
	struct vwifi_frame last_auth_req;
	struct vwifi_frame last_assoc_req;
	struct vwifi_frame last_data_tx;

	/* the last frames the responder synthesised */
	struct vwifi_frame last_probe_resp;
	struct vwifi_frame last_auth_resp;
	struct vwifi_frame last_assoc_resp;

	/* the presentation hooks' observations */
	struct vwifi_frame last_data_rx;
	struct vwifi_frame last_eapol_rx;	/* payload after the ethernet header */
	uint8_t eapol_src[6];

	struct vwifi_stats stats;
};

void vwifi_init(struct vwifi *sc, struct ieee80211com *ic, struct ifnet *ifp);
void vwifi_install(struct vwifi *sc);

/* Drain one frame captured by the driver; 0 = nothing pending. */
int vwifi_dispatch_one(struct vwifi *sc);

/* Inject one raw 802.11 frame into the stack (takes the port serializer). */
void vwifi_inject(struct vwifi *sc, const uint8_t *frame, size_t len);

/* The responder's frame builders: constants only, no stack state. */
size_t vwifi_build_probe_resp(uint8_t *buf);
size_t vwifi_build_auth_resp(uint8_t *buf);
size_t vwifi_build_assoc_resp(uint8_t *buf);

/* Mirror a STA->AP data frame into the AP->STA direction (the self loop). */
int vwifi_mirror_data(const uint8_t *in, size_t len, uint8_t *out);

#endif /* VWIFI_H */

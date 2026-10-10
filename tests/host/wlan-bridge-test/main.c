/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    main.c
 * @brief   The bridge chain suite: the real net80211 stack, the real bridge (both
 *          halves) and the real mac80211 substitute, driven over a scripted
 *          driver and a fake AP.
 *
 * This file is a linuxkpi-world translation unit: it allocates socket buffers
 * and calls the substitute's face. Everything it needs to know about the
 * net80211 side goes through scalar interfaces only - the port's own seams
 * (events, receive hooks) and this suite's peek ABI (n8_peek.h) - because the
 * two worlds must never share a struct, the same discipline the bridge itself
 * follows by having two halves.
 *
 * Cases (one `WLANBRIDGE: <id> PASS|FAIL <detail>` line each):
 *
 *   B1 attach chain: the substitute's register path builds the net80211 side
 *      (bridge attach, ifattach, the driver's add_interface) and the interface
 *      adopts the driver's address.
 *   B2 scan group ordering: the receive filter is programmed promiscuous BEFORE
 *      the sweep starts and cleared after it (the board-proven dependency). The
 *      `no-filter` fault turns this red on purpose.
 *   B3 scan results and events: the fake AP's probe response lands in the
 *      stack's scan table (BSSID/SSID/channel = the script's truth) and the
 *      scan-done event reaches the presentation side.
 *   B4 join chain: auth and assoc frames leave through the management drain,
 *      the fake AP's answers move the stack to RUN, the association event fires
 *      and the driver is told the BSSID.
 *   B5 EAPOL routing: the port's receive seam classifies an EAPOL frame and
 *      hands the payload behind the 14-byte ethernet header (the offset the
 *      supplicant depends on).
 *   B6 data path: an ethernet frame from the presentation side leaves the
 *      driver's tx as an encapsulated 802.11 data frame; a delivered data frame
 *      arrives at the presentation side whole.
 *   B7 key path: a CCMP key through the bridge reaches the driver's set_key
 *      with index, pairwise flag and material intact.
 *   B8 link closure of the real driver: its device table object is in the link
 *      (its register-level bring-up belongs to the board, where a real chip is a
 *      far better device model than a hand-written register script).
 *   B9 balance and event order: every buffer the chain handed out came back,
 *      the pool's named refusals stayed at zero, the transmit capture lost
 *      nothing, and the sweep's completion arrived before the association. The
 *      `double-free-skb` fault turns this red through the pool's own counter.
 *   B10 presentation layer: the production wlannetif.c over stubbed lwIP -
 *      frames out byte-identical, frames in whole, link events honored.
 */

/* the linuxkpi face first: it carries inline definitions (abs among them) that
 * the host libc's declarations would otherwise precede, and the two cannot
 * coexist in this order (the sibling suites put the face first for the same
 * reason) */
#include "linuxkpi_core.h"

#include <stdarg.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

/* exit() is declared here rather than by <stdlib.h>: the linuxkpi face
 * carries its own static abs() and the two cannot be included together in
 * this order (the same trap the face's first-include rule documents) */
extern void exit(int status);

#include "os/cmsis2/cmsis2.h"
#include <net/mac80211.h>
#include <net/cfg80211.h>
#include <linux/ieee80211.h>

#include "components/linuxkpi/linuxkpi_80211.h"
#include "skbuff/skbuff_pool.h"
#include <linux/usb.h>
#include "ports/net80211/bridge/wlan_bridge.h"

#include "fake_driver.h"
#include "ap_frames.h"
#include "n8_peek.h"
#include "seam_drive.h"
#include "wlnetif_stub.h"

void lkpi_wlan_test_platform_init(void);
int	wlan_seam_adapter_init(void);
void	wlan_osal_init(void);

/* the port's receive/event seams and the bridge's scalar entries (declared here:
 * this file must not include a net80211-world header) */
void	wlan_port_set_eapol_rx(void (*fn)(const uint8_t src[6],
	    const uint8_t *buf, size_t len, void *arg), void *arg);
void	wlan_port_set_data_rx(void (*fn)(const uint8_t *frame, size_t len,
	    void *arg), void *arg);
void	wlan_port_set_event_handler(void (*fn)(int event, const uint8_t *addr,
	    void *arg), void *arg);
int	wlan_bridge_scan_start(void);
int	wlan_bridge_scan_dwell(void);
int	wlan_bridge_up(void);
int	wlan_bridge_tx_raw(const uint8_t *frame, size_t len);
int	wlan_bridge_key_set(int pairwise, int key_idx, int cipher,
	    const uint8_t *key, size_t key_len);
int	wlan_bridge_key_clear(int pairwise, int key_idx);
void	wlan_bridge_set_manual(int on);

/* the supplicant backend instance registered with the port layer */
#include "wpa_vendor_port.h"
int	wlan_wpa_vendor_init(void);

/* the port's event numbering and the seam's cipher numbering, as scalars */
#define WLAN_EV_SCAN_DONE	0
#define WLAN_EV_ASSOC		1
#define WLAN_EV_DISASSOC	2
#define SEAM_CIPHER_CCMP	3

/* ------------------------------------------------------------------ */
/* scaffolding                                                         */
/* ------------------------------------------------------------------ */

static int failures;
static const char *fault_name;
static struct ieee80211_hw *g_hw;

/* the multicast address a group key install carries */
static const uint8_t bcast_addr[6] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

static void
case_line(const char *id, int ok, const char *fmt, ...)
{
	char detail[220];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(detail, sizeof(detail), fmt, ap);
	va_end(ap);
	printf("WLANBRIDGE: %s %s %s\n", id, ok ? "PASS" : "FAIL", detail);
	fflush(stdout);
	if (!ok)
		failures++;
}

/* ------------------------------------------------------------------ */
/* the presentation-side recorders                                     */
/* ------------------------------------------------------------------ */

static struct {
	unsigned int scan_done, assoc, disassoc;
	unsigned int seq;		/* event arrival index (ordering) */
	unsigned int scan_done_at, assoc_at;
	unsigned int eapol_frames, data_frames;
	uint8_t eapol_src[6];
	uint8_t eapol_payload[256];
	unsigned int eapol_len;
	uint8_t data_frame[512];
	unsigned int data_len;
} present;

static void
on_event(int event, const uint8_t *addr, void *arg)
{

	(void)addr;
	(void)arg;
	present.seq++;
	switch (event) {
	case WLAN_EV_SCAN_DONE:
		present.scan_done++;
		present.scan_done_at = present.seq;
		break;
	case WLAN_EV_ASSOC:
		present.assoc++;
		present.assoc_at = present.seq;
		break;
	case WLAN_EV_DISASSOC:
		present.disassoc++;
		break;
	}
}

static void
on_eapol(const uint8_t src[6], const uint8_t *buf, size_t len, void *arg)
{

	(void)arg;
	present.eapol_frames++;
	memcpy(present.eapol_src, src, 6);
	if (len > sizeof(present.eapol_payload))
		len = sizeof(present.eapol_payload);
	memcpy(present.eapol_payload, buf, len);
	present.eapol_len = (unsigned int)len;
}

static void
on_data(const uint8_t *frame, size_t len, void *arg)
{

	(void)arg;
	present.data_frames++;
	if (len > sizeof(present.data_frame))
		len = sizeof(present.data_frame);
	memcpy(present.data_frame, frame, len);
	present.data_len = (unsigned int)len;
}

/* ------------------------------------------------------------------ */
/* handedness helpers                                                  */
/* ------------------------------------------------------------------ */

static const uint8_t sta_mac[6] = { 0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0xee };

/* one received frame, handed the way a driver hands it: an skb carrying the rx
 * status block through the substitute's rx entry. `flags` and `signal` are the
 * status fields the driver would set; RX_FLAG_FAILED_FCS_CRC and
 * RX_FLAG_DECRYPTED are the two the bridge counts. */
static void
inject_rx_flags(const uint8_t *frame, size_t len, unsigned int flags,
    int signal)
{
	struct sk_buff *skb;
	struct ieee80211_rx_status *rs;

	if (g_hw == NULL)
		return;
	skb = lkpi_dev_alloc_skb((unsigned int)len, GFP_ATOMIC);
	if (skb == NULL)
		return;
	memcpy(lkpi_skb_put(skb, (unsigned int)len), frame, len);
	rs = (struct ieee80211_rx_status *)skb->cb;
	memset(rs, 0, sizeof(*rs));
	rs->freq = 2412;
	rs->band = NL80211_BAND_2GHZ;
	rs->signal = (int8_t)signal;
	rs->flag = (uint8_t)flags;
	ieee80211_rx_irqsafe(g_hw, skb);
}

static void
inject_rx(const uint8_t *frame, size_t len)
{
	inject_rx_flags(frame, len, 0, -50);
}

/* the fake AP answers each transmitted frame exactly once, in the order the
 * bridge handed them over (the frame-test suite's rhythm: one dispatch consumes
 * one captured frame). Re-answering an old request would inject a second
 * response into a state machine that already left that state. */
static unsigned int tx_answered;

static void
answer_new_tx_from_ap(void)
{
	const struct fake_driver_state *st = fake_driver_state();

	while (tx_answered < st->tx_count && tx_answered < FAKE_DRIVER_TX_MAX) {
		uint8_t resp[FAKE_DRIVER_FRAME_MAX];
		unsigned int i = tx_answered++;
		size_t rlen = 0;

		if (!ap_answer_to(st->tx_frames[i], st->tx_lens[i], resp, &rlen))
			continue;
		if (fault_name != NULL && strcmp(fault_name, "tamper-auth") == 0 &&
		    (resp[0] & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_AUTH)
			resp[28] = 1;	/* status: unspecified failure */
		inject_rx(resp, rlen);
	}
}

static int
order_index(enum fake_call call)
{
	const struct fake_driver_state *st = fake_driver_state();
	unsigned int i;

	for (i = 0; i < st->order_count && i < FAKE_DRIVER_ORDER_MAX; i++)
		if (st->order[i] == call)
			return (int)i;
	return -1;
}

/* ------------------------------------------------------------------ */
/* the cases                                                           */
/* ------------------------------------------------------------------ */

static int
case_b1(void)
{
	const struct fake_driver_state *st = fake_driver_state();
	uint8_t mac[6];

	if (n8_peek_hwaddr(mac) != 0) {
		case_line("B1", 0, "the interface has no address");
		return -1;
	}
	if (memcmp(mac, sta_mac, 6) != 0) {
		case_line("B1", 0, "the interface did not adopt the driver's "
		    "address (%02x:%02x:%02x:%02x:%02x:%02x)", mac[0], mac[1],
		    mac[2], mac[3], mac[4], mac[5]);
		return -1;
	}
	if (st->add_interface_calls != 1) {
		case_line("B1", 0, "add_interface ran %u times (want 1)",
		    st->add_interface_calls);
		return -1;
	}
	if (st->start_calls < 1) {
		case_line("B1", 0, "the driver's start chain did not run");
		return -1;
	}
	case_line("B1", 1, "attached, address adopted, add_interface+start ran");
	return 0;
}

static int
case_b2(void)
{
	const struct fake_driver_state *st = fake_driver_state();
	int f = order_index(FAKE_CALL_FILTER);
	int s = order_index(FAKE_CALL_SCAN_START);

	if (fault_name != NULL && strcmp(fault_name, "no-filter") == 0) {
		case_line("B2", 0, "red-control: the filter ordering is not in force");
		return -1;
	}
	if (f < 0) {
		case_line("B2", 0, "the receive filter was never programmed");
		return -1;
	}
	if (s < 0) {
		case_line("B2", 0, "the sweep never started");
		return -1;
	}
	if (f > s) {
		case_line("B2", 0, "the promiscuous filter must precede the sweep "
		    "(filter=%d scan=%d)", f, s);
		return -1;
	}
	if (st->filter_completes < 1) {
		case_line("B2", 0, "the filter was never cleared after the sweep");
		return -1;
	}
	if (st->filter_last_total != 0) {
		case_line("B2", 0, "the filter did not go back to zero (total=%#x)",
		    st->filter_last_total);
		return -1;
	}
	/* the channel walk: the driver is told about every channel, ascending,
	 * once each, and the last tune is the settle on the joined AP's channel.
	 * The sweep dwells on 13 channels, so the config sequence is ch1..ch13
	 * plus the join settle. A config after the probe of the same channel
	 * (the old arm-time tune) would leave the sequence doubling back, which
	 * this check refuses by requiring strictly ascending frequencies. */
	{
		unsigned int k;
		uint32_t prev = 0;

		if (st->config_freq_count < 13) {
			case_line("B2", 0, "the driver was tuned %u times; the "
			    "13-channel sweep needs at least 13",
			    st->config_freq_count);
			return -1;
		}
		for (k = 0; k < 13; k++) {
			if (st->config_freqs[k] <= prev) {
				case_line("B2", 0, "the channel walk is not strictly "
				    "ascending at step %u (%u after %u)", k,
				    st->config_freqs[k], prev);
				return -1;
			}
			prev = st->config_freqs[k];
		}
		if (st->config_freqs[12] != 2472) {
			case_line("B2", 0, "the walk does not end on channel 13 "
			    "(freq=%u)", st->config_freqs[12]);
			return -1;
		}
	}
	case_line("B2", 1, "filter(idx=%d) before scan(idx=%d); cleared after; "
	    "channel walk ascending (%u configs, last=%u)",
	    f, s, st->config_freq_count,
	    st->config_freq_count > 0 ?
	    st->config_freqs[st->config_freq_count - 1] : 0);
	return 0;
}

static int
case_b3(void)
{
	uint8_t ssid[40];
	size_t ssid_len = sizeof(ssid);
	uint32_t freq = 0;

	if (n8_peek_scan_table(ap_bssid, ssid, &ssid_len, &freq) != 0) {
		case_line("B3", 0, "the fake AP is not in the scan table");
		return -1;
	}
	if (ssid_len != AP_SSID_LEN || memcmp(ssid, AP_SSID, AP_SSID_LEN) != 0) {
		case_line("B3", 0, "the scan entry's SSID is wrong (%u bytes)",
		    (unsigned)ssid_len);
		return -1;
	}
	if (freq != 2412) {
		case_line("B3", 0, "the scan entry's channel is wrong (freq=%u)",
		    freq);
		return -1;
	}
	if (present.scan_done < 1) {
		case_line("B3", 0, "the scan-done event was not reported");
		return -1;
	}
	/* the sweep's own frame: the probe request the driver was handed carries
	 * the configured SSID (element 0 at offset 24 of the management frame),
	 * so the scripted AP was answered for the SSID the joiner wants, not
	 * for a broadcast probe the case would then not explain */
	{
		const struct fake_driver_state *st = fake_driver_state();
		unsigned int i;
		int seen = 0;

		for (i = 0; i < st->tx_count && i < FAKE_DRIVER_TX_MAX; i++) {
			if (!st->tx_is_mgmt[i] || st->tx_lens[i] < 26)
				continue;
			if ((st->tx_frames[i][0] & IEEE80211_FCTL_STYPE) !=
			    IEEE80211_STYPE_PROBE_REQ)
				continue;
			if (st->tx_frames[i][24] == IEEE80211_ELEMID_SSID &&
			    st->tx_frames[i][25] == AP_SSID_LEN &&
			    memcmp(&st->tx_frames[i][26], AP_SSID,
			    AP_SSID_LEN) == 0) {
				seen = 1;
				break;
			}
		}
		if (!seen) {
			case_line("B3", 0, "no probe request carried the "
			    "configured SSID");
			return -1;
		}
	}
	case_line("B3", 1, "AP in the scan table (ssid=%s ch=2412), scan-done "
	    "seen, probe carried the SSID", AP_SSID);
	return 0;
}

static int
case_b4(void)
{
	const struct fake_driver_state *st = fake_driver_state();

	if (st->tx_mgmt_calls < 3) {
		case_line("B4", 0, "fewer management frames left than the join "
		    "needs (%u)", st->tx_mgmt_calls);
		return -1;
	}
	if (n8_peek_state() != n8_peek_run_state()) {
		case_line("B4", 0, "the stack did not reach RUN (state=%d)",
		    n8_peek_state());
		return -1;
	}
	if (present.assoc < 1) {
		case_line("B4", 0, "the association event was not reported");
		return -1;
	}
	if (st->bss_info_calls < 1 || !st->last_bss_joined) {
		case_line("B4", 0, "the driver was not told about the association");
		return -1;
	}
	if (memcmp(st->last_bssid, ap_bssid, 6) != 0) {
		case_line("B4", 0, "the driver was given the wrong BSSID");
		return -1;
	}
	case_line("B4", 1, "auth+assoc drained, RUN reached, assoc event + bssid");
	return 0;
}

static int
case_b5(void)
{

	if (present.eapol_frames < 1) {
		case_line("B5", 0, "the EAPOL frame was not routed to the hook");
		return -1;
	}
	/* the payload starts behind the 14-byte ethernet header: the scripted body
	 * begins with version 2, type 3 */
	if (present.eapol_len < 4 || present.eapol_payload[0] != 0x02 ||
	    present.eapol_payload[1] != 0x03) {
		case_line("B5", 0, "the EAPOL payload offset is wrong (len=%u "
		    "first=%#x,%#x)", present.eapol_len,
		    present.eapol_len > 0 ? present.eapol_payload[0] : 0,
		    present.eapol_len > 1 ? present.eapol_payload[1] : 0);
		return -1;
	}
	case_line("B5", 1, "EAPOL routed, %u payload bytes behind the header",
	    present.eapol_len);
	return 0;
}

static int
case_b6(void)
{
	const struct fake_driver_state *st = fake_driver_state();
	unsigned int i;
	int found = 0;

	for (i = 0; i < st->tx_count && i < FAKE_DRIVER_TX_MAX; i++) {
		if (st->tx_is_mgmt[i])
			continue;
		if (st->tx_lens[i] >= 24 &&
		    (st->tx_frames[i][0] & IEEE80211_FCTL_FTYPE) ==
		    IEEE80211_FTYPE_DATA) {
			found = 1;
			break;
		}
	}
	if (!found) {
		case_line("B6", 0, "the ethernet frame was not encapsulated into "
		    "an 802.11 data frame");
		return -1;
	}
	if (present.data_frames < 1) {
		case_line("B6", 0, "no data frame arrived at the presentation side");
		return -1;
	}
	case_line("B6", 1, "data out encapsulated (%u tx total), data in "
	    "delivered (%u bytes)", st->tx_count, present.data_len);
	return 0;
}

static int
case_b7(void)
{
	const struct fake_driver_state *st = fake_driver_state();
	uint8_t key[16];
	int ret;

	memset(key, 0, sizeof(key));
	ret = wlan_bridge_key_set(1, 0, SEAM_CIPHER_CCMP, key, sizeof(key));
	if (ret != 0 || st->set_key_calls < 1) {
		case_line("B7", 0, "the key did not reach the driver (ret=%d "
		    "calls=%u)", ret, st->set_key_calls);
		return -1;
	}
	if (st->last_key_len != sizeof(key) || !st->last_key_pairwise ||
	    st->last_key_idx != 0) {
		case_line("B7", 0, "the key crossed with the wrong shape (len=%u "
		    "pairwise=%d idx=%d)", st->last_key_len,
		    st->last_key_pairwise, st->last_key_idx);
		return -1;
	}
	/* install the same material stack-side (what the supplicant does after the
	 * handshake): from here the stack's own encap/crypto decisions apply */
	if (n8_peek_install_ccmp_key(key, sizeof(key)) != 0) {
		case_line("B7", 0, "the stack refused the pairwise key");
		return -1;
	}
	/* the transmit leg: the drain must add the CCMP header and trailer (the
	 * Linux-family driver on the far side does not - mac80211 does) */
	{
		uint8_t payload[16];
		uint8_t frame[64];
		const struct fake_driver_state *fst;
		unsigned int before, i;
		int sealed = 0;

		for (i = 0; i < sizeof(payload); i++)
			payload[i] = (uint8_t)('a' + i);
		memset(frame, 0, sizeof(frame));
		memcpy(frame, ap_bssid, 6);
		memcpy(frame + 6, sta_mac, 6);
		frame[12] = 0x08;
		frame[13] = 0x00;
		memcpy(frame + 14, payload, sizeof(payload));
		before = st->tx_count;
		if (wlan_bridge_tx_raw(frame, 14 + sizeof(payload)) < 0) {
			case_line("B7", 0, "the protected transmit was refused");
			return -1;
		}
		fst = fake_driver_state();
		for (i = before; i < fst->tx_count && i < FAKE_DRIVER_TX_MAX; i++) {
			uint8_t *fp = fst->tx_frames[i];
			unsigned int len = fst->tx_lens[i];

			/* header + CCMP header + LLC + payload + MIC */
			if (len != 24 + 8 + 8 + sizeof(payload) + 8)
				continue;
			if ((fp[1] & 0x40) == 0)		/* protected bit */
				continue;
			if (memcmp(fp + 40, payload, sizeof(payload)) == 0)
				continue;			/* still plaintext */
			sealed = 1;
			break;
		}
		if (!sealed) {
			case_line("B7", 0, "no sealed frame left the driver "
			    "(waiting: len=%u+32 protected, ciphertext)",
			    (unsigned)(14 + sizeof(payload)));
			return -1;
		}
	}
	/* the receive leg: the peer seals a frame; the stack must deliver the
	 * plaintext whole. Then a corrupted MIC must be rejected by name. */
	{
		uint8_t payload[16];
		uint8_t sealed[128];
		size_t slen = sizeof(sealed);
		uint8_t expect[64];
		size_t elen = 0;
		unsigned int before, mic_before, i;
		int rx_ok, mic_ok;

		for (i = 0; i < sizeof(payload); i++)
			payload[i] = (uint8_t)('A' + i);
		if (n8_peek_seal_from_ap(payload, sizeof(payload), 4, sealed,
		    &slen) != 0) {
			case_line("B7", 0, "the peer could not seal a frame");
			return -1;
		}
		elen = 14 + sizeof(payload);
		memcpy(expect, sta_mac, 6);		/* dst: the station */
		memcpy(expect + 6, ap_bssid, 6);	/* src: the AP (A3) */
		expect[12] = 0x08;
		expect[13] = 0x00;
		memcpy(expect + 14, payload, sizeof(payload));

		before = present.data_frames;
		present.data_len = 0;
		inject_rx(sealed, slen);
		rx_ok = present.data_frames == before + 1 &&
		    present.data_len == elen &&
		    memcmp(present.data_frame, expect, elen) == 0;

		/* a second, freshly sealed frame with one bit flipped inside the
		 * MIC: rejected by name, nothing delivered */
		slen = sizeof(sealed);
		if (n8_peek_seal_from_ap(payload, sizeof(payload), 5, sealed,
		    &slen) != 0) {
			case_line("B7", 0, "the second peer seal failed");
			return -1;
		}
		sealed[slen - 1] ^= 0x01;
		before = present.data_frames;
		mic_before = n8_peek_ccmpmic_count();
		inject_rx(sealed, slen);
		mic_ok = present.data_frames == before &&
		    n8_peek_ccmpmic_count() == mic_before + 1;

		if (!rx_ok) {
			case_line("B7", 0, "the sealed frame did not arrive as "
			    "plaintext (frames=%u->%u len=%u)", before,
			    present.data_frames, present.data_len);
			return -1;
		}
		if (!mic_ok) {
			case_line("B7", 0, "the broken MIC was not rejected by "
			    "name (frames=%u mic=%u)", present.data_frames,
			    n8_peek_ccmpmic_count());
			return -1;
		}
	}
	n8_peek_clear_privacy();
	case_line("B7", 1, "CCMP key to both sides; tx sealed (24+8+8+%u+8); "
	    "rx plaintext; broken MIC named", (unsigned)16);
	return 0;
}

/* the real driver's public register-access entry: a definite symbol that exists
 * only in the extracted core.c, so a successful link proves the unit is in the
 * image (the driver's whole unit set is needed by its device table) */
struct rtl8xxxu_priv;
u8 rtl8xxxu_read8(struct rtl8xxxu_priv *priv, u16 addr);

static int
case_b8(void)
{

	if (rtl8xxxu_read8 == NULL) {
		case_line("B8", 0, "the driver's core unit is not linked");
		return -1;
	}
	case_line("B8", 1, "driver core linked (rtl8xxxu_read8 @%p)",
	    (const void *)rtl8xxxu_read8);
	return 0;
}

/*
 * The closing case: every resource the chain handed out came back, the named
 * refusals stayed at zero, and the events arrived in the order the presentation
 * layer depends on (the sweep's completion before the association). A double
 * free planted in the driver's transmit path must turn this red through the
 * pool's own named counter - that is what the pool's counters are for.
 */
static int
case_b9(void)
{
	const struct fake_driver_state *st = fake_driver_state();
	struct lkpi_skb_pool_stats ps;

	memset(&ps, 0, sizeof(ps));
	lkpi_skb_pool_stats(&ps);
	lkpi_skb_fence_scan();

	if (st->tx_lost != 0) {
		case_line("B9", 0, "the transmit capture dropped %u frames",
		    st->tx_lost);
		return -1;
	}
	if (present.scan_done < 1 || present.assoc < 1) {
		case_line("B9", 0, "event order cannot be judged (scan=%u assoc=%u)",
		    present.scan_done, present.assoc);
		return -1;
	}
	if (present.assoc_at < present.scan_done_at) {
		case_line("B9", 0, "the association event arrived before the "
		    "sweep's completion (scan@%u assoc@%u)",
		    present.scan_done_at, present.assoc_at);
		return -1;
	}
	if (ps.n_alloc != ps.n_free || ps.skb_used != 0 || ps.data_used != 0 ||
	    ps.dyalloc_live != 0) {
		case_line("B9", 0, "the pool did not come back to zero "
		    "(a=%u f=%u used=%u data=%u heap=%u/%u)", ps.n_alloc,
		    ps.n_free, ps.skb_used, ps.data_used, ps.dyalloc_live,
		    ps.dyalloc_oversize);
		return -1;
	}
	if (ps.double_free != 0 || ps.stray_free != 0 || ps.uaf_write != 0 ||
	    ps.fence_bad != 0) {
		case_line("B9", 0, "named refusals fired (df=%u sf=%u uaf=%u "
		    "fence=%u)", ps.double_free, ps.stray_free, ps.uaf_write,
		    ps.fence_bad);
		return -1;
	}
	if (ps.alloc_fail != 0) {
		case_line("B9", 0, "allocation failures occurred (%u)",
		    ps.alloc_fail);
		return -1;
	}
	if (fault_name != NULL && strcmp(fault_name, "double-free-skb") == 0) {
		/* a backstop: the named-refusal check above must already have
		 * caught the planted double release; reaching here means the
		 * pool did not see it */
		case_line("B9", 0, "red-control: the planted double release did not "
		    "reach the pool's counter");
		return -1;
	}
	case_line("B9", 1, "pool balanced (a=f=%u), named refusals zero, "
	    "tx_lost=0, scan-done before assoc", ps.n_alloc);
	return 0;
}

/* the presentation layer's test frame: the same bytes its stub sends */
static const uint8_t eth_ap_hdr[14] = {
	0x02, 0x11, 0x22, 0x33, 0x44, 0x55,
	0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0xee,
	0x08, 0x00
};
static const uint8_t eth_payload_marker[16] = {
	'w', 'l', 'a', 'n', 'n', 'e', 't', 'i',
	'f', '-', 's', 't', 'u', 'b', 0, 0
};

/*
 * The presentation layer (the design's A9 half that a host test can carry):
 * the production ports/lwip/wlannetif.c over stubbed lwIP but over the REAL
 * bridge, so the layer runs inside the chain. The frame it emits through
 * netif->linkoutput must appear in the scripted driver's capture (net80211's
 * own encapsulation in between), a frame the stack delivers must reach the
 * netif's input whole, the link events must move the netif's link state, and
 * the layer's own counters must agree with what happened.
 */
static int
case_b10(void)
{
	struct wlnetif_stub_result r;
	const struct fake_driver_state *st;
	uint8_t mac[6];
	uint8_t expect[14 + 16];
	unsigned int before, i, found;

	if (wlnetif_stub_assemble(mac) != 0) {
		case_line("B10", 0, "the netif did not assemble over the bridge");
		return -1;
	}
	if (memcmp(mac, sta_mac, 6) != 0) {
		case_line("B10", 0, "the netif did not adopt the bridge's "
		    "address (%02x:%02x:%02x:%02x:%02x:%02x)", mac[0], mac[1],
		    mac[2], mac[3], mac[4], mac[5]);
		return -1;
	}
	/* out: linkoutput -> bridge -> net80211 encap -> driver capture */
	memcpy(expect, eth_ap_hdr, 14);
	memcpy(expect + 14, eth_payload_marker, 16);
	before = fake_driver_state()->tx_count;
	if (wlnetif_stub_linkoutput() != 0) {
		case_line("B10", 0, "the netif's linkoutput failed");
		return -1;
	}
	st = fake_driver_state();
	found = 0;
	for (i = before; i < st->tx_count && i < FAKE_DRIVER_TX_MAX; i++) {
		if (!st->tx_is_mgmt[i] &&
		    (st->tx_frames[i][0] & IEEE80211_FCTL_FTYPE) ==
		    IEEE80211_FTYPE_DATA) {
			found = 1;
			break;
		}
	}
	if (!found) {
		case_line("B10", 0, "the frame from the netif did not reach the "
		    "driver");
		return -1;
	}

	/* in: a frame delivered through the port's own seam must reach the
	 * netif's input whole */
	if (seam_drive_frame(expect, 14 + 16) != 0) {
		case_line("B10", 0, "the receive delivery failed");
		return -1;
	}
	wlnetif_stub_get(&r);
	if (r.rx_input_frames != 1 ||
	    memcmp(r.rx_seen, expect, 14 + 16) != 0) {
		case_line("B10", 0, "the delivered frame did not reach the netif "
		    "input whole (%u frames)", r.rx_input_frames);
		return -1;
	}

	/* events: the port's own routing entry maps them onto the layer; each
	 * note is taken while that event's state is the current one */
	if (seam_drive_event(SEAM_DRIVE_EV_ASSOC, ap_bssid) != 0) {
		case_line("B10", 0, "the assoc event delivery failed");
		return -1;
	}
	wlnetif_stub_note_link_up();
	if (seam_drive_event(SEAM_DRIVE_EV_DISASSOC, ap_bssid) != 0) {
		case_line("B10", 0, "the disassoc event delivery failed");
		return -1;
	}
	wlnetif_stub_note_link_down();

	wlnetif_stub_up();
	wlnetif_stub_down();
	wlnetif_stub_get(&r);

	if (r.link_up_seen < 1 || r.link_down_seen < 1 ||
	    !r.link_assoc_flag || !r.link_down_flag || !r.up_flag ||
	    !r.down_flag) {
		case_line("B10", 0, "the link/up state did not follow the events "
		    "(seenz=%u/%u assoc=%u down=%u up=%u stopped=%u)",
		    r.link_up_seen, r.link_down_seen, r.link_assoc_flag,
		    r.link_down_flag, r.up_flag, r.down_flag);
		return -1;
	}
	if (r.dhcp_started < 1 || r.dhcp_stopped < 1) {
		case_line("B10", 0, "the up/down path did not reach the DHCP "
		    "client (%u/%u)", r.dhcp_started, r.dhcp_stopped);
		return -1;
	}
	if (r.diag_rx != 1 || r.diag_tx != 1 || r.diag_rx_fail != 0 ||
	    r.diag_tx_fail != 0) {
		case_line("B10", 0, "the layer's counters disagree with the "
		    "scenario (rx=%u/%u tx=%u/%u)", r.diag_rx, r.diag_rx_fail,
		    r.diag_tx, r.diag_tx_fail);
		return -1;
	}
	case_line("B10", 1, "wlannetif over the real bridge: frame out reached "
	    "the driver, rx whole, link/up events honored, counters %u/%u",
	    r.diag_rx, r.diag_tx);
	return 0;
}

/* ------------------------------------------------------------------ */
/* B11: the supplicant backend instance over the real bridge            */
/* ------------------------------------------------------------------ */

static unsigned int b11_scan_done;
static unsigned int b11_assoc;
static unsigned int b11_disassoc;

static void
b11_event(int event, const uint8_t *addr, const uint8_t *buf, size_t len,
    void *arg)
{

	(void)addr;
	(void)buf;
	(void)len;
	(void)arg;
	switch (event) {
	case 1:		/* WPA_VENDOR_EVENT_SCAN_DONE */
		b11_scan_done++;
		break;
	case 2:		/* WPA_VENDOR_EVENT_ASSOC */
		b11_assoc++;
		break;
	case 3:		/* WPA_VENDOR_EVENT_DISASSOC */
		b11_disassoc++;
		break;
	}
}

/* the WPA2-PSK/CCMP RSN IE the supplicant would hand down */
static const uint8_t b11_rsn_ie[22] = {
	0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
	0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00,
	0x00, 0x0f, 0xac, 0x02, 0x00, 0x00
};

static const uint8_t b11_tk[16] = {
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
	0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00
};

static const uint8_t b11_gtk[16] = {
	0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
	0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf
};

/* does any captured frame of this subtype carry the byte string? (the assoc
 * request must carry the supplicant's RSN IE verbatim) */
static int
b11_capture_contains_mgmt(uint8_t subtype, const uint8_t *needle, size_t n)
{
	const struct fake_driver_state *st = fake_driver_state();
	unsigned int f, i;

	for (f = 0; f < st->tx_count && f < FAKE_DRIVER_TX_MAX; f++) {
		if (!st->tx_is_mgmt[f] || st->tx_lens[f] < 24)
			continue;
		if ((st->tx_frames[f][0] & IEEE80211_FCTL_STYPE) != subtype)
			continue;
		for (i = 24; i + n <= st->tx_lens[f]; i++) {
			if (memcmp(&st->tx_frames[f][i], needle, n) == 0)
				return 1;
		}
	}
	return 0;
}

/* did a data frame leave carrying the EAPOL ethertype behind LLC/SNAP? */
static int
b11_capture_eapol_out(void)
{
	const struct fake_driver_state *st = fake_driver_state();
	unsigned int f, i;

	for (f = 0; f < st->tx_count && f < FAKE_DRIVER_TX_MAX; f++) {
		if (st->tx_is_mgmt[f] || st->tx_lens[f] < 24 + 8)
			continue;
		for (i = 24; i + 2 <= st->tx_lens[f]; i++) {
			if (st->tx_frames[f][i] == 0x88 &&
			    st->tx_frames[f][i + 1] == 0x8e)
				return 1;
		}
	}
	return 0;
}

/*
 * The supplicant backend instance (the nine-op contract over the real bridge):
 * ensure_up puts the stack in the supplicant's mode, the survey fills the pull
 * model, the join takes the supplicant's own RSN IE, the two key installs land
 * on both legs, the EAPOL exit reaches the data path, and the event pump feeds
 * the presentation layer's forwarding.
 */
static int
case_b11(void)
{
	const struct wpa_vendor_port *vp;
	const struct fake_driver_state *st;
	struct wpa_scan_res_ret row;
	struct wpa_vendor_conn conn;
	struct wpa_vendor_key key;
	uint8_t own[6];
	uint8_t eapol[20];
	unsigned int guard, set_key_before, clear_key_before;
	int rc;

	if (wlan_wpa_vendor_init() != 0) {
		case_line("B11", 0, "the backend did not register");
		return -1;
	}
	vp = wpa_vendor_backend();
	if (vp == NULL || vp->ensure_up == NULL || vp->connect == NULL) {
		case_line("B11", 0, "the registered table is incomplete");
		return -1;
	}
	vp->set_event_cb(b11_event, NULL);

	if (vp->ensure_up() != 0) {
		case_line("B11", 0, "ensure_up failed");
		return -1;
	}
	if (fault_name != NULL && strcmp(fault_name, "no-manual") == 0) {
		/* red control: the backend forgot the MANUAL switch, so the
		 * stack's own matcher would race the supplicant's decision */
		wlan_bridge_set_manual(0);
	}
	if (n8_peek_roaming() != 2) {	/* IEEE80211_ROAMING_MANUAL */
		case_line("B11", 0, "ensure_up did not put the stack in MANUAL "
		    "roaming (mode=%d)", n8_peek_roaming());
		return -1;
	}
	if (vp->get_hwaddr(own) != 0 || memcmp(own, sta_mac, 6) != 0) {
		case_line("B11", 0, "get_hwaddr did not report the interface "
		    "address");
		return -1;
	}

	/* the survey: the pull model must answer with the AP the scripted peer
	 * advertised, and MANUAL must leave the stack un-joined afterwards */
	if (vp->scan_start((const uint8_t *)AP_SSID, AP_SSID_LEN) != 0) {
		case_line("B11", 0, "scan_start failed");
		return -1;
	}
	for (guard = 0; guard < 64; guard++) {
		answer_new_tx_from_ap();
		if (wlan_bridge_scan_dwell() == 0)
			break;
	}
	answer_new_tx_from_ap();
	if (b11_scan_done < 1) {
		case_line("B11", 0, "the scan-complete event did not arrive "
		    "through the backend's pump");
		return -1;
	}
	if (n8_peek_state() == n8_peek_run_state()) {
		case_line("B11", 0, "MANUAL roaming still joined automatically "
		    "(state=RUN)");
		return -1;
	}
	if (vp->scan_fetch(0, &row) != 0) {
		case_line("B11", 0, "scan_fetch(0) returned nothing");
		return -1;
	}
	if (memcmp(row.bssid, ap_bssid, 6) != 0 || row.ssid_len != AP_SSID_LEN ||
	    memcmp(row.ssid, AP_SSID, AP_SSID_LEN) != 0 || row.freq != 2412 ||
	    row.ies_len < 2 || row.ies[0] != 1) {
		case_line("B11", 0, "the fetched row does not describe the AP "
		    "(ssid_len=%u freq=%d ies=%u)", (unsigned)row.ssid_len,
		    row.freq, (unsigned)row.ies_len);
		return -1;
	}
	if (vp->scan_fetch(1, &row) == 0) {
		case_line("B11", 0, "scan_fetch past the end did not stop the "
		    "iteration");
		return -1;
	}

	/* the join: the supplicant's own RSN IE, the privacy/802.1X policy it
	 * implies, and the assoc request that carries it */
	memset(&conn, 0, sizeof(conn));
	conn.ssid = (const uint8_t *)AP_SSID;
	conn.ssid_len = AP_SSID_LEN;
	conn.bssid = ap_bssid;
	conn.bssid_set = 1;
	conn.wpa_ie = b11_rsn_ie;
	conn.wpa_ie_len = sizeof(b11_rsn_ie);
	if (vp->connect(&conn) != 0) {
		case_line("B11", 0, "connect refused the join");
		return -1;
	}
	for (guard = 0; guard < 64; guard++) {
		if (n8_peek_state() == n8_peek_run_state())
			break;
		answer_new_tx_from_ap();
	}
	if (n8_peek_state() != n8_peek_run_state()) {
		case_line("B11", 0, "the join did not reach RUN (state=%d)",
		    n8_peek_state());
		return -1;
	}
	if (b11_assoc < 1) {
		case_line("B11", 0, "the association event did not arrive "
		    "through the backend's pump");
		return -1;
	}
	if (!n8_peek_privacy()) {
		case_line("B11", 0, "the join left the privacy flag unset");
		return -1;
	}
	if (n8_peek_authmode() != n8_peek_authmode_8021x()) {
		case_line("B11", 0, "the join did not put the node under 802.1X "
		    "(authmode=%d)", n8_peek_authmode());
		return -1;
	}
	if (!b11_capture_contains_mgmt(IEEE80211_STYPE_ASSOC_REQ, b11_rsn_ie,
		sizeof(b11_rsn_ie))) {
		case_line("B11", 0, "the assoc request did not carry the "
		    "supplicant's RSN IE");
		return -1;
	}

	/* EAPOL out before any key: the frame leaves through the one data path
	 * and the ethertype survives the encapsulation */
	memset(eapol, 0, sizeof(eapol));
	eapol[0] = 0x02;
	eapol[1] = 0x03;
	for (guard = 0; guard < sizeof(eapol) - 2; guard++)
		eapol[2 + guard] = (uint8_t)guard;
	/* the contract's success shape: >= 0 (the l2_packet carrier turns the
	 * non-negative return into the byte count) */
	if (vp->eapol_xmit(ap_bssid, eapol, sizeof(eapol)) < 0) {
		case_line("B11", 0, "eapol_xmit failed");
		return -1;
	}
	if (!b11_capture_eapol_out()) {
		case_line("B11", 0, "the EAPOL frame did not reach the data "
		    "path");
		return -1;
	}

	/* the two key installs: clear the slot first so the transition is real,
	 * then verify both legs. The driver leg is asserted ABSENT: the port
	 * never advertises a hardware cipher, so the stack's key is
	 * software-crypto and net80211's own model keeps such a key out of the
	 * driver (FreeBSD calls the driver's key hook only for hardware keys).
	 * Handing it over anyway makes the chip decrypt received frames, and the
	 * stack then decrypts the plaintext a second time and fails the MIC -
	 * every protected frame, DHCP's offers included, dies there. */
	st = fake_driver_state();
	clear_key_before = fake_driver_state()->clear_key_calls;
	set_key_before = st->set_key_calls;
	(void)wlan_bridge_key_clear(1, 0);
	if (fake_driver_state()->clear_key_calls != clear_key_before) {
		case_line("B11", 0, "the driver's key clear ran (+%u calls) for a "
		    "key the chip never held",
		    fake_driver_state()->clear_key_calls - clear_key_before);
		return -1;
	}
	if (n8_peek_key_defined(1, 0) != 0) {
		case_line("B11", 0, "the pairwise slot did not clear");
		return -1;
	}
	memset(&key, 0, sizeof(key));
	key.alg = WPA_VENDOR_ALG_CCMP;
	key.key_idx = 0;
	key.addr = ap_bssid;		/* unicast: pairwise */
	key.set_tx = 1;
	key.key = b11_tk;
	key.key_len = sizeof(b11_tk);
	rc = vp->set_key(&key);
	if (rc != 0 || n8_peek_key_defined(1, 0) != 1 ||
	    n8_peek_node_authorized() != 1) {
		case_line("B11", 0, "the pairwise install did not land on both "
		    "legs (rc=%d defined=%d auth=%d)", rc,
		    n8_peek_key_defined(1, 0), n8_peek_node_authorized());
		return -1;
	}
	if (fake_driver_state()->set_key_calls != set_key_before) {
		case_line("B11", 0, "a software-crypto key reached the driver's "
		    "CAM (+%u calls): the chip would decrypt received frames and "
		    "the stack would decrypt them again",
		    fake_driver_state()->set_key_calls - set_key_before);
		return -1;
	}

	set_key_before = fake_driver_state()->set_key_calls;
	memset(&key, 0, sizeof(key));
	key.alg = WPA_VENDOR_ALG_CCMP;
	key.key_idx = 1;
	key.addr = bcast_addr;		/* multicast bit: group key */
	key.set_tx = 1;
	key.key = b11_gtk;
	key.key_len = sizeof(b11_gtk);
	rc = vp->set_key(&key);
	if (rc != 0 || n8_peek_key_defined(0, 1) != 1 ||
	    n8_peek_def_txkey() != 1) {
		case_line("B11", 0, "the group install did not land (rc=%d "
		    "defined=%d def_tx=%d)", rc, n8_peek_key_defined(0, 1),
		    n8_peek_def_txkey());
		return -1;
	}
	if (fake_driver_state()->set_key_calls != set_key_before) {
		case_line("B11", 0, "a software-crypto group key reached the "
		    "driver's CAM (+%u calls)",
		    fake_driver_state()->set_key_calls - set_key_before);
		return -1;
	}

	/* the presentation layer's forwarding through the backend's slot */
	{
		struct wlnetif_stub_result r;

		if (wlnetif_stub_assemble(NULL) != 0) {
			case_line("B11", 0, "the netif did not assemble for the "
			    "forwarding check");
			return -1;
		}
		wlnetif_stub_bind_wpa();
		if (seam_drive_event(SEAM_DRIVE_EV_ASSOC, ap_bssid) != 0) {
			case_line("B11", 0, "the assoc delivery failed");
			return -1;
		}
		wlnetif_stub_note_link_up();
		wlnetif_stub_get(&r);
		if (r.link_up_seen < 1 || !r.link_assoc_flag) {
			case_line("B11", 0, "the backend did not forward the "
			    "association to the netif (seenz=%u flag=%u)",
			    r.link_up_seen, r.link_assoc_flag);
			return -1;
		}
	}

	/* disconnect: the stack leaves RUN and the event pump reports it */
	vp->disconnect(3);
	if (n8_peek_state() == n8_peek_run_state()) {
		case_line("B11", 0, "disconnect did not leave the RUN state");
		return -1;
	}

	case_line("B11", 1, "backend ops verified: MANUAL sweep + pull table, "
	    "RSN join + 802.1X gate, CCMP install stack-side only (the chip is "
	    "not asked to decrypt), 0x888E "
	    "exit, event pump forwarded to the netif");
	return 0;
}


/* ------------------------------------------------------------------ */
/* the board-feedback cases (r13/r14): the second sweep, the mid-sweep
 * join race, and the association wiring the driver actually reads           */
/* ------------------------------------------------------------------ */

/* how many probe requests carrying the configured SSID the driver was handed
 * so far (the sweep's own footprint; a sweep that did not probe adds none) */
static unsigned int
count_ssid_probes(void)
{
	const struct fake_driver_state *st = fake_driver_state();
	unsigned int i, n = 0;

	for (i = 0; i < st->tx_count && i < FAKE_DRIVER_TX_MAX; i++) {
		if (!st->tx_is_mgmt[i] || st->tx_lens[i] < 26)
			continue;
		if ((st->tx_frames[i][0] & IEEE80211_FCTL_STYPE) !=
		    IEEE80211_STYPE_PROBE_REQ)
			continue;
		if (st->tx_frames[i][24] == IEEE80211_ELEMID_SSID &&
		    st->tx_frames[i][25] == AP_SSID_LEN &&
		    memcmp(&st->tx_frames[i][26], AP_SSID, AP_SSID_LEN) == 0)
			n++;
	}
	return n;
}

/* one whole sweep, the pump's own rhythm: answer what the stack sent while
 * the channel is still current, then hand the stack its next channel */
static void
run_one_sweep(void)
{
	unsigned int guard;

	for (guard = 0; guard < 64; guard++) {
		answer_new_tx_from_ap();
		if (wlan_bridge_scan_dwell() == 0)
			break;
	}
	answer_new_tx_from_ap();
}

/*
 * B12: a sweep started while the stack already sits in its SCAN state (the
 * MANUAL-roaming park) must still probe. The stack arms its active-scan flag
 * only in begin_scan (the INIT -> SCAN entry); end_scan clears it; so the
 * bridge's scan_start is the only thing that re-arms a parked stack - without
 * it every sweep after the first walks the channels in silence (board r14:
 * the supplicant's scans kept "completing" over a stale table while no probe
 * left the radio).
 */
static int
case_b12(void)
{
	unsigned int before = count_ssid_probes();
	struct wlan_bridge_counters cb;
	unsigned int done_before;
	int rc;

	wlan_bridge_stats(&cb);
	done_before = cb.scan_completes;
	rc = wlan_bridge_scan_start();
	if (rc != 0) {
		case_line("B12", 0, "the first sweep did not start (rc=%d)", rc);
		return -1;
	}
	run_one_sweep();
	if (count_ssid_probes() == before) {
		case_line("B12", 0, "the first sweep transmitted no probe");
		return -1;
	}

	/* the sweep ended; MANUAL roaming parks the stack in SCAN */
	before = count_ssid_probes();
	if (wlan_bridge_scan_start() != 0) {
		case_line("B12", 0, "the second scan_start was refused (the "
		    "sweep bookkeeping did not close)");
		return -1;
	}
	if (fault_name != NULL && strcmp(fault_name, "no-rearm") == 0)
		n8_poke_clear_active_scan();	/* red control: the old state */
	run_one_sweep();
	/* the threshold is the sweep, not the re-entry: scan_start's own
	 * state re-entry emits one probe even with the flags cleared, so a
	 * parked re-arm that failed would still show +1; a working re-arm
	 * probes every dwelled channel */
	if (count_ssid_probes() < before + 4) {
		case_line("B12", 0, "the second sweep barely probed (+%u): the "
		    "parked stack was not re-armed",
		    count_ssid_probes() - before);
		return -1;
	}
	wlan_bridge_stats(&cb);
	if (cb.scan_completes <= done_before + 1) {
		case_line("B12", 0, "the second sweep did not close "
		    "(completes=%u)", cb.scan_completes);
		return -1;
	}
	case_line("B12", 1, "a parked (SCAN-state) re-sweep still probes "
	    "(+%u probes, scan-done reported)",
	    count_ssid_probes() - before);
	return 0;
}

/*
 * B13: a join that lands while a sweep is mid-flight must not strand the
 * sweep's bookkeeping. The pump's dwell is what closes a sweep the stack
 * already left (its entry check); a stranded `scanning` flag refuses every
 * later scan_start with -EBUSY - the board's structural hazard.
 */
static int
case_b13(void)
{
	int rc;

	if (wlan_bridge_scan_start() != 0) {
		case_line("B13", 0, "the mid-flight sweep did not start");
		return -1;
	}
	answer_new_tx_from_ap();
	if (wlan_bridge_scan_dwell() != 1) {
		case_line("B13", 0, "the dwell did not arm a second channel");
		return -1;
	}
	/* the supplicant's move: join over the live sweep (open network) */
	if (wlan_bridge_join((const uint8_t *)AP_SSID, AP_SSID_LEN, ap_bssid,
	    NULL, 0) != 0) {
		case_line("B13", 0, "the mid-sweep join was refused");
		return -1;
	}
	{
		unsigned int guard;

		for (guard = 0; guard < 64; guard++) {
			if (n8_peek_state() == n8_peek_run_state())
				break;
			answer_new_tx_from_ap();
		}
		if (n8_peek_state() != n8_peek_run_state()) {
			case_line("B13", 0, "the mid-sweep join did not reach "
			    "RUN (state=%d)", n8_peek_state());
			return -1;
		}
	}
	/* the pump's next dwell: the stack left S_SCAN, so this must close the
	 * sweep (return 0) rather than drag the state machine back */
	if (wlan_bridge_scan_dwell() != 0) {
		case_line("B13", 0, "a dwell after the join tried to walk the "
		    "sweep instead of closing it");
		return -1;
	}
	if (wlan_bridge_scan_pending() != 0) {
		case_line("B13", 0, "the closed sweep still reads pending");
		return -1;
	}
	rc = wlan_bridge_scan_start();
	if (rc != 0) {
		case_line("B13", 0, "scan_start after the mid-sweep join was "
		    "refused (rc=%d; the bookkeeping stranded)", rc);
		return -1;
	}
	case_line("B13", 1, "mid-sweep join: dwell closes the sweep, state "
	    "reaches RUN, scan_start recovers (no stranded EBUSY)");
	return 0;
}

/*
 * B14: the association wiring a mac80211 driver reads. The adapter's
 * bss_info must carry vif->cfg.assoc/aid and the station's rate bitmaps on
 * the join leg - the driver's association branch is gated on cfg.assoc and
 * builds its rate mask from the station (without them it runs its
 * NOT-associated leg while the stack has associated). The leave leg must not
 * push a BSSID change with a NULL address (the driver dereferences it).
 */
static int
case_b14(void)
{
	const struct fake_driver_state *st = fake_driver_state();
	unsigned int ev_join = st->bss_ev_count, ev_leave;
	unsigned int guard;

	/* finish whatever sweep B13 armed, then join again under this case's
	 * own eyes */
	run_one_sweep();
	if (wlan_bridge_join((const uint8_t *)AP_SSID, AP_SSID_LEN, ap_bssid,
	    NULL, 0) != 0) {
		case_line("B14", 0, "the join was refused");
		return -1;
	}
	for (guard = 0; guard < 64; guard++) {
		if (n8_peek_state() == n8_peek_run_state())
			break;
		answer_new_tx_from_ap();
	}
	if (n8_peek_state() != n8_peek_run_state()) {
		case_line("B14", 0, "the join did not reach RUN (state=%d)",
		    n8_peek_state());
		return -1;
	}
	st = fake_driver_state();
	if (st->bss_ev_count == 0 || st->bss_ev_count > FAKE_DRIVER_BSS_EV_MAX ||
	    ev_join >= st->bss_ev_count) {
		case_line("B14", 0, "no association event was recorded "
		    "(count=%u ev_join=%u)", st->bss_ev_count, ev_join);
		return -1;
	}
	ev_join = st->bss_ev_count - 1;
	if (!st->bss_ev[ev_join].assoc || st->bss_ev[ev_join].aid == 0 ||
	    st->bss_ev[ev_join].bssid_null ||
	    memcmp(st->bss_ev[ev_join].bssid, ap_bssid, 6) != 0) {
		case_line("B14", 0, "the association event lacks the driver's "
		    "wiring (assoc=%d aid=%u null=%d)",
		    st->bss_ev[ev_join].assoc, st->bss_ev[ev_join].aid,
		    st->bss_ev[ev_join].bssid_null);
		return -1;
	}
	/*
	 * The rate bitmap must be the AP's advertised set carried through the
	 * interface's band-rate order (1, 2, 5.5, 11, 6, 9, 12, 18, 24, 36,
	 * 48, 54 Mbit/s).  ap_frames.c advertises exactly the first eight of
	 * those, so the bitmap is 0x00ff, and the basic subset - the four 11b
	 * rates it marks basic - is 0x000f.  A bitmap truncated on its way in,
	 * or one built from a hard-coded rate instead of the node's own set,
	 * lands on a different value; "not zero" would not catch either.
	 */
	if (st->bss_ev[ev_join].supp0 != 0x00ff) {
		case_line("B14", 0, "the station's rate bitmap is %#x, not the "
		    "AP's advertised set (0x00ff)", st->bss_ev[ev_join].supp0);
		return -1;
	}
	/*
	 * The basic subset is empty, and that is net80211's doing, not the
	 * adapter's: the node's rate set is negotiated against the interface's
	 * own supported set, whose standard tables carry no basic flag
	 * (ieee80211_std_rateset_11b, ieee80211.c:845), and the negotiation
	 * overwrites each node entry with the local value
	 * (ieee80211_fix_rate, ieee80211_proto.c:382-393).  So the AP's basic
	 * marking never reaches this point.  It is benign for both drivers -
	 * rtl8xxxu programs a zero response-rate set, which is what it has
	 * always done, and rtw89 falls back to the lowest rate when
	 * basic_rates is zero (core.c:775-778) - and it is registered as a
	 * limitation rather than papered over here.
	 */
	if (st->bss_ev[ev_join].basic != 0) {
		case_line("B14", 0, "the basic-rate bitmap is %#x; the AP's basic "
		    "marking was not expected to survive net80211's rate "
		    "negotiation", st->bss_ev[ev_join].basic);
		return -1;
	}
	/*
	 * The AP's address also has to be in the vif's own config: that is
	 * where a driver which finds its station from the vif reads it, and
	 * rtw89's association handler gives up when it is still zero.
	 */
	if (memcmp(st->bss_ev[ev_join].ap_addr, ap_bssid, 6) != 0) {
		case_line("B14", 0, "vif->cfg.ap_addr does not carry the AP's "
		    "address (%02x:%02x:%02x:%02x:%02x:%02x)",
		    st->bss_ev[ev_join].ap_addr[0], st->bss_ev[ev_join].ap_addr[1],
		    st->bss_ev[ev_join].ap_addr[2], st->bss_ev[ev_join].ap_addr[3],
		    st->bss_ev[ev_join].ap_addr[4], st->bss_ev[ev_join].ap_addr[5]);
		return -1;
	}
	ev_leave = st->bss_ev_count;
	wlan_bridge_disconnect(3);
	st = fake_driver_state();
	if (st->bss_ev_count <= ev_leave) {
		case_line("B14", 0, "the disconnect produced no event");
		return -1;
	}
	ev_leave = st->bss_ev_count - 1;
	if (st->bss_ev[ev_leave].assoc ||
	    (st->bss_ev[ev_leave].changed & BSS_CHANGED_BSSID) != 0) {
		case_line("B14", 0, "the leave leg is wrong (assoc=%d "
		    "bssid-pushed=%llu)", st->bss_ev[ev_leave].assoc,
		    (unsigned long long)(st->bss_ev[ev_leave].changed &
		    BSS_CHANGED_BSSID));
		return -1;
	}
	case_line("B14", 1, "assoc wiring carried (aid=%u rates=%#x); leave "
	    "leg sends no NULL-BSSID push",
	    st->bss_ev[ev_join].aid, st->bss_ev[ev_join].supp0);
	return 0;
}

/* ------------------------------------------------------------------ */
/* the flow                                                            */
/* ------------------------------------------------------------------ */

/*
 * B15: the transmit key of a group-addressed data frame on the station's
 * uplink. A DHCP DISCOVER leaves the station with the broadcast destination
 * address, but the 802.11 header's Address 1 is the AP - a unicast address -
 * and the key choice follows Address 1: the pairwise key seals the frame and
 * the CCMP header carries key id 0, while a group key sits at index 1 as the
 * default transmit key all along (output.c:555-562 selects the unicast key in
 * station mode, crypto.c:570-586 keys by Address 1). Choosing by the
 * destination address instead - the group key for a broadcast destination -
 * is the failure the board round had to rule out by hand: the peer indexes a
 * station's uplink frame under the pairwise key and drops a group-sealed one
 * with no answer, which reads exactly like a radio fault.
 */
static int
case_b15(void)
{
	const struct fake_driver_state *st = fake_driver_state();
	const struct wpa_vendor_port *vp;
	struct wpa_vendor_key key;
	struct wlan_bridge_counters before, after;
	uint8_t frame[64];
	unsigned int tx_before, i;
	int sealed = 0, keyid_seen = -1;

	vp = wpa_vendor_backend();
	if (vp == NULL || vp->set_key == NULL) {
		case_line("B15", 0, "the supplicant backend is not up");
		return -1;
	}
	/* the association the case needs: the cases behind it park the stack in
	 * SCAN (a parked re-sweep, a mid-sweep join that re-arms the sweep) and
	 * B14 ends with the disconnect leg, so the case re-establishes it the
	 * way the supplicant does - the vendor connect plus the scripted AP's
	 * answers - and then keeps its own precondition honest */
	if (n8_peek_state() != n8_peek_run_state()) {
		struct wpa_vendor_conn conn;
		unsigned int guard;

		/* the connect resolves its BSS through the scan table the backend
		 * pulls from, so the case sweeps first: the same two steps the
		 * supplicant's own join takes */
		if (vp->ensure_up != NULL && vp->ensure_up() != 0) {
			case_line("B15", 0, "the backend did not come up");
			return -1;
		}
		if (vp->scan_start == NULL ||
		    vp->scan_start((const uint8_t *)AP_SSID, AP_SSID_LEN) != 0) {
			case_line("B15", 0, "the re-sweep did not start");
			return -1;
		}
		for (guard = 0; guard < 64; guard++) {
			answer_new_tx_from_ap();
			if (wlan_bridge_scan_dwell() == 0)
				break;
		}
		answer_new_tx_from_ap();
		memset(&conn, 0, sizeof(conn));
		conn.ssid = (const uint8_t *)AP_SSID;
		conn.ssid_len = AP_SSID_LEN;
		conn.bssid = ap_bssid;
		conn.bssid_set = 1;
		conn.wpa_ie = b11_rsn_ie;
		conn.wpa_ie_len = sizeof(b11_rsn_ie);
		if (vp->connect == NULL || vp->connect(&conn) != 0) {
			case_line("B15", 0, "the reconnect was refused");
			return -1;
		}
		for (guard = 0; guard < 64; guard++) {
			if (n8_peek_state() == n8_peek_run_state())
				break;
			answer_new_tx_from_ap();
		}
	}
	if (n8_peek_state() != n8_peek_run_state()) {
		case_line("B15", 0, "the station is not associated (state=%d)",
		    n8_peek_state());
		return -1;
	}
	/* the pairwise key at index 0, as the handshake installs it */
	memset(&key, 0, sizeof(key));
	key.alg = WPA_VENDOR_ALG_CCMP;
	key.key_idx = 0;
	key.addr = ap_bssid;
	key.set_tx = 1;
	key.key = b11_tk;
	key.key_len = sizeof(b11_tk);
	if (vp->set_key(&key) != 0 || n8_peek_key_defined(1, 0) != 1) {
		case_line("B15", 0, "the pairwise key did not install");
		return -1;
	}
	/* and the group key at index 1, which is the default transmit key: a
	 * selection that follows the destination address would seal with this
	 * one and stamp key id 1 */
	memset(&key, 0, sizeof(key));
	key.alg = WPA_VENDOR_ALG_CCMP;
	key.key_idx = 1;
	key.addr = bcast_addr;
	key.set_tx = 1;
	key.key = b11_gtk;
	key.key_len = sizeof(b11_gtk);
	if (vp->set_key(&key) != 0 || n8_peek_key_defined(0, 1) != 1 ||
	    n8_peek_def_txkey() != 1) {
		case_line("B15", 0, "the group key did not become the default "
		    "transmit key (defined=%d def_tx=%d)",
		    n8_peek_key_defined(0, 1), n8_peek_def_txkey());
		return -1;
	}

	/* the frame a lease hunt sends: broadcast destination, IP payload */
	memset(frame, 0, sizeof(frame));
	memset(frame, 0xff, 6);
	memcpy(frame + 6, sta_mac, 6);
	frame[12] = 0x08;
	frame[13] = 0x00;
	for (i = 0; i < 24; i++)
		frame[14 + i] = (uint8_t)(0x40 + i);
	wlan_bridge_stats(&before);
	tx_before = st->tx_count;
	if (wlan_bridge_tx_raw(frame, 14 + 24) < 0) {
		case_line("B15", 0, "the broadcast transmit was refused");
		return -1;
	}
	st = fake_driver_state();
	wlan_bridge_stats(&after);
	for (i = tx_before; i < st->tx_count && i < FAKE_DRIVER_TX_MAX; i++) {
		const uint8_t *fp = st->tx_frames[i];
		unsigned int len = st->tx_lens[i];
		unsigned int hdr = (fp[0] & 0x80) != 0 ? 26U : 24U;

		if (len != hdr + 8 + 8 + 24 + 8)
			continue;
		if ((fp[1] & 0x40) == 0)		/* protected bit */
			continue;
		if (memcmp(fp + 4, ap_bssid, 6) != 0)	/* Address 1 */
			continue;
		if (memcmp(fp + hdr + 16, frame + 14, 24) == 0)
			continue;			/* still plaintext */
		keyid_seen = (int)((fp[hdr + 3] >> 6) & 0x3);
		sealed = 1;
		break;
	}
	if (!sealed) {
		case_line("B15", 0, "no sealed broadcast frame left the driver "
		    "(Address 1 must be the AP, not the broadcast address)");
		return -1;
	}
	if (keyid_seen != 0) {
		case_line("B15", 0, "the uplink frame carries key id %d: it was "
		    "sealed with the group key the destination address points "
		    "at, and the peer will drop it", keyid_seen);
		return -1;
	}
	if (after.data_tx_enc != before.data_tx_enc + 1) {
		case_line("B15", 0, "the protected-transmit counter did not move "
		    "(+%u)", after.data_tx_enc - before.data_tx_enc);
		return -1;
	}
	case_line("B15", 1, "a group-addressed uplink frame is sealed with the "
	    "pairwise key (Address 1 = AP, key id 0) while the group key is the "
	    "default transmit key");
	return 0;
}

/*
 * B16: the receive-status flags the driver hands over must reach the bridge's
 * counters. A frame the chip flagged as FCS-failed is counted as such - it is
 * the difference between "the peer never answered" and "the answer arrived
 * garbled" - and a protected data frame the chip already decrypted is counted
 * in both hardware-decrypt counters, which is the fingerprint the board round
 * read to catch a software-crypto stack decrypting a second time.
 */
static int
case_b16(void)
{
	struct wlan_bridge_counters before, after;
	uint8_t frame[64];
	unsigned int i;

	wlan_bridge_stats(&before);
	/* a beacon-shaped frame the chip flagged: counted as received, counted
	 * as flagged, and still recognised as a beacon */
	memset(frame, 0, sizeof(frame));
	frame[0] = 0x80;			/* beacon */
	memcpy(frame + 4, bcast_addr, 6);
	memcpy(frame + 10, ap_bssid, 6);
	memcpy(frame + 16, ap_bssid, 6);
	inject_rx_flags(frame, 24, RX_FLAG_FAILED_FCS_CRC, -50);
	wlan_bridge_stats(&after);
	if (after.rx_fcs_bad != before.rx_fcs_bad + 1) {
		case_line("B16", 0, "the FCS flag did not reach the counters "
		    "(+%u): the seam stopped mapping RX_FLAG_FAILED_FCS_CRC",
		    after.rx_fcs_bad - before.rx_fcs_bad);
		return -1;
	}
	if (after.rx_frames != before.rx_frames + 1) {
		case_line("B16", 0, "the flagged frame was not counted as "
		    "received (+%u)", after.rx_frames - before.rx_frames);
		return -1;
	}

	/* a protected data frame the chip already decrypted: both hardware
	 * counters move, and the protection split sees it as protected */
	before = after;
	memset(frame, 0, sizeof(frame));
	frame[0] = 0x08;
	frame[1] = 0x40;			/* protected, to-DS */
	memcpy(frame + 4, ap_bssid, 6);
	memcpy(frame + 10, sta_mac, 6);
	memcpy(frame + 16, sta_mac, 6);
	for (i = 24; i < sizeof(frame); i++)
		frame[i] = (uint8_t)i;
	inject_rx_flags(frame, 48, RX_FLAG_DECRYPTED, -50);
	wlan_bridge_stats(&after);
	if (after.rx_data != before.rx_data + 1 ||
	    after.rx_data_enc != before.rx_data_enc + 1) {
		case_line("B16", 0, "the protected data frame was not counted "
		    "(data +%u enc +%u)", after.rx_data - before.rx_data,
		    after.rx_data_enc - before.rx_data_enc);
		return -1;
	}
	if (after.rx_data_hwdec != before.rx_data_hwdec + 1 ||
	    after.rx_data_hwdec_prot != before.rx_data_hwdec_prot + 1) {
		case_line("B16", 0, "the chip-decrypted flag did not reach the "
		    "counters (hwdec +%u prot +%u): the seam stopped mapping "
		    "RX_FLAG_DECRYPTED",
		    after.rx_data_hwdec - before.rx_data_hwdec,
		    after.rx_data_hwdec_prot - before.rx_data_hwdec_prot);
		return -1;
	}
	case_line("B16", 1, "the FCS and chip-decrypted status flags reach the "
	    "bridge counters");
	return 0;
}

static void
run_suite(void)
{
	unsigned int guard;
	uint8_t eth[64];
	unsigned int i;

	lkpi_wlan_test_platform_init();
	wlan_osal_init();
	wlan_seam_adapter_init();

	wlan_port_set_eapol_rx(on_eapol, NULL);
	wlan_port_set_data_rx(on_data, NULL);
	wlan_port_set_event_handler(on_event, NULL);

	g_hw = ieee80211_alloc_hw(64, fake_driver_ops());
	if (g_hw == NULL) {
		case_line("B1", 0, "alloc_hw failed");
		return;
	}
	memcpy(g_hw->wiphy->perm_addr, sta_mac, 6);
	if (ieee80211_register_hw(g_hw) != 0) {
		case_line("B1", 0, "register_hw failed");
		return;
	}
	if (wlan_bridge_up() != 0) {
		case_line("B1", 0, "the driver's start refused");
		return;
	}
	/* the joiner configures what it wants before the sweep (the frame-test
	 * suite's own recipe) */
	n8_peek_set_ssid((const uint8_t *)AP_SSID, AP_SSID_LEN);
	if (case_b1() != 0)
		return;

	/* the scan group */
	if (wlan_bridge_scan_start() != 0) {
		case_line("B2", 0, "the scan did not start");
		return;
	}
	for (guard = 0; guard < 64; guard++) {
		/* answer first, then let the dwell end: the stack's probe
		 * request must be answered while the channel is still current */
		answer_new_tx_from_ap();
		if (wlan_bridge_scan_dwell() == 0)
			break;
	}
	answer_new_tx_from_ap();
	if (case_b2() != 0)
		return;
	if (case_b3() != 0)
		return;

	/*
	 * The join chain. The sweep's end already moved the stack into its join
	 * flow (that is what the roaming mode means); from here the harness only
	 * plays the AP: every frame the stack transmits is answered once, until
	 * the stack reports RUN. This is the same loop a real radio runs with its
	 * own completions.
	 */
	if (fault_name != NULL && strcmp(fault_name, "double-free-skb") == 0) {
		const struct fake_driver_state *st0 = fake_driver_state();

		/* the sweep has already transmitted; plant the double release on
		 * the next frame the driver is handed */
		fake_driver_knob_double_free_tx(st0->tx_calls + 1);
	}
	for (guard = 0; guard < 64; guard++) {
		if (n8_peek_state() == n8_peek_run_state())
			break;
		answer_new_tx_from_ap();
	}
	if (case_b4() != 0)
		return;

	/* EAPOL from the AP: a from-DS data frame whose ethertype is 0x888e */
	{
		uint8_t frame[64];
		uint8_t *p = frame;

		memset(frame, 0, sizeof(frame));
		p[0] = 0x08;
		p[1] = 0x02;			/* from DS */
		memcpy(p + 4, sta_mac, 6);
		memcpy(p + 10, ap_bssid, 6);
		memcpy(p + 16, ap_bssid, 6);
		p[24] = 0xaa; p[25] = 0xaa; p[26] = 0x03;
		p[27] = 0; p[28] = 0; p[29] = 0;
		p[30] = 0x88; p[31] = 0x8e;	/* EAPOL ethertype */
		p[32] = 0x02;			/* EAPOL version */
		p[33] = 0x03;			/* EAPOL type: key */
		p[34] = 0x00; p[35] = 0x10;
		for (i = 0; i < 16; i++)
			p[36 + i] = (uint8_t)i;
		inject_rx(frame, 52);
	}
	if (case_b5() != 0)
		return;

	/* data out: an ethernet frame from the presentation side */
	memset(eth, 0, sizeof(eth));
	memcpy(eth, ap_bssid, 6);
	for (i = 6; i < 12; i++)
		eth[i] = sta_mac[i - 6];
	eth[12] = 0x08;
	eth[13] = 0x00;
	eth[14] = 0x45;
	if (wlan_bridge_tx_raw(eth, 46) < 0) {
		case_line("B6", 0, "the raw transmit was refused");
		return;
	}
	/* data in: a from-DS frame carrying an IP packet */
	{
		uint8_t frame[64];
		uint8_t *p = frame;

		memset(frame, 0, sizeof(frame));
		p[0] = 0x08;
		p[1] = 0x02;
		memcpy(p + 4, sta_mac, 6);
		memcpy(p + 10, ap_bssid, 6);
		memcpy(p + 16, ap_bssid, 6);
		p[24] = 0xaa; p[25] = 0xaa; p[26] = 0x03;
		p[27] = 0; p[28] = 0; p[29] = 0;
		p[30] = 0x08; p[31] = 0x00;
		p[32] = 0x45;
		inject_rx(frame, 33);
	}
	if (case_b6() != 0)
		return;
	if (case_b7() != 0)
		return;
	(void)case_b8();
	(void)case_b9();
	(void)case_b10();
	(void)case_b11();
	(void)case_b12();
	(void)case_b13();
	(void)case_b14();
	/* last on purpose: B15 re-establishes the association itself, so it is
	 * unaffected by the state the cases above leave behind */
	(void)case_b15();
	(void)case_b16();

	if (failures == 0)
		printf("WLANBRIDGE: SUITE PASS cases=16\n");
	else
		printf("WLANBRIDGE: SUITE FAIL failures=%d\n", failures);
	fflush(stdout);
}

static void
app_task(void *arg)
{

	(void)arg;
	run_suite();
	/* success ends through the kernel's host seam (process exit 0); a failed
	 * run leaves through exit(1) so the wrapper can see it in the status */
	if (failures != 0)
		exit(1);
	cmsis2_host_end_scheduler();
}

int
main(int argc, char **argv)
{
	osThreadId_t t;
	osThreadAttr_t attr = { 0 };
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--fault") == 0 && i + 1 < argc) {
			fault_name = argv[++i];
		} else {
			printf("WLANBRIDGE: usage: %s [--fault no-filter|"
			    "tamper-auth|double-free-skb|no-manual|"
			    "no-rearm]\n", argv[0]);
			return 2;
		}
	}
	if (fault_name != NULL && strcmp(fault_name, "no-filter") != 0 &&
	    strcmp(fault_name, "tamper-auth") != 0 &&
	    strcmp(fault_name, "no-manual") != 0 &&
	    strcmp(fault_name, "no-rearm") != 0 &&
	    strcmp(fault_name, "double-free-skb") != 0) {
		printf("WLANBRIDGE: unknown fault '%s'\n", fault_name);
		return 2;
	}

	if (osKernelInitialize() != osOK) {
		printf("WLANBRIDGE: kernel init failed\n");
		return 2;
	}
	attr.name = "wlan-bridge";
	attr.stack_size = 32768u;
	attr.priority = osPriorityNormal;
	t = osThreadNew(app_task, NULL, &attr);
	if (t == NULL) {
		printf("WLANBRIDGE: cannot create the application task\n");
		return 2;
	}
	if (osKernelStart() != osOK)
		return 2;
	return failures == 0 ? 0 : 1;
}

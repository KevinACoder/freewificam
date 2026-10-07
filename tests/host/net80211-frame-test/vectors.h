/*
 * @file    vectors.h
 * @brief   Golden vectors: the responder's frames as hand-derived byte strings.
 *
 * Truth discipline: every byte below is derived from the 802.11 management
 * frame layout and the information-element encoding, not from the stack under
 * test. A management frame is FC(2) Duration(2) A1(6) A2(6) A3(6) Seq(2) Body;
 * the responders of this suite put no FCS in the buffer handed to the stack.
 *
 * source=derived-from:802.11-2020 frame formats (FC/address/sequence fields)
 * source=derived-from:ieee80211-skill references/protocol-basics.md:58-94
 *   (frame control, subtype table, element ids: SSID 0, rates 1, DS 3)
 * source=derived-from:ieee80211-skill references/protocol-basics.md:129-139
 *   (open system authentication: algorithmic 0, sequence 1 then 2, status 0)
 * source=derived-from:ieee80211-skill references/modes-ap-sta-monitor.md:29-33
 *   (association response body: capability, status, association id)
 */

#ifndef VWIFI_VECTORS_H
#define VWIFI_VECTORS_H

#include <stdint.h>

/* Probe response: 59 bytes. */
static const uint8_t vwifi_vec_probe_resp[] = {
	0x50, 0x00,			/* FC: version 0, mgmt, probe response */
	0x00, 0x00,			/* duration */
	0x02, 0x00, 0x00, 0x00, 0x00, 0x02,	/* A1 = station */
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,	/* A2 = AP */
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,	/* A3 = BSSID */
	0x00, 0x00,			/* sequence control */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* timestamp */
	0x64, 0x00,			/* beacon interval: 100 */
	0x21, 0x00,			/* capability: ESS + short preamble */
	0x00, 0x08, 0x76, 0x77, 0x69, 0x66, 0x69, 0x2d, 0x61, 0x70,
					/* element 0: SSID "vwifi-ap" */
	0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24,
					/* element 1: supported rates */
	0x03, 0x01, 0x01		/* element 3: DS parameter set, ch 1 */
};

/* Authentication response: 30 bytes. */
static const uint8_t vwifi_vec_auth_resp[] = {
	0xb0, 0x00,			/* FC: version 0, mgmt, auth */
	0x00, 0x00,			/* duration */
	0x02, 0x00, 0x00, 0x00, 0x00, 0x02,	/* A1 = station */
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,	/* A2 = AP */
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,	/* A3 = BSSID */
	0x00, 0x00,			/* sequence control */
	0x00, 0x00,			/* algorithm: open system */
	0x02, 0x00,			/* sequence: 2 */
	0x00, 0x00			/* status: success */
};

/* Association response: 40 bytes. */
static const uint8_t vwifi_vec_assoc_resp[] = {
	0x10, 0x00,			/* FC: version 0, mgmt, assoc response */
	0x00, 0x00,			/* duration */
	0x02, 0x00, 0x00, 0x00, 0x00, 0x02,	/* A1 = station */
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,	/* A2 = AP */
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,	/* A3 = BSSID */
	0x00, 0x00,			/* sequence control */
	0x21, 0x00,			/* capability: ESS + short preamble */
	0x00, 0x00,			/* status: success */
	0x01, 0x00,			/* association id: 1 */
	0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24
					/* element 1: supported rates */
};

#endif /* VWIFI_VECTORS_H */

/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   linuxkpi_ap_beacon.c
 * @brief  The AP's beacon template, derived from the GT arm's on-air hostapd
 *         beacon (docs/HANDOFF-AP-MODE.md §5.1).
 *
 * mac80211 builds the AP beacon as head + TIM + tail
 * (stack/mac80211/tx.c:5626-5652): the head is the 24-byte 802.11 header plus
 * the 12 fixed beacon bytes, and the tail is every IE *after* the TIM.  On the
 * GT arm mac80211 inserts the TIM exactly the same way, so the source for this
 * template is one beacon from the GT hostapd AP with the TIM element removed
 * from the IE list and everything else kept byte for byte.  The bytes are not
 * written from memory: tools/gt/beacon-template.py generated the arrays below
 * from the capture whose hash is recorded next to them.
 *
 * Fields that depend on the round rather than the template are patched at
 * build time: SA/BSSID (the AP's MAC), the beacon interval, the timestamp, the
 * SSID payload and the DS/HT-operating primary channel.  The validator refuses
 * a template that is missing an IE an open HT20/WMM AP needs, or that still
 * carries a TIM.
 */
#include <stddef.h>
#include <string.h>

#include "linux80211/ap_beacon.h"

#define AP_HEAD_LEN	36u

/* The template is the GT AP arm's hostapd beacon, byte for byte:
 *   source:  /home/pi/gt-ap-gt1/beacon.pcap (tools/gt/run-gt-ap.sh, gt1)
 *   pcap sha256: df59d4d29e9e9abc7be620371ddca4dc372ff8edcfa8a607fe7f76045e6d8296
 *   generator: tools/gt/beacon-template.py (TIM removed; FCS ignored)
 * IEs, in order: SSID, supported rates, DS, ERP, extended rates, power
 * capability, HT capabilities, HT operation, extended capabilities, WMM.
 * The head carries the GT card's MAC in SA/BSSID; the builder patches it to the
 * running AP's address, zeroes the timestamp and sets the interval, and the DS
 * and HT-operation primary channels follow the requested frequency. */
static const unsigned char ap_head_template[AP_HEAD_LEN] = {
	0x80, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x6c, 0x29,
	0x95, 0x12, 0xf0, 0xd9, 0x6c, 0x29, 0x95, 0x12, 0xf0, 0xd9, 0x00, 0x06,
	0x7d, 0x0b, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x01, 0x04,
};

static const unsigned char ap_tail_template[] = {
	0x00, 0x0c, 0x72, 0x74, 0x6f, 0x73, 0x2d, 0x37, 0x32, 0x36, 0x30, 0x2d,
	0x61, 0x70, 0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24,
	0x03, 0x01, 0x0b, 0x2a, 0x01, 0x04, 0x32, 0x04, 0x30, 0x48, 0x60, 0x6c,
	0x3b, 0x02, 0x51, 0x00, 0x2d, 0x1a, 0x2c, 0x00, 0x17, 0xff, 0xff, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2c, 0x01, 0x01, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3d, 0x16, 0x0b, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x08, 0x04, 0x00,
	0x00, 0x02, 0x00, 0x00, 0x00, 0x40, 0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02,
	0x01, 0x01, 0x01, 0x00, 0x03, 0xa4, 0x00, 0x00, 0x27, 0xa4, 0x00, 0x00,
	0x42, 0x43, 0x5e, 0x00, 0x62, 0x32, 0x2f, 0x00,
};

static unsigned int
ap_ie_len(const unsigned char *tail, unsigned int off, unsigned int len)
{
	unsigned int ie;

	if (off + 2u > len)
		return 0u;
	ie = 2u + (unsigned int)tail[off + 1u];
	return (off + ie <= len) ? ie : 0u;
}

/* 2.4 GHz channels 1..13 and 14, 5 GHz 36..165: the round only needs 2.4. */
static unsigned char
ap_freq_to_channel(unsigned int freq)
{
	if (freq == 2484u)
		return 14u;
	if (freq >= 2412u && freq < 2484u)
		return (unsigned char)((freq - 2407u) / 5u);
	if (freq >= 5000u)
		return (unsigned char)((freq - 5000u) / 5u);
	return 0u;
}

int
lkpi_ap_beacon_build(const char *ssid, unsigned int freq_mhz,
		     const unsigned char addr[6],
		     unsigned char *head, unsigned int head_cap,
		     unsigned int *head_len,
		     unsigned char *tail, unsigned int tail_cap,
		     unsigned int *tail_len)
{
	unsigned char chan;
	unsigned int i;
	unsigned int tlen = (unsigned int)sizeof(ap_tail_template);

	if (ssid == NULL || addr == NULL || head == NULL || tail == NULL ||
	    head_len == NULL || tail_len == NULL)
		return -1;
	if (head_cap < AP_HEAD_LEN || tail_cap < tlen)
		return -1;
	chan = ap_freq_to_channel(freq_mhz);
	if (chan == 0u)
		return -1;

	memcpy(head, ap_head_template, AP_HEAD_LEN);
	memcpy(head + 10, addr, 6);
	memcpy(head + 16, addr, 6);
	memset(head + 24, 0, 8);		/* timestamp: the driver sets it */
	head[30] = 100u;			/* beacon interval, LE */
	head[31] = 0u;

	memcpy(tail, ap_tail_template, tlen);

	/* Patch the SSID IE; its length must be the template's (the round
	 * uses one SSID on both arms, so a mismatch is a bug, not a case). */
	for (i = 0; i < tlen; ) {
		unsigned int ie = ap_ie_len(tail, i, tlen);
		unsigned int ssid_len = (unsigned int)strlen(ssid);

		if (ie == 0u)
			return -1;
		if (tail[i] == 0u && tail[i + 1u] != ssid_len)
			return -1;
		i += ie;
	}
	for (i = 0; i < tlen; ) {
		unsigned int ie = ap_ie_len(tail, i, tlen);

		if (ie == 0u)
			return -1;
		if (tail[i] == 0u)		/* SSID */
			memcpy(tail + i + 2u, ssid, ie - 2u);
		else if (tail[i] == 3u && ie == 3u)	/* DS parameter */
			tail[i + 2u] = chan;
		else if (tail[i] == 61u && ie >= 3u)	/* HT operation */
			tail[i + 2u] = chan;
		i += ie;
	}

	*head_len = AP_HEAD_LEN;
	*tail_len = tlen;
	return 0;
}

int
lkpi_ap_beacon_validate(const unsigned char *head, unsigned int head_len,
			const unsigned char *tail, unsigned int tail_len,
			char *why, unsigned int why_len)
{
	unsigned int i;
	unsigned int n = 0u;
	int have_ssid = 0, have_rates = 0, have_ds = 0;
	int have_htcap = 0, have_htop = 0, have_wmm = 0;

	if (why != NULL && why_len != 0u)
		why[0] = '\0';
	if (head == NULL || tail == NULL)
		goto bad;
	if (head_len != AP_HEAD_LEN)
		goto bad;
	if (head[0] != 0x80u || head[1] != 0x00u)
		goto bad;

	for (i = 0; i < tail_len; i += n) {
		unsigned int ie;

		if (i + 2u > tail_len)
			goto bad;
		ie = 2u + (unsigned int)tail[i + 1u];
		if (i + ie > tail_len)
			goto bad;
		n = ie;
		switch (tail[i]) {
		case 0:				/* SSID */
			have_ssid = (ie > 2u);
			break;
		case 1:				/* supported rates */
			have_rates = 1;
			break;
		case 3:				/* DS parameter */
			have_ds = (ie == 3u);
			break;
		case 5:				/* TIM: mac80211 inserts it */
			goto bad;
		case 45:			/* HT capabilities */
			have_htcap = (ie == 28u);
			break;
		case 61:			/* HT operation */
			have_htop = (ie == 24u);
			break;
		case 221:			/* WMM: 00:50:f2 type 2 subtype 1 */
			if (ie >= 8u && tail[i + 2u] == 0x00u &&
			    tail[i + 3u] == 0x50u && tail[i + 4u] == 0xf2u &&
			    tail[i + 5u] == 0x02u && tail[i + 6u] == 0x01u)
				have_wmm = 1;
			break;
		default:
			break;
		}
	}
	if (i != tail_len)
		goto bad;
	if (!have_ssid || !have_rates || !have_ds || !have_htcap ||
	    !have_htop || !have_wmm)
		goto bad;
	return 0;

bad:
	if (why != NULL && why_len != 0u) {
		const char *r = "beacon template shape";

		if (head == NULL || tail == NULL)
			r = "null buffer";
		else if (head_len != AP_HEAD_LEN)
			r = "head length";
		else if (head[0] != 0x80u || head[1] != 0x00u)
			r = "head frame control";
		else if (tail != NULL)
			r = "tail IE set (SSID/rates/DS/HT/WMM, no TIM)";
		strncpy(why, r, why_len - 1u);
		why[why_len - 1u] = '\0';
	}
	return -1;
}

unsigned int
lkpi_ap_beacon_ie_count(const unsigned char *tail, unsigned int tail_len)
{
	unsigned int i = 0u;
	unsigned int count = 0u;

	if (tail == NULL)
		return 0u;
	while (i + 2u <= tail_len) {
		unsigned int ie = 2u + (unsigned int)tail[i + 1u];

		if (i + ie > tail_len)
			return 0u;
		count++;
		i += ie;
	}
	return (i == tail_len) ? count : 0u;
}

unsigned int
lkpi_ap_beacon_hash(const unsigned char *buf, unsigned int len)
{
	unsigned int h = 2166136261u;		/* FNV-1a 32 */
	unsigned int i;

	if (buf == NULL)
		return 0u;
	for (i = 0; i < len; i++) {
		h ^= (unsigned int)buf[i];
		h *= 16777619u;
	}
	return h;
}

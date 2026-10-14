/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    wlan8189_ap_beacon.c
 * @brief   The access point's beacon bytes, assembled from a captured template.
 * @date    15.10.2026
 * @design  see wlan8189_ap_beacon.h - the beacon-supply role hostapd plays,
 *          reduced to the stream the driver parses. The element set is a data
 *          table copied from a real beacon, not code re-invented here; the
 *          header comment below names the capture and every element the
 *          template carried that this build leaves out.
 */

#include "wlan8189_ap_beacon.h"

/* --- the template ---------------------------------------------------------
 *
 * Source: the open access point captured on the measurement host's own radio
 * (hostapd on a brcmfmac radio, channel 1, HT20, no security; the round's
 * ledger holds the capture, and this is its frame 7). The bytes below are that
 * frame's elements verbatim; the two values that are ours to choose, the
 * network name and the channel, are parameters instead.
 *
 * Elements the template carried and this build leaves out, each with its owner:
 *   - TIM: the driver maintains it per association (core/rtw_ap.c).
 *   - Country, Power Constraint, TPC Report: the measurement host's regulatory
 *     domain, which the board is not.
 *   - OBSS Scan Parameters: 2.4 GHz HT40 coexistence, and this AP is HT20.
 *   - Extended Capabilities: hostapd's feature set (BSS transition and the
 *     like), none of which this first slice implements.
 *   - the Broadcom vendor element: the measurement host's own driver.
 */
static const unsigned char tpl_rates[] = {
	0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c,
};

static const unsigned char tpl_erp[] = {
	0x2a, 0x01, 0x00,
};

static const unsigned char tpl_ext_rates[] = {
	0x32, 0x04, 0x0c, 0x12, 0x18, 0x60,
};

/* HT capabilities: 26 payload bytes (the capture's, verbatim: id 45, length
 * 26, then four capability bytes and 22 zeros) */
static const unsigned char tpl_ht_cap[] = {
	0x2d, 0x1a,
	0x21, 0x00, 0x17, 0xff,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00,
};

/* HT operation: id and length, then the primary channel (ours), then the
 * capture's remaining 21 payload bytes */
static const unsigned char tpl_ht_op_head[] = {
	0x3d, 0x16,
};

static const unsigned char tpl_ht_op_tail[] = {
	0x08, 0x11,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* WMM parameter element: 24 payload bytes (the capture's, verbatim) */
static const unsigned char tpl_wmm[] = {
	0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01, 0x80, 0x00, 0x03, 0xa4,
	0x00, 0x00, 0x27, 0xa4, 0x00, 0x00, 0x42, 0x43, 0x5e, 0x00, 0x62, 0x32,
	0x2f, 0x00,
};

/* --- the assembly --------------------------------------------------------- */

/* append `len` bytes to `buf` at `*off` unless the capacity is short */
static int
put(unsigned char *buf, unsigned int cap, unsigned int *off,
    const unsigned char *src, unsigned int len)
{

	if (*off + len > cap)
		return -1;
	while (len-- != 0u)
		buf[(*off)++] = *src++;
	return 0;
}

unsigned int
wlan8189_ap_beacon_head(unsigned char *buf, unsigned int cap,
    unsigned int interval_tu, unsigned int capability)
{
	unsigned int off = 0u;

	if (buf == 0)
		return 0u;
	if (cap < WLAN8189_AP_BEACON_HEAD_LEN)
		return 0u;

	/* the 802.11 header: the driver strips it, only the length matters */
	while (off < 24u)
		buf[off++] = 0x00;
	/* timestamp: zero on purpose - the driver reads its capability from the
	 * stream's first two bytes, i.e. here (core/rtw_ap.c:1979), so a zero
	 * timestamp is what keeps the network open */
	while (off < 32u)
		buf[off++] = 0x00;
	/* beacon interval, little-endian */
	buf[off++] = (unsigned char)(interval_tu & 0xffu);
	buf[off++] = (unsigned char)((interval_tu >> 8) & 0xffu);
	/* capability, little-endian - the bits that go on air */
	buf[off++] = (unsigned char)(capability & 0xffu);
	buf[off++] = (unsigned char)((capability >> 8) & 0xffu);
	return off;
}

unsigned int
wlan8189_ap_beacon_tail(unsigned char *buf, unsigned int cap,
    const unsigned char *ssid, unsigned int ssid_len, unsigned int channel)
{
	unsigned int off = 0u;
	unsigned int i;

	if (buf == 0 || ssid == 0 || ssid_len == 0u || ssid_len > 32u)
		return 0u;

	/* SSID: id, length, the name */
	if (off + 2u + ssid_len > cap)
		return 0u;
	buf[off++] = 0x00;
	buf[off++] = (unsigned char)ssid_len;
	for (i = 0u; i < ssid_len; i++)
		buf[off++] = ssid[i];

	/* supported rates (the template's) */
	if (put(buf, cap, &off, tpl_rates, (unsigned int)sizeof(tpl_rates)) != 0)
		return 0u;

	/* DS parameter set: id, length, channel */
	if (off + 3u > cap)
		return 0u;
	buf[off++] = 0x03;
	buf[off++] = 0x01;
	buf[off++] = (unsigned char)(channel & 0xffu);

	/* ERP, extended supported rates, HT capabilities (the template's) */
	if (put(buf, cap, &off, tpl_erp, (unsigned int)sizeof(tpl_erp)) != 0)
		return 0u;
	if (put(buf, cap, &off, tpl_ext_rates,
	    (unsigned int)sizeof(tpl_ext_rates)) != 0)
		return 0u;
	if (put(buf, cap, &off, tpl_ht_cap, (unsigned int)sizeof(tpl_ht_cap)) != 0)
		return 0u;

	/* HT operation: the template's, with our channel as the primary */
	if (put(buf, cap, &off, tpl_ht_op_head,
	    (unsigned int)sizeof(tpl_ht_op_head)) != 0)
		return 0u;
	if (off + 1u > cap)
		return 0u;
	buf[off++] = (unsigned char)(channel & 0xffu);
	if (put(buf, cap, &off, tpl_ht_op_tail,
	    (unsigned int)sizeof(tpl_ht_op_tail)) != 0)
		return 0u;

	/* WMM (the template's) */
	if (put(buf, cap, &off, tpl_wmm, (unsigned int)sizeof(tpl_wmm)) != 0)
		return 0u;

	return off;
}

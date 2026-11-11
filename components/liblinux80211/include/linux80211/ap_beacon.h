/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   ap_beacon.h
 * @brief  The AP's beacon template: the 36-byte 802.11 head and the IE tail a
 *         START_AP carries, built from the GT arm's on-air hostapd beacon.
 *
 * mac80211 inserts the TIM between head and tail itself
 * (stack/mac80211/tx.c:5626-5652), so the tail starts after the TIM and must
 * not contain one.  The head is the fixed part only (24-byte header +
 * timestamp + interval + capability); the validator enforces both shapes.
 *
 * The IE bytes are not invented here: they are the GT hostapd AP's beacon
 * (tools/gt/run-gt-ap.sh, the same open/ch11/HT20/WMM configuration), with the
 * SSID/DS/HT-operating primary channel patched for the requested channel.  The
 * source capture's hash is recorded in the .c file.
 */
#ifndef LINUX80211_AP_BEACON_H
#define LINUX80211_AP_BEACON_H

#define LKPI_AP_BEACON_HEAD_MAX	64u
#define LKPI_AP_BEACON_TAIL_MAX	512u

/*
 * Build the head and tail for `ssid` on `freq_mhz`, with `addr` (the AP's
 * MAC) as SA/BSSID.  Returns 0, -EINVAL for a bad argument or a template that
 * cannot carry the requested SSID, or -ENOSPC if a buffer is too small.
 */
int lkpi_ap_beacon_build(const char *ssid, unsigned int freq_mhz,
			 const unsigned char addr[6],
			 unsigned char *head, unsigned int head_cap,
			 unsigned int *head_len,
			 unsigned char *tail, unsigned int tail_cap,
			 unsigned int *tail_len);

/*
 * Parse the built template.  Returns 0 when the head is the fixed-only shape
 * and the tail is a well-formed IE list containing the IEs an open HT20 AP
 * needs (SSID, supported rates, DS, HT capabilities, HT operation, WMM) and
 * no TIM; otherwise -EINVAL and a short reason in `why`.
 */
int lkpi_ap_beacon_validate(const unsigned char *head, unsigned int head_len,
			    const unsigned char *tail, unsigned int tail_len,
			    char *why, unsigned int why_len);

/* Number of IEs in the tail (0 when malformed). */
unsigned int lkpi_ap_beacon_ie_count(const unsigned char *tail,
				     unsigned int tail_len);

/* A small fingerprint for the anchor line (FNV-1a, 32 bit). */
unsigned int lkpi_ap_beacon_hash(const unsigned char *buf, unsigned int len);

#endif /* LINUX80211_AP_BEACON_H */

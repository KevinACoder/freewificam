/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    wlan8189_ap_beacon.h
 * @brief   The access point's beacon, assembled from a captured template.
 * @date    15.10.2026
 * @design  the beacon-supply role hostapd plays on Linux, reduced to the bytes
 *          the driver actually consumes: cfg80211's beacon contract is
 *          "head = 802.11 header + fixed fields, tail = elements" (the driver
 *          concatenates head[24:] with tail and parses that stream,
 *          third-party/rtl8189fs/os_dep/linux/ioctl_cfg80211.c:4902-4908), and
 *          the element set below is the template's - data, not a second
 *          implementation of anything (see wlan8189_ap_beacon.c).
 *
 * The functions are plain C with no kernel, lwIP or driver headers, because the
 * same file is compiled twice on purpose: into the board image (the AP command
 * and case) and into the host suite linuxkpi-cfg80211-test, which pins the
 * assembly byte for byte (the round's acceptance A2). One implementation, two
 * consumers - a second copy would be the defect this file exists to prevent.
 */

#ifndef WLAN8189_AP_BEACON_H
#define WLAN8189_AP_BEACON_H

/* The beacon head: the 24-byte 802.11 header (zeroed - the driver strips it,
 * only its length matters) plus the 12 fixed bytes the driver keeps. */
#define WLAN8189_AP_BEACON_HEAD_LEN	36u

/* Build the head into `buf` (capacity `cap`): 24 zero bytes, then timestamp
 * (zero - the driver reads its capability from the stream's first two bytes,
 * i.e. the timestamp, so a zero timestamp is what keeps the network open,
 * core/rtw_ap.c:1979), beacon interval and capability. Returns the bytes
 * written, or 0 when the buffer cannot hold them. */
unsigned int	wlan8189_ap_beacon_head(unsigned char *buf, unsigned int cap,
		    unsigned int interval_tu, unsigned int capability);

/* Build the element stream into `buf` (capacity `cap`): SSID, supported rates,
 * DS parameter set (the channel), ERP, extended supported rates, HT
 * capabilities, HT operation (the channel) and WMM - the template's elements,
 * minus the ones the driver or the platform owns (see the source). Returns the
 * bytes written, or 0 when the buffer cannot hold them. */
unsigned int	wlan8189_ap_beacon_tail(unsigned char *buf, unsigned int cap,
		    const unsigned char *ssid, unsigned int ssid_len,
		    unsigned int channel);

/* The capability the template carried (ESS | Short Slot | Spectrum Management |
 * Radio Measurement), exposed so the round's ledger and the host test can name
 * it instead of hard-coding the number in two places. */
#define WLAN8189_AP_CAPABILITY	0x1501u

/* The beacon interval in TUs, the template's value. */
#define WLAN8189_AP_INTERVAL_TU	100u

#endif /* WLAN8189_AP_BEACON_H */

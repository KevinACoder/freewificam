/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    net/ieee80211_radiotap.h
 * @brief   The radiotap capture header a monitor-mode receive path prepends: the base
 *          header record and the presence-bitmap field indices.
 *
 * @design declaration-only compatibility face, transcribed field for field from the
 *         interface's header (include/net/ieee80211_radiotap.h:26-99). The interface
 *         builds the base record with its __struct_group() alias so a fixed-size view of
 *         the four leading fields can be named as well; that alias machinery is not
 *         carried, because nothing in the compiled set names it - the record below has
 *         the interface's exact members, in its order, with its types and packing, and
 *         the flexible `it_optional` array that makes the header as long as its
 *         per-field presence bitmaps require. It is part of the vendored driver's fixed
 *         include set, so the header must exist under this name even where the compiled
 *         unit only includes it; the rest of the interface's radiotap surface (the
 *         per-field value records and their bit definitions) arrives with the consumer
 *         that names them. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_NET_IEEE80211_RADIOTAP_H_
#define _LINUXKPI_NET_IEEE80211_RADIOTAP_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/net/ieee80211_radiotap.h:26-53 */
struct ieee80211_radiotap_header {
	u8		it_version;	/* radiotap version, always 0 */
	u8		it_pad;		/* padding (or alignment) */
	__le16		it_len;		/* overall radiotap header length */
	__le32		it_present;	/* (first) present word */

	__le32		it_optional[];	/* all remaining presence bitmaps */
} __packed;

/* [LNX] include/net/ieee80211_radiotap.h:57 */
#define	PKTHDR_RADIOTAP_VERSION	0

/* [LNX] include/net/ieee80211_radiotap.h:62-99, one enumerator for one present-word bit
 * position: the values are the wire field indices and are contract with the capture
 * format, so they are stated exactly. */
enum ieee80211_radiotap_presence {
	IEEE80211_RADIOTAP_TSFT = 0,
	IEEE80211_RADIOTAP_FLAGS = 1,
	IEEE80211_RADIOTAP_RATE = 2,
	IEEE80211_RADIOTAP_CHANNEL = 3,
	IEEE80211_RADIOTAP_FHSS = 4,
	IEEE80211_RADIOTAP_DBM_ANTSIGNAL = 5,
	IEEE80211_RADIOTAP_DBM_ANTNOISE = 6,
	IEEE80211_RADIOTAP_LOCK_QUALITY = 7,
	IEEE80211_RADIOTAP_TX_ATTENUATION = 8,
	IEEE80211_RADIOTAP_DB_TX_ATTENUATION = 9,
	IEEE80211_RADIOTAP_DBM_TX_POWER = 10,
	IEEE80211_RADIOTAP_ANTENNA = 11,
	IEEE80211_RADIOTAP_DB_ANTSIGNAL = 12,
	IEEE80211_RADIOTAP_DB_ANTNOISE = 13,
	IEEE80211_RADIOTAP_RX_FLAGS = 14,
	IEEE80211_RADIOTAP_TX_FLAGS = 15,
	IEEE80211_RADIOTAP_RTS_RETRIES = 16,
	IEEE80211_RADIOTAP_DATA_RETRIES = 17,
	/* 18 is XChannel, but it's not defined yet */
	IEEE80211_RADIOTAP_MCS = 19,
	IEEE80211_RADIOTAP_AMPDU_STATUS = 20,
	IEEE80211_RADIOTAP_VHT = 21,
	IEEE80211_RADIOTAP_TIMESTAMP = 22,
	IEEE80211_RADIOTAP_HE = 23,
	IEEE80211_RADIOTAP_HE_MU = 24,
	IEEE80211_RADIOTAP_ZERO_LEN_PSDU = 26,
	IEEE80211_RADIOTAP_LSIG = 27,
	IEEE80211_RADIOTAP_TLV = 28,

	/* valid in every it_present bitmap, even vendor namespaces */
	IEEE80211_RADIOTAP_RADIOTAP_NAMESPACE = 29,
	IEEE80211_RADIOTAP_VENDOR_NAMESPACE = 30,
	IEEE80211_RADIOTAP_EXT = 31,
	IEEE80211_RADIOTAP_EHT_USIG = 33,
	IEEE80211_RADIOTAP_EHT = 34,
	IEEE80211_RADIOTAP_UHR_ELR = 37,
	IEEE80211_RADIOTAP_UHR = 38,
};

#endif /* _LINUXKPI_NET_IEEE80211_RADIOTAP_H_ */

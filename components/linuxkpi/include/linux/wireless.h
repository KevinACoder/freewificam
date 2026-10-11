/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/wireless.h
 * @brief   The wireless-extension request records a driver's ioctl-style configuration
 *          surface exchanges, as the interface names them.
 *
 * @design declaration-only compatibility face. The interface splits this API in two -
 *         a uapi header that holds every record and constant, and include/linux/
 *         wireless.h as a thin wrapper over it - and the face follows the shape without
 *         inventing the other half's directory: the records the compiled set reaches are
 *         stated here, transcribed field for field from the interface's uapi header
 *         (include/uapi/linux/wireless.h), which is the header that supplies them on a
 *         Linux build. This face must supply them because the vendored tree carries its
 *         own copy of this header (third-party/rtl8851bu/include/linux/wireless.h), which
 *         pulls the C library's socket header - on a Linux build the kernel's header wins
 *         the same way and the vendored copy is dead code. The set is grown per consumer
 *         (records first, then the constant families a compiled unit names), and the
 *         interface's `struct iw_request_info` is not here at all: it belongs to the net
 *         face (include/net/iw_handler.h), which owns the handler side of this API.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_WIRELESS_H_
#define _LINUXKPI_LINUX_WIRELESS_H_

#include "../../linuxkpi_core.h"

#include <linux/if.h>
#include <linux/socket.h>

/* [LNX] include/uapi/linux/wireless.h:90: the wireless-extension version this header
 * describes. It is a hard value, not a decoration: sources gate whole code blocks on
 * `WIRELESS_EXT < 17` / `> 17` / `<= 20` and on `we_version_compiled`, so leaving it
 * undefined would silently select the ancient branches the interface does not take. */
#define	WIRELESS_EXT		22

/* [LNX] include/uapi/linux/wireless.h: the sizes the records below are built on. */
#define	IW_MAX_FREQUENCIES	32
#define	IW_ESSID_MAX_SIZE	32
#define	IW_ENCODE_SEQ_MAX_SIZE	8
/* [LNX] include/uapi/linux/wireless.h:560: the largest private-argument payload a
 * request may carry, in bytes. */
#define	IW_CUSTOM_MAX		256

/* [LNX] include/uapi/linux/wireless.h:465-474: the quality flags, carried
 * unconditionally as the interface carries them (the vendored tree's own copy guards an
 * identical set on `WIRELESS_EXT < 17`, and on this face that gate is false). */
#define	IW_QUAL_QUAL_UPDATED	0x01	/* Value was updated since last read */
#define	IW_QUAL_LEVEL_UPDATED	0x02
#define	IW_QUAL_NOISE_UPDATED	0x04
#define	IW_QUAL_ALL_UPDATED	0x07
#define	IW_QUAL_DBM		0x08	/* Level + Noise are dBm */
#define	IW_QUAL_QUAL_INVALID	0x10	/* Driver doesn't provide value */
#define	IW_QUAL_LEVEL_INVALID	0x20
#define	IW_QUAL_NOISE_INVALID	0x40
#define	IW_QUAL_RCPI		0x80	/* Level + Noise are 802.11k RCPI */
#define	IW_QUAL_ALL_INVALID	0x70

/* [LNX] include/uapi/linux/wireless.h:628-633: the key algorithms a request names. */
#define	IW_ENCODE_ALG_NONE	0
#define	IW_ENCODE_ALG_WEP	1
#define	IW_ENCODE_ALG_TKIP	2
#define	IW_ENCODE_ALG_CCMP	3
#define	IW_ENCODE_ALG_PMK	4
#define	IW_ENCODE_ALG_AES_CMAC	5

/* [LNX] include/uapi/linux/wireless.h:641-646: the flags of a Michael MIC failure
 * event (struct iw_michaelmicfailure ->flags), which the receive path sets when it
 * reports a TKIP countermeasure. The values are one flag space and are carried whole -
 * a partial copy of such a space is an invitation to a wrong bit later. */
#define	IW_MICFAILURE_KEY_ID	0x00000003	/* Key ID 0..3 */
#define	IW_MICFAILURE_GROUP	0x00000004
#define	IW_MICFAILURE_PAIRWISE	0x00000008
#define	IW_MICFAILURE_STAKEY	0x00000010
#define	IW_MICFAILURE_COUNT	0x00000060	/* 1 or 2 (0 = count not supported) */

/* [LNX] include/uapi/linux/wireless.h: the PMKSA request commands and the PMKID size. */
#define	IW_PMKSA_ADD		1
#define	IW_PMKSA_REMOVE		2
#define	IW_PMKSA_FLUSH		3
#define	IW_PMKID_LEN		16

/* ------------------------------------------------------------------ */
/* records                                                             */
/* ------------------------------------------------------------------ */

/* [LNX] include/uapi/linux/wireless.h:680-686 */
struct iw_param {
	__s32		value;		/* The value of the parameter itself */
	__u8		fixed;		/* Hardware should not use auto select */
	__u8		disabled;	/* Disable the feature */
	__u16		flags;		/* Various specific flags (if any) */
};

/* [LNX] include/uapi/linux/wireless.h:691-695 */
struct iw_point {
	void __user	*pointer;	/* Pointer to the data (in user space) */
	__u16		length;		/* number of fields or size in bytes */
	__u16		flags;		/* Optional params */
};

/* [LNX] include/uapi/linux/wireless.h:706-712 */
struct iw_freq {
	__s32		m;		/* Mantissa */
	__s16		e;		/* Exponent */
	__u8		i;		/* List index (when in range struct) */
	__u8		flags;		/* Flags (fixed/auto) */
};

/* [LNX] include/uapi/linux/wireless.h:716-721 */
struct iw_quality {
	__u8		qual;		/* link quality (%retries, SNR, ...) */
	__u8		level;		/* signal level (dBm) */
	__u8		noise;		/* noise level (dBm) */
	__u8		updated;	/* Flags to know if updated */
};

/* [LNX] include/uapi/linux/wireless.h:731-737 */
struct iw_discarded {
	__u32		nwid;		/* Rx : Wrong nwid/essid */
	__u32		code;		/* Rx : Unable to code/decode (WEP) */
	__u32		fragment;	/* Rx : Can't perform MAC reassembly */
	__u32		retries;	/* Tx : Max MAC retries num reached */
	__u32		misc;		/* Others cases */
};

/* [LNX] include/uapi/linux/wireless.h:743-745 */
struct iw_missed {
	__u32		beacon;		/* Missed beacons/superframe */
};

/* [LNX] include/uapi/linux/wireless.h:750-755 */
struct iw_thrspy {
	struct sockaddr		addr;	/* Source address (hw/mac) */
	struct iw_quality	qual;	/* Quality of the link */
	struct iw_quality	low;	/* Low threshold */
	struct iw_quality	high;	/* High threshold */
};

/* [LNX] include/uapi/linux/wireless.h:768-803: the optional part of a scan request. */
struct iw_scan_req {
	__u8		scan_type;	/* IW_SCAN_TYPE_{ACTIVE,PASSIVE} */
	__u8		essid_len;
	__u8		num_channels;	/* num entries in channel_list; 0 = all */
	__u8		flags;		/* reserved as padding; use zero */
	struct sockaddr	bssid;
	__u8		essid[IW_ESSID_MAX_SIZE];
	__u32		min_channel_time;	/* in TU */
	__u32		max_channel_time;	/* in TU */
	struct iw_freq	channel_list[IW_MAX_FREQUENCIES];
};

/* [LNX] include/uapi/linux/wireless.h:829-840: the extended key request payload. The
 * key bytes are a flexible array member, as in the interface. */
struct iw_encode_ext {
	__u32		ext_flags;	/* IW_ENCODE_EXT_* */
	__u8		tx_seq[IW_ENCODE_SEQ_MAX_SIZE];	/* LSB first */
	__u8		rx_seq[IW_ENCODE_SEQ_MAX_SIZE];	/* LSB first */
	struct sockaddr	addr;
	__u16		alg;		/* IW_ENCODE_ALG_* */
	__u16		key_len;
	__u8		key[];
};

/* [LNX] include/uapi/linux/wireless.h:842-846 */
struct iw_mlme {
	__u16		cmd;		/* IW_MLME_* */
	__u16		reason_code;
	struct sockaddr	addr;
};

/* [LNX] include/uapi/linux/wireless.h:855-859 */
struct iw_pmksa {
	__u32		cmd;		/* IW_PMKSA_* */
	struct sockaddr	bssid;
	__u8		pmkid[IW_PMKID_LEN];
};

/* [LNX] include/uapi/linux/wireless.h: the Michael MIC failure event payload. */
struct iw_michaelmicfailure {
	__u32		flags;
	struct sockaddr	src_addr;
	__u8		tsc[IW_ENCODE_SEQ_MAX_SIZE];	/* LSB first */
};

/* [LNX] include/uapi/linux/wireless.h:881-892: the per-device wireless statistics the
 * handler descriptor reports. Carried by value in driver state, so the record has to be
 * complete, not forward-declared. */
struct iw_statistics {
	__u16		status;		/* Status
					 * - device dependent for now */

	struct iw_quality	qual;		/* Quality of the link
						 * (instant/mean/max) */
	struct iw_discarded	discard;	/* Packet discarded counts */
	struct iw_missed	miss;		/* Packet missed counts */
};

/* [LNX] include/uapi/linux/wireless.h:902-935: the fixed payload every wireless request
 * carries. The union's size is the contract (16 octets of data behind a 16-octet name is
 * why the request record is 32 octets); the members are the interface's. */
union iwreq_data {
	/* Config - generic */
	char		name[IFNAMSIZ];
	/* Name : used to verify the presence of wireless extensions. */

	struct iw_point	essid;		/* Extended network name */
	struct iw_param	nwid;		/* network id (or domain - the cell) */
	struct iw_freq	freq;		/* frequency or channel */

	struct iw_param	sens;		/* signal level threshold */
	struct iw_param	bitrate;	/* default bit rate */
	struct iw_param	txpower;	/* default transmit power */
	struct iw_param	rts;		/* RTS threshold */
	struct iw_param	frag;		/* Fragmentation threshold */
	__u32		mode;		/* Operation mode */
	struct iw_param	retry;		/* Retry limits & lifetime */

	struct iw_point	encoding;	/* Encoding stuff : tokens */
	struct iw_param	power;		/* PM duration/timeout */
	struct iw_quality qual;		/* Quality part of statistics */

	struct sockaddr	ap_addr;	/* Access point address */
	struct sockaddr	addr;		/* Destination address (hw/mac) */

	struct iw_param	param;		/* Other small parameters */
	struct iw_point	data;		/* Other large parameters */
};

/* [LNX] include/uapi/linux/wireless.h:944-952: the request record itself, the same size
 * as `struct ifreq`. */
struct iwreq {
	union {
		char	ifrn_name[IFNAMSIZ];	/* if name, e.g. "eth0" */
	} ifr_ifrn;

	/* Data part (defined just above) */
	union iwreq_data	u;
};

/* [LNX] include/uapi/linux/wireless.h:1063-1068 */
struct iw_priv_args {
	__u32		cmd;		/* Number of the ioctl to issue */
	__u16		set_args;	/* Type and number of args */
	__u16		get_args;	/* Type and number of args */
	char		name[IFNAMSIZ];	/* Name of the extension */
};

/* [LNX] include/uapi/linux/wireless.h:1081-1085 */
struct iw_event {
	__u16		len;		/* Real length of this stuff */
	__u16		cmd;		/* Wireless IOCTL */
	union iwreq_data	u;	/* IOCTL fixed payload */
};

#endif /* _LINUXKPI_LINUX_WIRELESS_H_ */

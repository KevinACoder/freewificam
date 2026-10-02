#ifndef _LINUXKPI_LINUX_WIRELESS_H_
#define _LINUXKPI_LINUX_WIRELESS_H_
/* [S6] B8: the wext EVENT face - CONFIG_WIRELESS_EXT is off (no handler
 * dispatch, no ioctl), but the RX path builds iw events inline
 * (recv_linux michael-mic indication), so the shapes and commands live
 * here ([LNX] wireless.h 30a family, trimmed to what the driver set
 * references). */
#include "../../linuxkpi_core.h"

#define	SIOCIWFIRST		0x8B00
#define	SIOCIWLAST		0x8BFF
#define	SIOCGIWNAME		(SIOCIWFIRST + 0x01)
#define	SIOCSIWSCAN		(SIOCIWFIRST + 0x18)
#define	SIOCGIWSCAN		(SIOCIWFIRST + 0x19)
#define	SIOCSIWESSID		(SIOCIWFIRST + 0x1A)
#define	SIOCGIWESSID		(SIOCIWFIRST + 0x1B)
#define	SIOCSIWRATE		(SIOCIWFIRST + 0x20)
#define	SIOCSIWMODE		(SIOCIWFIRST + 0x26)
#define	IW_SCAN_MAX_DATA	4096
#define	IW_ESSID_MAX_SIZE	32

struct iw_point {
	void		*pointer;
	unsigned short	length;
	unsigned short	flags;
};

struct iw_param {
	int		value;
	unsigned char	fixed;
	unsigned char	disabled;
	unsigned short	flags;
};

struct iw_freq {
	int		m;
	short		e;
	unsigned char	i;
	unsigned char	flags;
};

union iwreq_data {
	char		name[IFNAMSIZ];
	struct iw_point	essid;
	struct iw_param	sens;
	struct iw_param	bitrate;
	struct iw_param	rt;
	struct iw_param	frag;
	struct iw_param	rts;
	struct iw_param	retry;
	struct iw_param	txpower;
	struct iw_param	power;
	struct iw_freq	freq;
	unsigned int	mode;
	struct iw_point	encoding;
	struct iw_point	data;   /* the generic member the vendor writes
				   (wrqu.data.length) - [LNX] wireless.h tail */
	struct sockaddr	addr;
};

#define	IW_CUSTOM_MAX		256
#undef iwreq_data_has_extra
struct iw_michaelmicfailure {
	unsigned int	flags;
	struct sockaddr	src_addr;
};

struct iw_pmkid_cand {
	unsigned int	flags;
	unsigned int	index;
	unsigned char	bssid[ETH_ALEN];
};

/* event commands (IWEVFIRST = SIOCIWLAST + 1) */
#define	IWEVFIRST		0x8C00
#define	IWEVTXDROP		(IWEVFIRST + 0x00)
#define	IWEVQUAL		(IWEVFIRST + 0x01)
#define	IWEVCUSTOM		(IWEVFIRST + 0x02)
#define	IWEVREGISTERED		(IWEVFIRST + 0x03)
#define	IWEVEXPIRED		(IWEVFIRST + 0x04)
#define	IWEVMICHAELMICFAILURE	(IWEVFIRST + 0x05)
#define	IWEVASSOCREQIE		(IWEVFIRST + 0x06)
#define	IWEVASSOCRESPIE		(IWEVFIRST + 0x07)
#define	IWEVPMKIDCAND		(IWEVFIRST + 0x08)

struct iw_event {
	unsigned int	len;
	unsigned int	cmd;
	union iwreq_data u;
};

#define	IW_EV_LCP_LEN		(2 * sizeof(unsigned int))
#define	IW_EV_POINT_OFF		offsetof(union iwreq_data, essid)
#define	IW_EV_POINT_LEN		(IW_EV_LCP_LEN + sizeof(struct iw_point) - IW_EV_POINT_OFF)

#define	IW_MICFAILURE_GROUP	0x00000001
#define	IW_MICFAILURE_PAIRWISE	0x00000002
#define	IW_MICFAILURE_KEYID	0x0000000c
#define	IW_MICFAILURE_STA_KEY	0x00000010

#define	IW_ENCODE_INDEX		0x00ff
#define	IW_ENCODE_FLAGS		0xff00
#define	IW_ENCODE_MODE		0xf000
#define	IW_ENCODE_DISABLED	0x8000
#define	IW_ENCODE_OPEN		0x2000
#define	IW_ENCODE_RESTRICTED	0x4000
#endif

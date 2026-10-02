#ifndef _LINUXKPI_NET_IW_HANDLER_H_
#define _LINUXKPI_NET_IW_HANDLER_H_
/* [S6] B8: CONFIG_WIRELESS_EXT is off - no handler dispatch, no
 * net_device.wireless_handlers field.  The osdep funnel includes this
 * unconditionally, and drv_types embeds one iw_statistics per adapter:
 * those shapes come from here ([LNX] wireless.h:261 family). */
#include "../linuxkpi_core.h"

struct iw_handler_def;
struct iw_request_info;
struct iw_point;
union iwreq_data;

struct iw_quality {
	unsigned char	qual;
	unsigned char	level;
	unsigned char	noise;
	unsigned char	updated;
};

struct iw_discarded {
	unsigned long	nwid;
	unsigned long	code;
	unsigned long	fragment;
	unsigned long	retries;
	unsigned long	crypto;
	unsigned long	misc;
};

struct iw_missed {
	unsigned long	beacon;
};

struct iw_statistics {
	unsigned char	status;
	struct iw_quality	qual;
	struct iw_discarded	discard;
	struct iw_missed	miss;
};

#define	IW_QUAL_QUAL_UPDATED	0x01
#define	IW_QUAL_LEVEL_UPDATED	0x02
#define	IW_QUAL_NOISE_UPDATED	0x04
#define	IW_QUAL_ALL_UPDATED	0x07
#define	IW_QUAL_DBM		0x08
#endif

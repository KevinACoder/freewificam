#ifndef _LINUXKPI_LINUX_WIRELESS_H_
#define _LINUXKPI_LINUX_WIRELESS_H_
/* [S6] B8: include-only stub (CONFIG_WIRELESS_EXT is off; the wext face is
 * eliminated - only the include must resolve) */
#define	SIOCIWFIRST		0x8B00
#define	SIOCIWLAST		0x8BFF
#define	SIOCGIWNAME		(SIOCIWFIRST + 0x01)
#define	SIOCSIWSCAN		(SIOCIWFIRST + 0x18)
#define	SIOCGIWSCAN		(SIOCIWFIRST + 0x19)
#define	SIOCSIWESSID		(SIOCIWFIRST + 0x1A)
#define	SIOCGIWESSID		(SIOCIWFIRST + 0x1B)
#define	IW_SCAN_MAX_DATA	4096
#define	IW_ESSID_MAX_SIZE	32
#endif

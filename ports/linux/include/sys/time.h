/*
 * @file
 * @brief The one time type the imported sources keep as a file-scope constant.
 *
 * The receive path holds a `struct timeval` (the beacon interval in
 * microseconds) and includes no time header itself on the target.  The kernel
 * build has no libc <sys/time.h>; the kernel's own time types are not pulled
 * into the net80211 world.
 */

#ifndef _WLAN_LINUX_SYS_TIME_H_
#define _WLAN_LINUX_SYS_TIME_H_

struct timeval {
	long tv_sec;
	long tv_usec;
};

#endif /* _WLAN_LINUX_SYS_TIME_H_ */

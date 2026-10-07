/*
 * @file
 * @brief time structs for the imported USB headers.
 */

#ifndef _COMPAT_SYS_TIME_H_
#define _COMPAT_SYS_TIME_H_

#include <sys/cdefs.h>
#include <sys/types.h>

struct timespec {
	long tv_sec;
	long tv_nsec;
};

struct timeval {
	long tv_sec;
	long tv_usec;
};

#endif /* _COMPAT_SYS_TIME_H_ */

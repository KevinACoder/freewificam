/*
 * @file
 * @brief sockaddr family shell (no socket layer).
 */

#ifndef _COMPAT_SYS_SOCKET_H_
#define _COMPAT_SYS_SOCKET_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/ioccom.h>

#define AF_UNSPEC 0
#define AF_INET 2
#define AF_LINK 18

#define SOCK_DGRAM 2

typedef unsigned short sa_family_t;
typedef unsigned int socklen_t;

struct sockaddr {
	uint8_t sa_len;
	sa_family_t sa_family;
	char sa_data[14];
};

/* sa_len conventions for the families the stack builds */
#define _SA_LEN(sa) ((sa)->sa_len)

#endif /* _COMPAT_SYS_SOCKET_H_ */

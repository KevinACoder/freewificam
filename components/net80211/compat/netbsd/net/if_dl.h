/*
 * @file
 * @brief link-level sockaddr shell.
 */

#ifndef _COMPAT_NET_IF_DL_H_
#define _COMPAT_NET_IF_DL_H_

#include <sys/socket.h>
#include <sys/types.h>
#include <stdio.h>

#define LLADDR(s) ((void *)((s)->sdl_data + (s)->sdl_nlen))
#define CLLADDR(s) ((const void *)((s)->sdl_data + (s)->sdl_nlen))

struct sockaddr_dl {
	uint8_t sdl_len;
	sa_family_t sdl_family;
	unsigned short sdl_index;
	uint8_t sdl_type;
	uint8_t sdl_nlen;
	uint8_t sdl_alen;
	uint8_t sdl_slen;
	char sdl_data[12];
};

#endif /* _COMPAT_NET_IF_DL_H_ */

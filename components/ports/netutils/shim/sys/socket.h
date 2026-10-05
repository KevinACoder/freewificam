/*
 * @file   sys/socket.h
 * @brief  BSD socket shim -> lwIP sockets for the vendored netutils sources.
 *
 * The netutils tools are written against the POSIX socket headers (on
 * RT-Thread those come from SAL). This image has no SAL; lwIP 2.2.1 with
 * LWIP_SOCKET=1 is the socket provider, and with LWIP_COMPAT_SOCKETS=1 its
 * sockets.h maps the BSD names (socket/bind/sendto/select/...) onto the
 * lwip_* realizations. The shim directory precedes the toolchain's newlib
 * headers on the include path, so <sys/socket.h> lands here instead of
 * newlib's declarations- only header - which would compile but leave every
 * socket call unresolved at link time.
 *
 * struct sockaddr/sockaddr_in/sa_family_t come from lwip/sockets.h (with
 * sa_len present: LWIP_SOCKET_HAVE_SA_LEN=1, matching the vendored code's
 * sin_len stores). struct timeval comes from the toolchain's <sys/time.h>
 * via arch/cc.h (LWIP_TIMEVAL_PRIVATE=0), so the POSIX headers and lwIP see
 * one definition.
 */

#ifndef FWC_NETUTILS_SHIM_SYS_SOCKET_H
#define FWC_NETUTILS_SHIM_SYS_SOCKET_H

#include <lwip/sockets.h>

#endif /* FWC_NETUTILS_SHIM_SYS_SOCKET_H */

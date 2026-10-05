/*
 * @file   sys/select.h
 * @brief  POSIX select shim -> lwIP sockets for the vendored netutils sources.
 *
 * lwIP's sockets.h carries fd_set/FD_* and select() when
 * LWIP_SOCKET_SELECT=1 (the lwipopts default). Same rationale as
 * sys/socket.h: the shim directory wins over newlib's header, so the
 * vendored tftp core's select() calls resolve to lwip_select.
 */

#ifndef FWC_NETUTILS_SHIM_SYS_SELECT_H
#define FWC_NETUTILS_SHIM_SYS_SELECT_H

#include <lwip/sockets.h>

#endif /* FWC_NETUTILS_SHIM_SYS_SELECT_H */

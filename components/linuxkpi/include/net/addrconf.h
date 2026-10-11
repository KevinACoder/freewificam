/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    net/addrconf.h
 * @brief   The IPv6 address-configuration surface, as far as this port needs it.
 *
 * @design declaration-only compatibility face. The interface's header
 *         (include/net/addrconf.h) declares the inet6 address-configuration API: address
 *         and neighbour lifetime bookkeeping, multicast membership, the host-address
 *         selection entry points and the eui48-derived interface-identifier helpers.
 *         It is part of the vendored driver's fixed include set, so the header must
 *         exist under this name, but no translation unit of the compiled set names any
 *         symbol of it: the driver's IPv6 work is byte manipulation of its own WoWLAN
 *         offload records (core/rtw_wow.c, phl/phl_wow.c), not calls into this API, and
 *         there is no inet6 layer on this image to call into. The declarations are
 *         therefore left out rather than transcribed into a surface with no
 *         implementation and no caller (the header's identity, the bare include, is
 *         all the compiled set reaches); the first caller that needs one arrives with
 *         its declaration and the interface's exact shape. Reference for the face
 *         shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_NET_ADDRCONF_H_
#define _LINUXKPI_NET_ADDRCONF_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_NET_ADDRCONF_H_ */

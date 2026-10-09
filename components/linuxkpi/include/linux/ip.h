/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/ip.h
 * @brief   The include a driver's platform layer names for the IPv4 helpers; the wire
 *          header itself belongs to the vendored tree in this build, and this face
 *          states why.
 *
 * @design declaration-only compatibility face, deliberately without the IPv4 wire
 *         header. The interface (include/linux/ip.h) defines `struct iphdr` and
 *         ip_hdr(); in this build the vendored tree carries its own include/ip.h with
 *         the same `struct iphdr` (and its own include guard), and that header is
 *         included after this one by the driver's own type header - so defining the
 *         struct here as well would be a redefinition of one wire format, which is
 *         exactly the duplicate the project forbids. The struct is therefore
 *         forward-declared for any pointer use; the byte layout has exactly one
 *         definition in this build, the vendored one, and the day the vendored header
 *         stops providing it the layout moves here rather than appearing twice.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_IP_H_
#define _LINUXKPI_LINUX_IP_H_

#include "../../linuxkpi_core.h"

struct sk_buff;
struct iphdr;

/*
 * [LNX] include/linux/ip.h: `static inline struct iphdr *ip_hdr(const struct sk_buff
 * *skb) { return (struct iphdr *)skb_network_header(skb); }`. The vendored bytes use
 * ip_hdr() on a receive path, so the accessor is stated here; it needs the network
 * header offset the interface's sk_buff carries, which this project's sk_buff contract
 * (components/linuxkpi/linuxkpi_core.h) does not model - hence no body, and a caller
 * must be given the port's real offset rather than a silently wrong one.
 */
struct iphdr *ip_hdr(const struct sk_buff *skb);

#endif /* _LINUXKPI_LINUX_IP_H_ */

/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    net/checksum.h
 * @brief   The one checksum helper a bridge/NAT layer names.
 *
 * @design declaration-only compatibility face. The interface (include/net/checksum.h
 *         over the arch's asm/checksum.h) owns the checksum vocabulary; the compiled set
 *         names only csum_partial(), the running one's-complement sum a rewritten
 *         ICMPv6 checksum is recomputed from. It is declared as the interface declares
 *         it; the body is the port's, since whether the target has a checksum
 *         instruction decides it. The interface's csum_unfold() - the identity
 *         reinterpretation of a folded sum as an unfolded one - is stated in the core
 *         contract (components/linuxkpi/linuxkpi_core.h, with the wire-order
 *         conversions), because a compiled translation unit reaches it through the
 *         core rather than through this header. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    11.10.2026
 */

#ifndef _LINUXKPI_NET_CHECKSUM_H_
#define _LINUXKPI_NET_CHECKSUM_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/asm-generic/checksum.h:19: extern __wsum csum_partial(const void
 * *buff, int len, __wsum sum); */
extern __wsum csum_partial(const void *buff, int len, __wsum sum);

#endif /* _LINUXKPI_NET_CHECKSUM_H_ */

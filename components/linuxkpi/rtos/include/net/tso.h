/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    net/tso.h
 * @brief   The TCP segmentation-offload walker a driver's transmit path drives.
 *
 * @design declaration-only compatibility face. The interface's header
 *         (include/net/tso.h) owns the per-packet walker record and the three
 *         steps that advance it (start, build header, build data), plus a
 *         descriptor-count helper stated inline. What is transcribed here is the
 *         walker record field for field, the inline count helper, and the three
 *         step signatures; the steps' bodies belong to the kernel's networking
 *         stack and have no counterpart on this port yet. Reference for the face
 *         shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_NET_TSO_H_
#define _LINUXKPI_NET_TSO_H_

#include "../../linuxkpi_core.h"
#include <linux/skbuff.h>
#include <linux/dma-mapping.h>
#include <net/ip.h>

/* [LNX] include/net/tso.h:8 */
#define	TSO_HEADER_SIZE		256

/* [LNX] include/net/tso.h:10-18 */
struct tso_t {
	int	next_frag_idx;
	int	size;
	void	*data;
	u16	ip_id;
	u8	tlen;	/* transport header len */
	bool	ipv6;
	u32	tcp_seq;
};

/* [LNX] include/net/tso.h:21-24 */
static __always_inline __unused int
tso_count_descs(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->gso_segs * 2 + skb_shinfo(skb)->nr_frags;
}

/* [LNX] include/net/tso.h:26-30 */
void tso_build_hdr(const struct sk_buff *skb, char *hdr, struct tso_t *tso,
		   int size, bool is_last);
void tso_build_data(const struct sk_buff *skb, struct tso_t *tso, int size);
int tso_start(struct sk_buff *skb, struct tso_t *tso);

#endif /* _LINUXKPI_NET_TSO_H_ */

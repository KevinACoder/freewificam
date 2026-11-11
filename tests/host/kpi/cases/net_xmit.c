/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   net_xmit.c
 * @brief  The egress seam's transmit contract, on the host.
 *
 * The first board caller of lkpi_netdev_xmit() is mac80211, which reads three
 * fields the old full-MAC path never looked at: skb->dev (its status path),
 * skb->protocol (the EAPOL control-port test) and the tailroom an encrypting
 * stack may need.  This case pins them, plus the return mapping: a driver drop
 * must not come back as success.
 */
#include <stdint.h>
#include <string.h>

#include "linuxkpi_core.h"

#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>

#include "kpi_test.h"

#include "linux80211/net.h"

static struct net_device g_dev;
static struct sk_buff *g_seen;
static netdev_tx_t g_ret;
static unsigned g_headroom;
static unsigned g_tailroom;
static unsigned g_len;
static __be16 g_proto;

static netdev_tx_t
fake_xmit(struct sk_buff *skb, struct net_device *dev)
{
	g_seen = skb;
	g_headroom = skb_headroom(skb);
	g_tailroom = skb_tailroom(skb);
	g_len = skb->len;
	g_proto = skb->protocol;
	KPI_ASSERT(skb->dev == dev, "the seam set skb->dev");
	kfree_skb(skb);
	return g_ret;
}

static const struct net_device_ops g_ops = {
	.ndo_start_xmit = fake_xmit,
};

int
kpi_case_net_xmit(void)
{
	static const unsigned char frame[64] = {
		0x1c, 0xa7, 0x70, 0xde, 0x29, 0xa1,	/* dst: the AP */
		0x6c, 0x29, 0x95, 0x12, 0xf0, 0xd9,	/* src: the 7260 */
		0x08, 0x00,				/* IPv4 */
	};
	struct net_device bare;

	memset(&g_dev, 0, sizeof(g_dev));
	memset(&bare, 0, sizeof(bare));
	g_dev.netdev_ops = &g_ops;
	g_ret = NETDEV_TX_OK;
	g_seen = NULL;

	KPI_ASSERT_EQ(lkpi_netdev_xmit(NULL, frame, sizeof(frame)), -EINVAL,
		      "NULL dev");
	KPI_ASSERT_EQ(lkpi_netdev_xmit(&g_dev, NULL, sizeof(frame)), -EINVAL,
		      "NULL frame");
	KPI_ASSERT_EQ(lkpi_netdev_xmit(&g_dev, frame, 4), -EINVAL,
		      "a frame shorter than the ethernet header");
	KPI_ASSERT_EQ(lkpi_netdev_xmit(&bare, frame, sizeof(frame)), -ENODEV,
		      "a device without a transmit entry");

	KPI_ASSERT_EQ(lkpi_netdev_xmit(&g_dev, frame, sizeof(frame)), 0,
		      "NETDEV_TX_OK maps to 0");
	KPI_ASSERT(g_seen != NULL, "the transmit entry ran");
	KPI_ASSERT_EQ(g_len, sizeof(frame), "the frame length reached the entry");
	KPI_ASSERT(g_proto == htons(ETH_P_IP),
		   "skb->protocol carries the frame's ethertype");
	KPI_ASSERT(g_headroom >= 64, "the ethernet header fits in the headroom");
	KPI_ASSERT(g_tailroom >= 64,
		   "the egress reserved tailroom for an encrypting stack");

	/* A drop is not success: the supplicant's EAPOL send and lwIP's
	 * linkoutput both have to see it. */
	g_ret = NETDEV_TX_BUSY;
	KPI_ASSERT_EQ(lkpi_netdev_xmit(&g_dev, frame, sizeof(frame)), -EIO,
		      "a non-OK return maps to -EIO");

	return KPI_PASS;
}

/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   netdev.c
 * @brief  The netdev lifecycle C4's scan trigger depends on: registration
 *         gives the device a non-zero ifindex, and dev_open()/dev_close()
 *         move netif_running() (the bit cfg80211 reads as wdev_running()).
 *
 * C3 could return 0 from dev_open() while netif_running() stayed false; the
 * board's nl80211 pre_doit would then answer -ENETDOWN.  This is the host pin
 * that keeps the two observable states together.
 */
#include <stdint.h>

#include "linuxkpi_core.h"

#include <linux/netdevice.h>

#include "kpi_test.h"

int
kpi_case_netdev_open(void)
{
	struct net_device *dev;

	dev = alloc_netdev_mqs(0, "kpinet%d", NET_NAME_ENUM, ether_setup, 1, 1);
	KPI_ASSERT(dev != NULL, "alloc_netdev_mqs");
	KPI_ASSERT_EQ(dev->ifindex, 0, "a fresh record has no ifindex");
	KPI_ASSERT(!netif_running(dev), "a fresh record is not running");

	KPI_ASSERT(dev_alloc_name(dev, "kpinet%d") >= 0, "dev_alloc_name");
	KPI_ASSERT_EQ(register_netdev(dev), 0, "register_netdev");
	KPI_ASSERT(dev->ifindex > 0, "registration assigned an ifindex");
	KPI_ASSERT(!netif_running(dev), "registration alone does not start it");

	KPI_ASSERT_EQ(dev_open(dev), 0, "dev_open");
	KPI_ASSERT(netif_running(dev), "dev_open sets __LINK_STATE_START");
	KPI_ASSERT((dev->flags & IFF_UP) != 0, "dev_open sets IFF_UP");

	dev_close(dev);
	KPI_ASSERT(!netif_running(dev), "dev_close clears __LINK_STATE_START");
	KPI_ASSERT((dev->flags & IFF_UP) == 0, "dev_close clears IFF_UP");

	unregister_netdev(dev);
	free_netdev(dev);
	return KPI_PASS;
}

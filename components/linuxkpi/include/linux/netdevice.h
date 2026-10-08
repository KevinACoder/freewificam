/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/netdevice.h
 * @brief   The one device-related type the wireless structures need: the feature-word
 *          type they carry around.
 *
 * @design declaration-only compatibility face, grown per consumer: the interface's
 *         netdevice header is a large surface, and the only thing its wireless users
 *         need from it here is the feature word type (`netdev_features_t`, a 64-bit
 *         bitmask) plus the forward declaration of the device structure, because the
 *         data path in this project presents the interface through lwIP rather than
 *         through a Linux net device. Everything else arrives with a consumer that
 *         actually needs it. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    08.10.2026
 */

#ifndef _LINUXKPI_LINUX_NETDEVICE_H_
#define _LINUXKPI_LINUX_NETDEVICE_H_

#include "../../linuxkpi_core.h"

struct net_device;

typedef u64 netdev_features_t;

#endif /* _LINUXKPI_LINUX_NETDEVICE_H_ */
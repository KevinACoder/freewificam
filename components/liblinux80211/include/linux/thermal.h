/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/thermal.h
 * @brief   The thermal-zone records iwlwifi's mvm header names.
 *
 * @design declaration-only compatibility face.  `mvm/mvm.h` includes this
 *         header unconditionally, but the records it declares (the zone, the
 *         cooling device, the trip points) are used only under
 *         `#ifdef CONFIG_THERMAL`.  This line leaves that symbol absent
 *         (linux80211/driver_config.h), so the compiled set needs the header to
 *         exist and to parse, not a thermal implementation.  The declarations
 *         are the interface's names so a future CONFIG_THERMAL build has one
 *         place to grow.
 * @date    10.11.2026
 */

#ifndef _LINUXKPI_LINUX_THERMAL_H_
#define _LINUXKPI_LINUX_THERMAL_H_

#include "../../linuxkpi_core.h"

struct device;
struct thermal_zone_device;
struct thermal_cooling_device;

/* [LNX] include/linux/thermal.h: the trip record iwl_mvm_thermal_device
 * embeds under CONFIG_THERMAL (temperature in millidegrees C, the kernel's
 * unit).  The field set is the one the driver's initialiser names. */
struct thermal_trip {
	int	temperature;
	int	type;
	int	flags;
};

#define THERMAL_TEMP_INVALID	(-INT_MAX)
#define THERMAL_TRIP_PASSIVE	0
#define THERMAL_TRIP_FLAG_RW_TEMP	BIT(0)
#define THERMAL_NAME_LENGTH	20

struct thermal_zone_device_ops {
	int (*get_temp)(struct thermal_zone_device *tz, int *temp);
	int (*set_trip_temp)(struct thermal_zone_device *tz, int trip,
			     int temp);
};

struct thermal_cooling_device_ops;

struct thermal_zone_device *
thermal_zone_device_register_with_trips(const char *type,
					const struct thermal_trip *trips,
					int num_trips, void *devdata,
					const struct thermal_zone_device_ops *ops,
					const struct thermal_zone_device_ops *ops2,
					int passive_delay, int polling_delay);
void thermal_zone_device_unregister(struct thermal_zone_device *tz);
int thermal_zone_device_enable(struct thermal_zone_device *tz);
int thermal_zone_device_disable(struct thermal_zone_device *tz);

struct thermal_cooling_device *
thermal_cooling_device_register(const char *type, void *devdata,
				const struct thermal_cooling_device_ops *ops);
void thermal_cooling_device_unregister(struct thermal_cooling_device *cdev);

#endif /* _LINUXKPI_LINUX_THERMAL_H_ */

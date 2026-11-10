/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_rfkill.c
 * @brief   The rfkill record cfg80211's wiphy allocation creates.
 *
 * @design this port has no rfkill switch and no rfkill core (the face says so
 *         in include/linux/rfkill.h).  cfg80211 nevertheless allocates one at
 *         wiphy_new() and registers it at wiphy_register(); a NULL from
 *         rfkill_alloc() makes the whole registration fail.  So the record
 *         exists and is inert: it always reports "not blocked", registration
 *         succeeds, and there is nothing to toggle.
 *
 *         The one semantic that matters is the direction: a driver that asks
 *         "is the radio hard-blocked" gets false, which is the no-switch
 *         answer, never a fake "everything is fine" after a real block request
 *         (there is no request path to fake).
 * @date    10.11.2026
 */

#include <stdint.h>
#include <string.h>

#include "linuxkpi_core.h"

#include <uapi/linux/rfkill.h>
#include <linux/rfkill.h>

struct lkpi_rfkill {
	const char		*name;
	struct device		*parent;
	const struct rfkill_ops	*ops;
	void			*ops_data;
	bool			blocked;
};

struct rfkill *
rfkill_alloc(const char *name, struct device *parent, enum rfkill_type type,
	     const struct rfkill_ops *ops, void *ops_data)
{
	struct lkpi_rfkill *rf;

	(void)type;
	rf = kzalloc(sizeof(*rf), GFP_KERNEL);
	if (rf == NULL)
		return NULL;
	rf->name = name;
	rf->parent = parent;
	rf->ops = ops;
	rf->ops_data = ops_data;
	return (struct rfkill *)rf;
}

int
rfkill_register(struct rfkill *rfkill)
{
	return rfkill != NULL ? 0 : -EINVAL;
}

void
rfkill_unregister(struct rfkill *rfkill)
{
	(void)rfkill;
}

void
rfkill_destroy(struct rfkill *rfkill)
{
	kfree(rfkill);
}

bool
rfkill_set_hw_state_reason(struct rfkill *rfkill, bool blocked,
			   unsigned long reason)
{
	struct lkpi_rfkill *rf = (struct lkpi_rfkill *)rfkill;
	bool was = rf != NULL ? rf->blocked : false;

	(void)reason;
	if (rf != NULL)
		rf->blocked = blocked;
	return was;
}

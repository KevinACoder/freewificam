/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    c2h_inline.c
 * @brief   The --wrap of ieee80211_queue_work for the C2H inline seam.
 *
 * The stamp of the intent is c2h_inline.h.  This unit is linked through
 * WL80211_PROBE_SRCS and the stack's queue call is wrapped by wl80211.mk.
 */
#include <stddef.h>

#include <linux/types.h>
#include <linux/workqueue.h>
#include <net/mac80211.h>

#include "linux80211/c2h_inline.h"

void (*lkpi_c2h_work_func)(struct work_struct *work);

void
lkpi_c2h_inline_register(void (*func)(struct work_struct *work))
{
	lkpi_c2h_work_func = func;
}

void __real_ieee80211_queue_work(struct ieee80211_hw *hw,
				 struct work_struct *work);

void
__wrap_ieee80211_queue_work(struct ieee80211_hw *hw, struct work_struct *work)
{
	if (work != NULL && work->func != NULL &&
	    lkpi_c2h_work_func != NULL && work->func == lkpi_c2h_work_func) {
		/* The C2H work is small (dequeue + firmware parse); running it
		 * here keeps its reports inside the driver's 500 ms purge
		 * window.  The work's own mutex and the queue's lock make a
		 * concurrent queued instance harmless. */
		work->func(work);
		return;
	}
	__real_ieee80211_queue_work(hw, work);
}

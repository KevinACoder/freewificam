/*
 * @file
 * @brief abort() for the net80211 world inside the kernel.
 *
 * The imported sources call abort() when an internal invariant fails.  Print
 * the fact through the port log seam and park the task: a failed assertion that
 * speaks is findable, a silent one is not.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bug.h>

void abort(void)
{
	pr_err("net80211: abort() reached; parking the task\n");
	BUG();
}

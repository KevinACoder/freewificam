// SPDX-License-Identifier: GPL-2.0
/*
 * lkpi_udisk_linux.c - the Linux shell of the acceptance driver (SPEC
 * S6/linuxkpi §3.7-4): module plumbing + the self-test trigger.  The
 * core compiles unchanged; this shell only registers and triggers, and
 * the evidence lands in dmesg (one line, one stamp, one message).
 *
 * First-target flow (spec §3.7-5): unbind usb-storage from the interface,
 * then `insmod lkpi_udisk.ko test_enable=1 test_lba=1024` - the probe
 * chain re-runs for the now-unclaimed interface and the post-probe hook
 * drives the full self-test (TUR, INQUIRY, READ CAPACITY, the four
 * pattern write/read rounds), whose lines ARE the acceptance evidence.
 */

#include <linux/module.h>

#include "lkpi_udisk.h"

static bool test_enable;
static uint test_lba = 1024;
static uint test_bw_mib;

module_param(test_enable, bool, 0);
MODULE_PARM_DESC(test_enable, "run the self-test after probe");
module_param(test_lba, uint, 0);
MODULE_PARM_DESC(test_lba, "the pattern-test LBA (default 1024)");
module_param(test_bw_mib, uint, 0);
MODULE_PARM_DESC(test_bw_mib, "optional post-test bandwidth sweep (MiB)");

static void
ud_probe_done(struct usb_interface *intf)
{

	if (!test_enable)
		return;
	pr_info("lkpi_udisk: selftest start (lba %u)\n", test_lba);
	if (lkpi_udisk_test(intf, test_lba) != 0)
		pr_err("lkpi_udisk: selftest FAIL\n");
	if (test_bw_mib != 0) {
		(void)lkpi_udisk_bw(intf, test_bw_mib, false);
		(void)lkpi_udisk_bw(intf, test_bw_mib, true);
	}
}

/* the core's weak post-probe hook: the Linux shell auto-runs here */
void
ud_post_probe(struct usb_interface *intf)
{
	ud_probe_done(intf);
}

module_usb_driver(lkpi_udisk_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zhugengyu");
MODULE_DESCRIPTION("lkpi_udisk - the linuxkpi acceptance BOT driver (Linux shell)");

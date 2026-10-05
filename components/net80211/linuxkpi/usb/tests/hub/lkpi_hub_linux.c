// SPDX-License-Identifier: GPL-2.0
/*
 * lkpi_hub_linux.c - the Linux shell of the acceptance driver (SPEC
 * specs/linuxkpi-hub §3.4-5): module plumbing + the stimulus trigger.
 * The core compiles unchanged; this shell only registers and triggers,
 * and the evidence lands in dmesg (one line, one stamp, one message).
 *
 * First-target flow (spec §3.2): unbind the kernel hub driver from the
 * GL hub interface, then `insmod lkpi_hub.ko stim_port=4` - usbcore
 * re-probes the unclaimed interface, this driver's class match takes
 * it, the post-probe hook arms the status-change int-URB and runs the
 * stimulus script; the event lines ARE the acceptance evidence.
 */

#include <linux/module.h>
#include <linux/printk.h>

#include "lkpi_hub.h"

/* the downstream port the stimulus script fires at (the U 盘 port on
 * the GL hub; both targets must use the same port - the dual-target
 * comparison key) */
static uint stim_port;
static uint stim_rounds = 6;

module_param(stim_port, uint, 0);
MODULE_PARM_DESC(stim_port,
	"downstream hub port for the stimulus script (0 = arm only)");
module_param(stim_rounds, uint, 0);
MODULE_PARM_DESC(stim_rounds, "stimulus rounds (default 6)");

/* the core's weak post-probe hook: the Linux shell arms here (the
 * kernel hub driver is unbound, nobody competes for the int-EP) and
 * runs the counting window; lkpi_hub_capture skips its window-opening
 * arm retry because the urb is already armed */
void
uh_post_probe(struct usb_interface *intf)
{

	if (lkpi_hub_arm(intf) != 0) {
		pr_err("lkpi_hub: arm failed\n");
		return;
	}
	if (stim_port != 0)
		(void)lkpi_hub_capture(intf, stim_port, stim_rounds);
}

module_usb_driver(lkpi_hub_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zhugengyu");
MODULE_DESCRIPTION("lkpi_hub - the linuxkpi acceptance hub status driver (Linux shell)");

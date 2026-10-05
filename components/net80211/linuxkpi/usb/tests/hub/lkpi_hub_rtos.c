/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * lkpi_hub_rtos.c - the freewebcam shell of the acceptance driver (SPEC
 * specs/linuxkpi-hub §3.4-5): cherrysh command + explicit registration.
 * The core compiles unchanged; this shell only registers and triggers,
 * and the evidence lands on the stamped console (lkpi_hub: prefix).
 *
 *   lkpi_hub attach            the claim-less second attachment (H2;
 *                              walks the roothub children for a hub
 *                              device, probes through the real match
 *                              chain, never touches intf[].priv)
 *   lkpi_hub capture <port> [n]
 *                              the window protocol (sacrifice stimulus
 *                              -> arm retry -> counting stimuli -> stats)
 *   lkpi_hub stim <port> [n]   the stimulus script only
 *   lkpi_hub arm | stop | count
 *
 * Registration: usb_register() runs before usbh_platform_start() like
 * the udisk driver's; the hub interface itself is claimed by the
 * cherryusb hub class (first-match class section), so this driver sits
 * inert until the attach command probes it explicitly.
 */

#include <stdint.h>
#include <stdlib.h>

#include "cherrysh_adapter.h"
#include "csh.h"

#include "lkpi_hub.h"

/* the adapter's claim-less attachment (port/adapters/linuxkpi) */
extern int lkpi_usbh_hub_attach(void);

/* the driver has exactly one attached instance; the hook stashes the
 * interface at probe time for the shell commands */
static struct usb_interface *s_attached_intf;

/* the core's weak post-probe hook: the RTOS shell only remembers the
 * interface - the capture protocol arms inside its window (spec
 * §3.3-1: the RTOS probe records, never arms) */
void
uh_post_probe(struct usb_interface *intf)
{

	s_attached_intf = intf;
}

int
lkpi_hub_rtos_register(void)
{

	return usb_register(&lkpi_hub_driver);
}

static unsigned int
lkpi_hub_arg(const char *s, unsigned int def)
{
	unsigned int v = 0;

	if (s == NULL || *s == '\0')
		return def;
	while (*s >= '0' && *s <= '9')
		v = v * 10u + (unsigned int)(*s++ - '0');
	return v;
}

static int lkpi_hub_cmd(int argc, char *argv[])
{
	chry_shell_t *csh = CSH_FROM_ARGV(argc, argv);
	const char *sub = argc >= 2 ? argv[1] : "";

	if (strcmp(sub, "attach") == 0)
		return lkpi_usbh_hub_attach() != 0;
	if (s_attached_intf == NULL) {
		csh_printf(csh, "lkpi_hub: no attached instance (attach "
		    "first)\n");
		return 1;
	}
	if (strcmp(sub, "capture") == 0) {
		u32 port = lkpi_hub_arg(argc >= 3 ? argv[2] : NULL, 4);
		u32 rounds = lkpi_hub_arg(argc >= 4 ? argv[3] : NULL, 6);

		return lkpi_hub_capture(s_attached_intf, port, rounds) != 0;
	}
	if (strcmp(sub, "stim") == 0) {
		u32 port = lkpi_hub_arg(argc >= 3 ? argv[2] : NULL, 4);
		u32 rounds = lkpi_hub_arg(argc >= 4 ? argv[3] : NULL, 6);

		return lkpi_hub_stim(s_attached_intf, port, rounds) != 0;
	}
	if (strcmp(sub, "arm") == 0) {
		return lkpi_hub_arm(s_attached_intf) != 0;
	}
	if (strcmp(sub, "stop") == 0) {
		lkpi_hub_stop(s_attached_intf);
		return 0;
	}
	if (strcmp(sub, "count") == 0) {
		csh_printf(csh, "lkpi_hub: events %u\n",
		    lkpi_hub_count(s_attached_intf));
		return 0;
	}

	csh_printf(csh, "usage: lkpi_hub [attach|capture <port> [n]|stim "
	    "<port> [n]|arm|stop|count]\n");
	return 1;
}
CSH_CMD_EXPORT_ALIAS_FULL(lkpi_hub_cmd, lkpi_hub, "lkpi_hub [attach|capture|stim|arm|stop|count]",
    "the lkpi_hub acceptance driver commands (interrupt transfer)");

/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * lkpi_udisk_rtos.c - the freewebcam shell of the acceptance driver (SPEC
 * S6/linuxkpi §3.7-4): cherrysh command + explicit registration.  The
 * core compiles unchanged; this shell only registers and triggers, and
 * the evidence lands on the stamped console (lkpi_udisk: prefix, the
 * print-discipline counterpart of the Linux target's dmesg lines).
 *
 *   lkpi_ud [test [lba]]    the full self-test (TUR+INQUIRY+CAPACITY+
 *                           patterns A5/5A/00/FF - the dual-target key)
 *   lkpi_ud inq | cap       identification only
 *   lkpi_ud pattern <lba> <byte>
 *   lkpi_ud bw [MiB] [w]    sequential bandwidth (read; w = write side)
 *   lkpi_ud lba <n>         the default test LBA (default 1024)
 *
 * Registration: usb_register() must run BEFORE the enumeration starts
 * (the hub thread claims through the class hook the moment a device
 * shows up), so the app main calls lkpi_udisk_rtos_register() ahead of
 * usbh_platform_start().
 */

#include <stdint.h>
#include <stdlib.h>

#include "cherrysh_adapter.h"
#include "csh.h"

#include "lkpi_udisk.h"

static u32 s_default_lba = 1024;

/* the driver has exactly one claimed instance in this image; the test
 * entries ride the usb_interface, which the driver context knows - the
 * hook stashes it at probe time for the shell */
static struct usb_interface *s_claimed_intf;

/* the core's weak post-probe hook: the RTOS shell just remembers the
 * interface (triggers run from the shell commands) */
void
ud_post_probe(struct usb_interface *intf)
{

	s_claimed_intf = intf;
}

int
lkpi_udisk_rtos_register(void)
{

	return usb_register(&lkpi_udisk_driver);
}

static unsigned int
lkpi_ud_arg(const char *s, unsigned int def)
{
	unsigned int v = 0;

	if (s == NULL || *s == '\0')
		return def;
	while (*s >= '0' && *s <= '9')
		v = v * 10u + (unsigned int)(*s++ - '0');
	return v;
}

static int lkpi_ud_cmd(int argc, char *argv[])
{
	chry_shell_t *csh = CSH_FROM_ARGV(argc, argv);
	const char *sub = argc >= 2 ? argv[1] : "";

	if (s_claimed_intf == NULL) {
		csh_printf(csh, "lkpi_ud: no claimed instance (probe not "
		    "run?)\n");
		return 1;
	}

	if (strcmp(sub, "test") == 0) {
		u32 lba = lkpi_ud_arg(argc >= 3 ? argv[2] : NULL,
		    s_default_lba);

		if (lkpi_udisk_test(s_claimed_intf, lba) != 0) {
			csh_printf(csh, "lkpi_ud: test FAIL\n");
			return 1;
		}
		csh_printf(csh, "lkpi_ud: test PASS\n");
		return 0;
	}
	if (strcmp(sub, "inq") == 0) {
		return lkpi_udisk_inq(s_claimed_intf) != 0;
	}
	if (strcmp(sub, "cap") == 0) {
		return lkpi_udisk_cap(s_claimed_intf) != 0;
	}
	if (strcmp(sub, "pattern") == 0) {
		u32 lba = lkpi_ud_arg(argc >= 3 ? argv[2] : NULL,
		    s_default_lba);
		u32 byte = lkpi_ud_arg(argc >= 4 ? argv[3] : NULL, 0xA5);

		return lkpi_udisk_pattern(s_claimed_intf, lba,
		    (u8)byte) != 0;
	}
	if (strcmp(sub, "bw") == 0) {
		u32 mib = lkpi_ud_arg(argc >= 3 ? argv[2] : NULL, 32);
		bool write = argc >= 4 && strcmp(argv[3], "w") == 0;

		return lkpi_udisk_bw(s_claimed_intf, mib, write) != 0;
	}
	if (strcmp(sub, "lba") == 0) {
		s_default_lba = lkpi_ud_arg(argc >= 3 ? argv[2] : NULL,
		    s_default_lba);
		csh_printf(csh, "lkpi_ud: default lba %u\n",
		    s_default_lba);
		return 0;
	}

	csh_printf(csh, "usage: lkpi_ud [test [lba]|inq|cap|pattern <lba> "
	    "<byte>|bw [MiB] [w]|lba <n>]\n");
	return 1;
}
CSH_CMD_EXPORT_ALIAS_FULL(lkpi_ud_cmd, lkpi_ud, "lkpi_ud [test|inq|cap|pattern|bw|lba]",
    "the lkpi_udisk acceptance driver commands (dual-target truth)");

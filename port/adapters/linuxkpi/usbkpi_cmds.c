/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * usbkpi_cmds.c - shell command for the linuxkpi USB layer: the dump exit
 * the linux<->rtos byte comparison reads (SPEC S6/linuxkpi R2.4).
 *
 *   usbkpi on|off        arm/disarm the urb capture ring
 *   usbkpi on ctrl|bulk  arm with a capture filter (the types the diff wants)
 *   usbkpi status        sequence counter + capture state
 *   usbkpi trace on|off  per-transfer console narrative (bring-up only:
 *                        saturates the UART, distorts timing)
 *   usbkpi dump [n]      the last n records (ep/dir/type/phase/status/
 *                        len/crc + the first payload bytes) - usbmon-
 *                        normalizable
 *   usbkpi rxhold on|off control experiment: hold bulk-IN submits back
 *                        from the HC (no concurrent rx churn)
 *   usbkpi wedge         the ctrl-path stage + drain thread state
 *
 * The ring records every urb crossing at the linuxkpi boundary (submit and
 * completion, control messages included), which is exactly the stream a
 * Linux usbmon capture on the same dongle produces; the comparison tool
 * normalizes the two.  Payload content is summarized (first 8 bytes +
 * crc32).
 */

#include <stdint.h>
#include <string.h>

#include "cherrysh_adapter.h"
#include "csh.h"

#include "linuxkpi_usb.h"
#include <linux/usb.h>	/* USB_ENDPOINT_XFER_* for the filter words */

/* single-command dump ceiling: matches the ring size so one command
 * retrieves the whole window */
#define	USBKPI_DUMP_MAX	16384

static void
usbkpi_print_record(chry_shell_t *csh, const struct usbkpi_record *r)
{

	csh_printf(csh, "%llu %7u %s ep%02x t%u%c st%d len%u crc%08x "
	    "%02x%02x%02x%02x%02x%02x%02x%02x\n",
	    (unsigned long long)r->seq, r->tick, r->dir ? "in " : "out",
	    r->ep, r->type & 0x7fu, r->type & 0x80u ? 'S' : 'C',
	    r->status, r->len, r->crc,
	    r->head[0], r->head[1], r->head[2], r->head[3],
	    r->head[4], r->head[5], r->head[6], r->head[7]);
}

static unsigned int
usbkpi_arg(const char *s, unsigned int def, unsigned int max)
{
	unsigned int v = 0;

	if (s == NULL || *s == '\0')
		return def;
	while (*s != '\0') {
		if (*s < '0' || *s > '9')
			return def;
		v = v * 10 + (unsigned int)(*s - '0');
		if (v > max)
			return max;
		s++;
	}
	return v != 0 ? v : def;
}

static int usbkpi_cmd(int argc, char *argv[])
{
	chry_shell_t *csh = CSH_FROM_ARGV(argc, argv);
	static struct usbkpi_record recs[32];

	if (argc < 2) {
		csh_printf(csh, "usage: usbkpi on|off|status|trace on|dump "
		    "[n] [skip]|rxhold on|off|wedge\n");
		return 0;
	}

	if (strcmp(argv[1], "on") == 0) {
		if (argc >= 3) {
			unsigned int mask;

			if (strcmp(argv[2], "all") == 0)
				mask = 0xffu;
			else if (strcmp(argv[2], "ctrl") == 0)
				mask = 1u << USB_ENDPOINT_XFER_CONTROL;
			else if (strcmp(argv[2], "bulk") == 0)
				mask = 1u << USB_ENDPOINT_XFER_BULK;
			else {
				csh_printf(csh,
				    "usbkpi: filter all|ctrl|bulk\n");
				return 0;
			}
			usbkpi_set_type_mask(mask);
		}
		usbkpi_set_enabled(true);
		csh_printf(csh, "usbkpi: enabled (seq %llu)\n",
		    (unsigned long long)usbkpi_sequence());
		return 0;
	}
	if (strcmp(argv[1], "off") == 0) {
		usbkpi_set_enabled(false);
		csh_printf(csh, "usbkpi: disabled (seq %llu)\n",
		    (unsigned long long)usbkpi_sequence());
		return 0;
	}
	if (strcmp(argv[1], "wedge") == 0) {
		usbkpi_wedge_report();
		return 0;
	}
	if (strcmp(argv[1], "rxhold") == 0) {
		bool on = argc >= 3 && strcmp(argv[2], "on") == 0;

		usbkpi_rxhold_set(on);
		csh_printf(csh, "usbkpi: rxhold %s\n", on ? "on" : "off");
		return 0;
	}
	if (strcmp(argv[1], "trace") == 0) {
		lkpi_usb_trace_set(argc >= 3 && strcmp(argv[2], "on") == 0);
		return 0;
	}
	if (strcmp(argv[1], "sync") == 0) {
		lkpi_usb_force_sync_set(argc >= 3 &&
		    strcmp(argv[2], "on") == 0);
		return 0;
	}
	if (strcmp(argv[1], "status") == 0) {
		unsigned int mask = usbkpi_type_mask();

		csh_printf(csh, "usbkpi: seq %llu filter %s%s%s rxhold %s\n",
		    (unsigned long long)usbkpi_sequence(),
		    (mask & (1u << USB_ENDPOINT_XFER_CONTROL)) != 0 ?
		    "ctrl" : "",
		    (mask & (1u << USB_ENDPOINT_XFER_BULK)) != 0 ?
		    "+bulk" : "",
		    (mask & (1u << USB_ENDPOINT_XFER_INT)) != 0 ?
		    "+int" : "",
		    usbkpi_rxhold() ? "on" : "off");
		return 0;
	}
	if (strcmp(argv[1], "dump") == 0) {
		unsigned int want = usbkpi_arg(argc >= 3 ? argv[2] : NULL,
		    32, USBKPI_DUMP_MAX);
		unsigned int skip = usbkpi_arg(argc >= 4 ? argv[3] : NULL,
		    0, 65535);
		unsigned int printed = 0;

		/* batches of 32 keep the console line assembly happy and
		 * the stack flat; skip walks back into the ring */
		while (printed < want) {
			unsigned int batch = want - printed > 32 ?
			    32 : want - printed;
			unsigned int got = usbkpi_get_records(recs, batch,
			    skip + printed);
			unsigned int i;

			if (got == 0)
				break;
			for (i = 0; i < got; i++)
				usbkpi_print_record(csh, &recs[i]);
			printed += got;
		}
		csh_printf(csh, "usbkpi: %u record(s)\n", printed);
		return 0;
	}

	csh_printf(csh, "usage: usbkpi on|off|status|trace on|dump [n] [skip]"
	    "|rxhold on|off|wedge\n");
	return 0;
}
CSH_CMD_EXPORT_ALIAS_FULL(usbkpi_cmd, usbkpi, "usbkpi on|off|status|dump ...",
    "linuxkpi usb capture ring (the byte-comparison exit)");

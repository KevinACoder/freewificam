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
 *   usbkpi rxq           bulk single-flight queue state (per-endpoint
 *                        busy + parked FIFO depth; the W3 arbiter)
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

/* CherryUSB before the linux headers: with USB_DEF_H set, linux/usb.h
 * skips its descriptor copies and the TU keeps one descriptor type */
#include "usbh_core.h"

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

/* minilibc has no strtoul: a 1/8-digit hex parser for the forensics
 * commands (peek/find) */
static unsigned long
usbkpi_hex(const char *s, unsigned long def)
{
	unsigned long v = 0;

	if (s == NULL || *s == '\0')
		return def;
	while (*s != '\0') {
		char c = *s++;
		int d;

		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (c >= 'a' && c <= 'f')
			d = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			d = c - 'A' + 10;
		else
			return def;
		v = (v << 4) | (unsigned long)d;
	}
	return v;
}

static int usbkpi_cmd(int argc, char *argv[])
{
	chry_shell_t *csh = CSH_FROM_ARGV(argc, argv);
	static struct usbkpi_record recs[32];

	if (argc < 2) {
		csh_printf(csh, "usage: usbkpi on|off|status|trace on|dump "
		    "[n] [skip]|rxhold on|off|wedge|rxq|epstat|rawbulk|"
		    "sync on|off\n");
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
			else if (strcmp(argv[2], "int") == 0)
				mask = 1u << USB_ENDPOINT_XFER_INT;
			else {
				csh_printf(csh,
				    "usbkpi: filter all|ctrl|bulk|int\n");
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
	if (strcmp(argv[1], "rxq") == 0 || strcmp(argv[1], "stream") == 0) {
		usbkpi_stream_report();
		return 0;
	}
	if (strcmp(argv[1], "epstat") == 0) {
		usbkpi_ep_report();
		return 0;
	}
	if (strcmp(argv[1], "peek") == 0) {
		/* P6-b forensics: dump memory the CPU sees right now (the
		 * failed-IN stage question: stale cache lines or absent
		 * data), plus a needle scan over the image's DRAM window
		 * for where the device's bytes actually landed */
		uintptr_t addr;
		unsigned int n;

		if (argc < 4) {
			csh_printf(csh, "usbkpi: peek <hexaddr> <n>\n");
			return 1;
		}
		addr = (uintptr_t)usbkpi_hex(argv[2], 0);
		n = usbkpi_arg(argv[3], 64, 4096);
		for (unsigned int off = 0; off < n; off += 16) {
			const uint8_t *p = (const uint8_t *)addr + off;

			csh_printf(csh, "%08x: %02x%02x%02x%02x %02x%02x%02x%02x "
			    "%02x%02x%02x%02x %02x%02x%02x%02x\n",
			    (unsigned int)(addr + off),
			    p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
			    p[8], p[9], p[10], p[11], p[12], p[13], p[14],
			    p[15]);
		}
		return 0;
	}
	if (strcmp(argv[1], "find") == 0) {
		/* scan a DRAM window for a 4-byte needle (hex, e.g.
		 * 50414d50 = "PMAP" from INQUIRY) - where did the device's
		 * data land.  Args: needle [start [end]] (default covers the
		 * image + the whole 4 MiB heap; narrow it to dodge the
		 * needle's own stack slot - a full-window scan self-hits) */
		uint32_t needle;
		uintptr_t a, lo, hi;
		unsigned int hits = 0;

		if (argc < 3) {
			csh_printf(csh, "usbkpi: find <8hex> [start [end]]\n");
			return 1;
		}
		needle = (uint32_t)usbkpi_hex(argv[2], 0xdeadbeef);
		lo = (uintptr_t)usbkpi_hex(argc >= 4 ? argv[3] : NULL,
		    0x0a000000u);
		hi = (uintptr_t)usbkpi_hex(argc >= 5 ? argv[4] : NULL,
		    0x0a700000u);
		for (a = lo; a < hi; a += 4) {
			if (*(volatile uint32_t *)a == needle) {
				csh_printf(csh, "usbkpi: hit at %08x\n",
				    (unsigned int)a);
				if (++hits >= 32) {
					csh_printf(csh, "usbkpi: ...\n");
					break;
				}
			}
		}
		csh_printf(csh, "usbkpi: scan done (%u hit(s))\n", hits);
		return 0;
	}
	if (strcmp(argv[1], "rawbulk") == 0) {
		/* E2b (issues/2026-10-01-xhci-bulk-td-not-fetched): one raw
		 * cherryusb BOT round - CBW(TUR) out, CSW in - submitted
		 * from THIS shell thread with the vendored usbh_msc urb
		 * shape (sync timeout, complete NULL, static aligned
		 * buffers) against the same interface the linux driver
		 * claimed.  Pass => the hurb shape is not the variable
		 * (context/timing is); fail => shape domain. */
		static struct usbh_urb urb;
		static uint8_t cbw[31] __attribute__((aligned(64)));
		static uint8_t csw[13] __attribute__((aligned(64)));
		struct usbh_hubport *hp = lkpi_usbh_claimed_hport();
		uint8_t intf = lkpi_usbh_claimed_intf();
		struct usb_endpoint_descriptor *epout = NULL, *epin = NULL;
		unsigned int i;
		int ret;

		if (hp == NULL) {
			csh_printf(csh, "usbkpi: no claimed instance\n");
			return 1;
		}
		for (i = 0; i < CONFIG_USBHOST_MAX_ENDPOINTS; i++) {
			struct usb_endpoint_descriptor *ep =
			    &hp->config.intf[intf].altsetting[0].ep[i].ep_desc;

			if (ep->bLength == 0)
				continue;
			if ((ep->bmAttributes & 0x3u) == USB_ENDPOINT_XFER_BULK) {
				if ((ep->bEndpointAddress & 0x80u) != 0u)
					epin = ep;
				else
					epout = ep;
			}
		}
		if (epin == NULL || epout == NULL) {
			csh_printf(csh, "usbkpi: bulk ep pair not found\n");
			return 1;
		}
		/* BOT CBW: 'USBC' sig, tag, data len 0 (TUR carries no
		 * data), OUT flags, lun 0, 6-byte CDB of zeros = TUR.
		 * The optional "stage" arg moves both buffers into the
		 * stage pool's memory (the bisect: raw shape, pool
		 * address) while the default keeps the file statics. */
		{
			uint8_t *cbw_p = cbw;
			uint8_t *csw_p = csw;

			if (argc >= 3 && strcmp(argv[2], "stage") == 0) {
				cbw_p = (uint8_t *)
				    lkpi_usb_stage_slot_mem(2);
				csw_p = (uint8_t *)
				    lkpi_usb_stage_slot_mem(3);
				if (cbw_p == NULL || csw_p == NULL) {
					csh_printf(csh,
					    "usbkpi: stage slots not exposed\n");
					return 1;
				}
			}
			memset(cbw_p, 0, 31);
			cbw_p[0] = 0x55;
			cbw_p[1] = 0x53;
			cbw_p[2] = 0x42;
			cbw_p[3] = 0x43;
			cbw_p[4] = 1;

			usbh_bulk_urb_fill(&urb, hp, epout, cbw_p, 31, 5000,
			    NULL, NULL);
			ret = usbh_submit_urb(&urb);
			csh_printf(csh,
			    "usbkpi: raw CBW -> %d actual=%u ep=%02x buf=%s\n",
			    ret, urb.actual_length,
			    epout->bEndpointAddress,
			    (cbw_p == cbw) ? "static" : "stage");
			if (ret != 0)
				return 1;

			usbh_bulk_urb_fill(&urb, hp, epin, csw_p, 13, 5000,
			    NULL, NULL);
			ret = usbh_submit_urb(&urb);
			csh_printf(csh, "usbkpi: raw CSW -> %d actual=%u "
			    "sig=%c%c%c%c status=%02x\n", ret,
			    urb.actual_length, csw_p[0], csw_p[1], csw_p[2],
			    csw_p[3], csw_p[12]);
			return ret != 0;
		}
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
	    "|rxhold on|off|wedge|rxq|rawbulk|sync on|off\n");
	return 0;
}
CSH_CMD_EXPORT_ALIAS_FULL(usbkpi_cmd, usbkpi, "usbkpi on|off|status|dump ...",
    "linuxkpi usb capture ring (the byte-comparison exit)");

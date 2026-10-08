/*
 * @file    wpa_cmd.c
 * @brief   wpa command: drive the supplicant from the cherrysh console.
 *
 * The PSK is only ever a command line argument, nothing is stored.
 *
 * @design facade over the port API (DESIGN.md section 1, "control command"
 *         row): the same CSH_CMD_EXPORT_ALIAS_FULL export shape the shell's
 *         own built-ins use (components/cherrysh/builtin/clear.c). The
 *         ensure_up call goes through the port API so the chip bring-up
 *         routes via the registered vendor backend and runs on the console
 *         thread (the chip init blocks on the USB workers and must not run
 *         on the supplicant thread).
 *
 * @author  zhugengyu
 * @date    09.10.2026
 */

#include <string.h>

#include "csh.h"

#include "wpa_port_api.h"

/* sscanf sits behind the freestanding libc surface the image does not
 * ship; a MAC address is six hex bytes, parse it by hand */
static int sscanf_bssid(const char *s, unsigned int b[6]);

static void usage(chry_shell_t *csh) {
	csh_printf(csh,
		   "usage: wpa start | status | connect <ssid> <psk> [bssid] | "
		   "disconnect\n");
}

static int cmd_wpa(int argc, char **argv) {
	chry_shell_t *csh = (void *) argv[argc + 1];
	int ret;

	if (argc < 2) {
		usage(csh);
		return 0;
	}

	if (strcmp(argv[1], "start") == 0) {
		ret = wpa_port_start();
		csh_printf(csh, "wpa: start %s\n",
			   ret == 0 ? "ok" : "failed");
		return 0;
	}
	if (strcmp(argv[1], "status") == 0) {
		ret = wpa_port_status();
		if (ret != 0) {
			csh_printf(csh, "wpa: supplicant busy\n");
		}
		return 0;
	}
	if (strcmp(argv[1], "connect") == 0 &&
	    (argc == 3 || argc == 4 || argc == 5)) {
		/* argc == 3: no psk - an open network (key_mgmt NONE) */
		if (!wpa_port_started()) {
			/* interface up happens on this (console) thread:
			 * the chip init blocks on the USB workers and must
			 * not run on the supplicant thread */
			wpa_port_ensure_up();
			wpa_port_start();
		}
		if (argc >= 5) {
			unsigned int b[6];
			unsigned char mac[6];
			int i;

			if (sscanf_bssid(argv[4], b) == 6) {
				for (i = 0; i < 6; i++) {
					mac[i] = (unsigned char) b[i];
				}
				ret = wpa_port_connect_bssid(argv[2],
				    argv[3], mac);
			} else {
				csh_printf(csh, "wpa: bad bssid %s\n",
					   argv[4]);
				return 1;
			}
		} else {
			ret = wpa_port_connect(argv[2],
			    argc >= 4 ? argv[3] : NULL);
		}
		if (ret == 0) {
			csh_printf(csh, "wpa: connecting to \"%s\"\n",
				   argv[2]);
		} else {
			csh_printf(csh, "wpa: connect failed (%d)\n", ret);
		}
		return 0;
	}
	if (strcmp(argv[1], "disconnect") == 0) {
		ret = wpa_port_disconnect();
		csh_printf(csh, "wpa: disconnect %s\n",
			   ret == 0 ? "ok" : "failed/busy");
		return 0;
	}

	usage(csh);
	return 0;
}

/* a MAC address is six hex bytes; parse it by hand because the image's
 * libc surface carries no scanf the console can rely on */
static int sscanf_bssid(const char *s, unsigned int b[6]) {
	int i;

	for (i = 0; i < 6; i++) {
		unsigned int v = 0;
		int n;

		for (n = 0; n < 2; n++) {
			char c = *s++;

			if (c >= '0' && c <= '9') {
				v = (v << 4) | (unsigned int) (c - '0');
			} else if (c >= 'a' && c <= 'f') {
				v = (v << 4) | (unsigned int) (c - 'a' + 10);
			} else if (c >= 'A' && c <= 'F') {
				v = (v << 4) | (unsigned int) (c - 'A' + 10);
			} else {
				return i;
			}
		}
		b[i] = v;
		if (i < 5) {
			if (*s != ':') {
				return i + 1;
			}
			s++;
		}
	}
	return 6;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_wpa, wpa, "wpa start/connect/status/disconnect",
			  "wpa_supplicant control (WPA2-PSK)")

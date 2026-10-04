/*
 * @file   wlan8851_cmds.c
 * @brief  The shell command of the rtl8851bu vendor-driver line (S5b W1;
 *         shape carried from the rtl8821cu sibling, V5 status face).
 *
 *   wlan8851            status: attach state, driver stopped/surprise
 *                       flags, PHL hw-init, scan table size
 *   wlan8851 up         ndo_open: efuse -> PHL init -> the embedded
 *                       firmware arrays -> chip ready
 *   wlan8851 down       ndo_stop
 *   wlan8851 scan       site survey through the vendor PHL cmd dispatcher
 *   wlan8851 bss        dump the scan table
 *   wlan8851 register   re-run the registration (D-1 guard drill)
 *   wlan8851 dbg <1-10> runtime log level (vendor rtw_cfg.c knob)
 *
 * The [S5a] W8-W11 forensics commands (regcheck/rxdbg) are sibling-line
 * diagnostics and stay out of the V5 face until a V5 round needs one
 * (接口宁少勿多).
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>
#include <string.h>

#include "csh.h"
#include "cherrysh_adapter.h"

#include "drv_types.h"
#include "rtl8851bu_port.h"

/* drv_entry.c */
extern void *rtl8851bu_get_adapter(void);
extern int wlan8851_register(void);
extern int rtl8851bu_up(void);
extern int rtl8851bu_down(void);
extern int rtl8851bu_scan(void);
extern int rtl8851bu_scan_dump(void);
/* [S5b W4] connect face */
extern int rtl8851bu_connect(const u8 *bssid, const char *ssid, int ssid_len,
			     const u8 *rsn_ie, int rsn_ie_len);
extern int rtl8851bu_disconnect(void);
extern int rtl8851bu_link_status(void);

#ifdef CONFIG_WLAN_MON
/* os/threadx/mon.c: on-demand stall dump (the 10 s periodic dump also
 * runs; this one is for "now, because I am about to issue something") */
extern void wlan_mon_dump_now(void);
#endif

static void wlan8851_status(int argc, char *argv[])
{
	_adapter *a = rtl8851bu_get_adapter();

	chry_shell_t *csh = CSH_FROM_ARGV(argc, argv);

	if (a == NULL) {
		csh_printf(csh, "rtl8851bu: not attached (dongle 0bda:b851 "
			   "enumerates directly; see usbh list)\n");
		return;
	}

	csh_printf(csh, "rtl8851bu: tick=%lu attached, drv_stopped=%s "
		   "surprise_removed=%s hw_init=%d bss=%d\n",
		   (unsigned long)jiffies,
		   dev_is_drv_stopped(adapter_to_dvobj(a)) ? "yes" : "no",
		   dev_is_surprise_removed(adapter_to_dvobj(a)) ? "yes" : "no",
		   (int)rtw_hw_is_init_completed(adapter_to_dvobj(a)),
		   (int)_rtw_queue_empty(&a->mlmepriv.scanned_queue));
}

static int cmd_wlan8851(int argc, char *argv[])
{
	chry_shell_t *csh = CSH_FROM_ARGV(argc, argv);

	if (argc < 2) {
		wlan8851_status(argc, argv);
		return 0;
	}

	if (strcmp(argv[1], "up") == 0) {
		int r = rtl8851bu_up();

		csh_printf(csh, "rtl8851bu: up %s (%d)\n",
			   (r == 0) ? "ok" : "FAIL", r);
		return 0;
	}
	if (strcmp(argv[1], "down") == 0) {
		int r = rtl8851bu_down();

		csh_printf(csh, "rtl8851bu: down %s (%d)\n",
			   (r == 0) ? "ok" : "FAIL", r);
		return 0;
	}
	if (strcmp(argv[1], "scan") == 0) {
		int r = rtl8851bu_scan();

		csh_printf(csh, "rtl8851bu: scan %s (%d)\n",
			   (r == 0) ? "done" : "REFUSED", r);
		return 0;
	}
	if (strcmp(argv[1], "connect") == 0) {
		/* W4 冒烟级: rtw_set_802_11_connect 直调（open 网络路径）。
		 * usage: wlan8851 connect <ssid> [bssid] [psk_ie_hex…]
		 * 无 psk 参数 = open（明文）；psk_ie_hex 为 RSN IE 十六进制。*/
		const char *ssid = (argc >= 3) ? argv[2] : NULL;
		const u8 *bssid = NULL;
		static u8 bssid_buf[6];
		static u8 ie_buf[256];
		int ie_len = 0;

		if (ssid == NULL) {
			csh_printf(csh, "usage: wlan8851 connect <ssid> [bssid aa:bb:..] [rsnie hex]\n");
			return 1;
		}
		if (argc >= 4) {
			unsigned int b[6];
			int i;

			if (sscanf(argv[3], "%x:%x:%x:%x:%x:%x",
				   &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
				for (i = 0; i < 6; i++)
					bssid_buf[i] = (u8)b[i];
				bssid = bssid_buf;
			}
		}
		if (argc >= 5) {
			const char *h = argv[4];
			unsigned int v;

			while (*h != '\0' && ie_len < (int)sizeof(ie_buf) &&
			       sscanf(h, "%2x", &v) == 1) {
				ie_buf[ie_len++] = (u8)v;
				h += 2;
				if (*h == ':' || *h == '-')
					h++;
			}
		}
		{
			int r = rtl8851bu_connect(bssid, ssid, (int)strlen(ssid),
			    (ie_len > 0) ? ie_buf : NULL, ie_len);

			csh_printf(csh, "rtl8851bu: connect %s (%d)\n",
				   (r == 0) ? "issued" : "FAIL", r);
		}
		return 0;
	}
	if (strcmp(argv[1], "disconnect") == 0) {
		int r = rtl8851bu_disconnect();

		csh_printf(csh, "rtl8851bu: disconnect %s (%d)\n",
			   (r == 0) ? "ok" : "FAIL", r);
		return 0;
	}
	if (strcmp(argv[1], "link") == 0) {
		(void)rtl8851bu_link_status();
		return 0;
	}
#ifdef CONFIG_WLAN_NETIF
	if (strcmp(argv[1], "ip") == 0) {
		/* DHCP/lease state probe (tcpip-thread snapshot) */
		int r = wlan8851_wlan_netif_ip();

		csh_printf(csh, "wlan8851: ip %s\n", (r == 0) ? "BOUND" : "not bound");
		return 0;
	}
#endif
	if (strcmp(argv[1], "register") == 0) {
		/* the D-1 guard drill (W2 criterion shape): a second call
		 * must print the refusal and come back -1 */
		int r = wlan8851_register();

		csh_printf(csh, "rtl8851bu: register %s (%d)\n",
			   (r == 0) ? "ok" : "REFUSED", r);
		return 0;
	}
	if (strcmp(argv[1], "bss") == 0) {
		(void)rtl8851bu_scan_dump();
		return 0;
	}
#ifdef CONFIG_WLAN_MON
	if (strcmp(argv[1], "mon") == 0) {
		wlan_mon_dump_now();
		return 0;
	}
#endif
	if (strcmp(argv[1], "status") == 0) {
		wlan8851_status(argc, argv);
		return 0;
	}
	if (strcmp(argv[1], "dbg") == 0) {
		/* the vendor log level knob (os_dep/linux/rtw_cfg.c); W1
		 * default comes from the config (RTW_LOG_LEVEL), this is the
		 * runtime escape hatch (1=always .. 6=warn 8=info 10=debug,
		 * vendor rtw_debug.h) */
		extern uint rtw_drv_log_level;

		if (argc >= 3) {
			int lvl = 0;

			if (sscanf(argv[2], "%d", &lvl) == 1 && lvl >= 1 &&
			    lvl <= 10) {
				rtw_drv_log_level = (uint)lvl;
				csh_printf(csh, "rtl8851bu: drv log level = %u\n",
					   rtw_drv_log_level);
				return 0;
			}
			csh_printf(csh, "rtl8851bu: level must be 1..10\n");
			return 1;
		}
		csh_printf(csh, "rtl8851bu: drv log level = %u\n",
			   rtw_drv_log_level);
		return 0;
	}
	csh_printf(csh, "usage: wlan8851 [up|down|scan|bss|connect <ssid> [bssid] [rsnie]|disconnect|link|ip|status|register|dbg <1-10>|mon]\n");
	return -1;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_wlan8851, wlan8851,
			  "wlan8851 [up|down|scan|bss|status|register|dbg|mon]",
			  "drive the rtl8851bu vendor driver (attach/up/scan)");

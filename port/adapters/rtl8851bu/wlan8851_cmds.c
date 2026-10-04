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
	csh_printf(csh, "usage: wlan8851 [up|down|scan|bss|status|register|dbg <1-10>]\n");
	return -1;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_wlan8851, wlan8851,
			  "wlan8851 [up|down|scan|bss|status|register|dbg]",
			  "drive the rtl8851bu vendor driver (attach/up/scan)");

/*
 * @file   wlan8821_cmds.c
 * @brief  The shell command of the rtl8821cu vendor-driver line (S5a W1;
 *         shape carried from the [FWC] adapter, rename only).
 *
 *   wlan8821            status: attach state, driver stopped/surprise
 *                       flags, RX counters
 *   wlan8821 up         ndo_open: efuse -> HALMAC init -> the embedded
 *                       firmware array -> chip ready
 *   wlan8821 down       ndo_stop
 *   wlan8821 scan       site survey through the vendor MLME
 *   wlan8821 bss        dump the scan table
 *   wlan8821 register   re-run the registration (D-1 guard drill)
 *
 * Command output goes through csh_printf (interactive, no timestamps);
 * the driver's own asynchronous prints (attach, scan done) land on the
 * stamped console sink like every other non-interactive line.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>
#include <string.h>

#include "csh.h"
#include "cherrysh_adapter.h"

#include "drv_types.h"
#include "hal_data.h"
#include "rtl8821cu_port.h"
#include "usb_ops_linux.h"

/* drv_entry.c */
extern void *rtl8821cu_get_adapter(void);
extern int wlan8821_register(void);
extern int rtl8821cu_up(void);
extern int rtl8821cu_down(void);
extern int rtl8821cu_scan(void);
extern int rtl8821cu_scan_dump(void);
extern unsigned long rtl8821cu_rx_stats(unsigned long *bytes);
#ifdef CONFIG_NIC_WPA_BACKEND
extern unsigned long rtl8821cu_wpa_rx_counters(unsigned long *eapol,
					       unsigned long *data_drop);
#endif

static void wlan8821_status(int argc, char *argv[])
{
	_adapter *a = rtl8821cu_get_adapter();
	unsigned long rx_bytes = 0;
	unsigned long rx_frames;

	chry_shell_t *csh = CSH_FROM_ARGV(argc, argv);

	if (a == NULL) {
		csh_printf(csh, "rtl8821cu: not attached (dongle 0bda:1a2b "
			   "ejects into 0bda:c820; see usbh list)\n");
		return;
	}

	rx_frames = rtl8821cu_rx_stats(&rx_bytes);
	csh_printf(csh, "rtl8821cu: tick=%lu attached, drv_stopped=%s "
		   "surprise_removed=%s hw_init=%d rx=%lu/%luB\n",
		   (unsigned long)jiffies,
		   rtw_is_drv_stopped(a) ? "yes" : "no",
		   rtw_is_surprise_removed(a) ? "yes" : "no",
		   (int)rtw_get_hw_init_completed(a),
		   rx_frames, rx_bytes);
#ifdef CONFIG_NIC_WPA_BACKEND
	{
		unsigned long eapol = 0, drop = 0;

		rtl8821cu_wpa_rx_counters(&eapol, &drop);
		csh_printf(csh, "rtl8821cu: wpa eapol_rx=%lu data_drop=%lu\n",
			   eapol, drop);
	}
#endif
#ifdef CONFIG_WLAN_NETIF
	{
		unsigned long fed = 0, drop = 0, txf = 0, txe = 0;

		rtl8821cu_wlan_netif_stats(&fed, &drop, &txf, &txe);
		csh_printf(csh, "rtl8821cu: lwip rx_fed=%lu rx_drop=%lu "
			   "last_rx_ms=%lu tx=%lu tx_err=%lu\n", fed, drop,
			   rtl8821cu_wlan_netif_last_rx_ms(), txf, txe);
	}
#endif
	/* [W6 probe, command-face readout] vendor RX-path counter line */
	{
		extern void rtw_rxdbg_dump(void);

		rtw_rxdbg_dump();
		lkpi_tasklet_report(&a->recvpriv.recv_tasklet);
		/* the lifecycle watermarks: vendor-side in-flight urbs, the
		 * completed-skbs queue (tasklet lag) and the free pool sizes */
		csh_printf(csh, "rtl8821cu: rxlife pend=%d rxq=%u freeq=%u "
			   "freebuf=%u\n",
			   ATOMIC_READ(&a->recvpriv.rx_pending_cnt),
			   (unsigned)skb_queue_len(&a->recvpriv.rx_skb_queue),
			   (unsigned)skb_queue_len(
				   &a->recvpriv.free_recv_skb_queue),
			   (unsigned)a->recvpriv.free_recv_buf_queue_cnt);
	}
}

static int wlan8821_regcheck(chry_shell_t *csh)
{
	_adapter *a = rtl8821cu_get_adapter();
	u32 mac = 0;
	u32 fw = 0;
	u32 rf_power = 0;
	u32 hmetfr = 0;
	u32 rf18, rf42;
	int mac_ret, fw_ret, rf_power_ret, hmetfr_ret;

	if (a == NULL) {
		csh_printf(csh, "rtl8821cu: regcheck requires attach\n");
		return 1;
	}

	/* Keep the raw EP0 result: usb_read8() discards its transfer status. */
	mac_ret = usbctrl_vendorreq(&a->iopriv.intf, 0x05, REG_SYS_FUNC_EN,
				 0, &mac, 1, 0x01);
	fw_ret = usbctrl_vendorreq(&a->iopriv.intf, 0x05, REG_MCUFW_CTRL_8821C,
				0, &fw, 1, 0x01);
	/* PHYDM refuses every RF access unless MAC 0x1c[31:24] is 0x07. */
	rf_power_ret = usbctrl_vendorreq(&a->iopriv.intf, 0x05, 0x1c,
					  0, &rf_power, 4, 0x01);
	hmetfr_ret = usbctrl_vendorreq(&a->iopriv.intf, 0x05, REG_HMETFR,
					0, &hmetfr, 1, 0x01);
	csh_printf(csh, "rtl8821cu: regcheck ep0 sys=0x%02x ret=%d "
		   "fw=0x%02x ret=%d rf_power=0x%08x ret=%d gate=%s "
		   "hmetfr=0x%02x ret=%d\n",
		   mac & 0xffu, mac_ret, fw & 0xffu, fw_ret,
		   rf_power, rf_power_ret,
		   rf_power_ret == 4 && (rf_power >> 24) == 0x07u ?
		   "on" : "off/unknown", hmetfr & 0xffu, hmetfr_ret);
	if (mac_ret != 1 || fw_ret != 1 || rf_power_ret != 4 ||
	    hmetfr_ret != 1)
		return 1;

	rf18 = rtw_hal_read_rfreg(a, 0, 0x18, 0xfffff);
	rf42 = rtw_hal_read_rfreg(a, 0, 0x42, 0xfffff);
	csh_printf(csh, "rtl8821cu: regcheck rf18=0x%08x %s "
		   "rf42=0x%08x %s\n", rf18,
		   rf18 != 0xffffffffu ? "valid" : "FAIL", rf42,
		   rf42 != 0xffffffffu ? "valid" : "FAIL");
	return rf18 == 0xffffffffu || rf42 == 0xffffffffu;
}

static int cmd_wlan8821(int argc, char *argv[])
{
	chry_shell_t *csh = CSH_FROM_ARGV(argc, argv);

	if (argc < 2) {
		wlan8821_status(argc, argv);
		return 0;
	}

	if (strcmp(argv[1], "up") == 0) {
		int r = rtl8821cu_up();

		csh_printf(csh, "rtl8821cu: up %s (%d)\n",
			   (r == 0) ? "ok" : "FAIL", r);
		return 0;
	}
	if (strcmp(argv[1], "down") == 0) {
		int r = rtl8821cu_down();

		csh_printf(csh, "rtl8821cu: down %s (%d)\n",
			   (r == 0) ? "ok" : "FAIL", r);
		return 0;
	}
	if (strcmp(argv[1], "scan") == 0) {
		int r = rtl8821cu_scan();

		csh_printf(csh, "rtl8821cu: scan %s (%d)\n",
			   (r == 0) ? "issued" : "REFUSED", r);
		return 0;
	}
	if (strcmp(argv[1], "register") == 0) {
		/* the D-1 guard drill (W2 criterion 3): a second call must
		 * print the refusal and come back -1 */
		int r = wlan8821_register();

		csh_printf(csh, "rtl8821cu: register %s (%d)\n",
			   (r == 0) ? "ok" : "REFUSED", r);
		return 0;
	}
	if (strcmp(argv[1], "bss") == 0) {
		(void)rtl8821cu_scan_dump();
		return 0;
	}
	if (strcmp(argv[1], "status") == 0) {
		wlan8821_status(argc, argv);
		return 0;
	}
	if (strcmp(argv[1], "regcheck") == 0)
		return wlan8821_regcheck(csh);
	if (strcmp(argv[1], "dbg") == 0) {
		/* [W7 print-clean] the vendor log level is WARNING by default;
		 * this is the runtime escape hatch for a debug round
		 * (1=always .. 6=warn 8=info 10=debug, vendor rtw_debug.h) */
		extern uint rtw_drv_log_level;

		if (argc >= 3) {
			int lvl = 0;

			if (sscanf(argv[2], "%d", &lvl) == 1 && lvl >= 1 &&
			    lvl <= 10) {
				rtw_drv_log_level = (uint)lvl;
				csh_printf(csh, "rtl8821cu: drv log level = %u\n",
					   rtw_drv_log_level);
				return 0;
			}
			csh_printf(csh, "rtl8821cu: level must be 1..10\n");
			return 1;
		}
		csh_printf(csh, "rtl8821cu: drv log level = %u\n",
			   rtw_drv_log_level);
		return 0;
	}
	csh_printf(csh, "usage: wlan8821 [up|down|scan|bss|status|regcheck|register|dbg <1-10>]\n");
	return -1;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_wlan8821, wlan8821,
			  "wlan8821 [up|down|scan|bss|status|regcheck|register]",
			  "drive the rtl8821cu vendor driver (attach/up/scan)");

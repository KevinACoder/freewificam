/*
 * @file
 * @brief The kernelmod line's module entry.
 *
 * Two shapes share this file:
 *
 *  - WITH_DRIVER=0 (K1): the module carries the imported net80211 stack, its
 *    port layer and the bridge, and the entry runs a dummy-seam self-test
 *    (`wlan_ko: self-test attach rc=0`). This is the "stack is alive inside the
 *    kernel" anchor.
 *  - WITH_DRIVER=1 (K2+): the module also carries the verbatim rtl8xxxu driver
 *    and the mac80211 substitute; the entry registers the USB driver through
 *    the glue in ports/linux/driver_glue.h. The bridge attach then happens from
 *    the driver's probe path (ieee80211_register_hw), so the dummy self-test is
 *    compiled out - the bridge is a single-adapter singleton.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <stdint.h>

#include "wlan_linux.h"
#include <wlan_bridge.h>

#ifdef CONFIG_WLAN_KO_FW_RTW8821C
int lkpi_fw_rtw8821c_fw_register(void);
#endif
#ifdef CONFIG_WLAN_KO_FW_RTW8851B
int lkpi_fw_rtw8851b_fw_register(void);
#endif

#ifdef CONFIG_WLAN_KO_DRIVER

static int __init wlan_ko_init(void)
{
	int rc;

	pr_info("wlan_ko: loading (kernelmod line, driver world)\n");
#ifdef CONFIG_WLAN_KO_FW_RTW8821C
	rc = lkpi_fw_rtw8821c_fw_register();
	if (rc != 0) {
		pr_err("wlan_ko: firmware blob registration failed rc=%d\n", rc);
		return rc;
	}
	pr_info("wlan_ko: firmware blob registered (rtw88/rtw8821c_fw.bin)\n");
#endif
#ifdef CONFIG_WLAN_KO_FW_RTW8851B
	rc = lkpi_fw_rtw8851b_fw_register();
	if (rc != 0) {
		pr_err("wlan_ko: firmware blob registration failed rc=%d\n", rc);
		return rc;
	}
	pr_info("wlan_ko: firmware blob registered (rtw89/rtw8851b_fw-1.bin)\n");
#endif
	rc = wlan_driver_register();
	pr_info("wlan_ko: usb driver registered rc=%d\n", rc);
	return 0;
}

static void __exit wlan_ko_exit(void)
{
	wlan_control_exit();
	wlan_driver_unregister();
	wlan_bridge_detach();
	pr_info("wlan_ko: unloaded\n");
}

#else /* K1: stack-only self-test */

#include <seam.h>
#include <wlan_bridge.h>
#include <osal/wlan_osal.h>

static int dummy_start(struct wlan_seam_drv *drv)
{
	(void)drv;
	return 0;
}

static int dummy_stop(struct wlan_seam_drv *drv)
{
	(void)drv;
	return 0;
}

static int dummy_add_interface(struct wlan_seam_drv *drv, const uint8_t mac[6],
    int iftype)
{
	(void)drv;
	(void)mac;
	(void)iftype;
	return 0;
}

static int dummy_config(struct wlan_seam_drv *drv, uint32_t changed,
    uint32_t freq)
{
	(void)drv;
	(void)changed;
	(void)freq;
	return 0;
}

static int dummy_filter(struct wlan_seam_drv *drv, uint32_t changed,
    uint32_t total)
{
	(void)drv;
	(void)changed;
	(void)total;
	return 0;
}

static int dummy_sw_scan_start(struct wlan_seam_drv *drv)
{
	(void)drv;
	return 0;
}

static void dummy_sw_scan_complete(struct wlan_seam_drv *drv)
{
	(void)drv;
}

static int dummy_has_hw_scan(struct wlan_seam_drv *drv)
{
	(void)drv;
	return 0;
}

static int dummy_hw_scan_start(struct wlan_seam_drv *drv,
    const uint32_t *freqs, size_t n_freqs)
{
	(void)drv;
	(void)freqs;
	(void)n_freqs;
	return -1;
}

static void dummy_hw_scan_cancel(struct wlan_seam_drv *drv)
{
	(void)drv;
}

static int dummy_tx(struct wlan_seam_drv *drv, const uint8_t *frame,
    size_t len, int is_mgmt, int hw_crypto, int pairwise, int key_idx)
{
	(void)drv;
	(void)frame;
	(void)len;
	(void)is_mgmt;
	(void)hw_crypto;
	(void)pairwise;
	(void)key_idx;
	return -1;
}

static int dummy_set_key(struct wlan_seam_drv *drv, int pairwise, int key_idx,
    int cipher, const uint8_t *key, size_t key_len)
{
	(void)drv;
	(void)pairwise;
	(void)key_idx;
	(void)cipher;
	(void)key;
	(void)key_len;
	return 0;
}

static int dummy_clear_key(struct wlan_seam_drv *drv, int pairwise, int key_idx)
{
	(void)drv;
	(void)pairwise;
	(void)key_idx;
	return 0;
}

static void dummy_bss_info(struct wlan_seam_drv *drv, int joined,
    const uint8_t bssid[6], uint32_t freq, uint16_t aid,
    const uint8_t *rates, size_t rates_len)
{
	(void)drv;
	(void)joined;
	(void)bssid;
	(void)freq;
	(void)aid;
	(void)rates;
	(void)rates_len;
}

static int dummy_bssid(struct wlan_seam_drv *drv, const uint8_t bssid[6],
    uint16_t beacon_int, uint8_t dtim_period)
{
	(void)drv;
	(void)bssid;
	(void)beacon_int;
	(void)dtim_period;
	return 0;
}

static const struct wlan_seam_drv_ops dummy_ops = {
	.start = dummy_start,
	.stop = dummy_stop,
	.add_interface = dummy_add_interface,
	.config = dummy_config,
	.filter = dummy_filter,
	.sw_scan_start = dummy_sw_scan_start,
	.sw_scan_complete = dummy_sw_scan_complete,
	.has_hw_scan = dummy_has_hw_scan,
	.hw_scan_start = dummy_hw_scan_start,
	.hw_scan_cancel = dummy_hw_scan_cancel,
	.tx = dummy_tx,
	.set_key = dummy_set_key,
	.clear_key = dummy_clear_key,
	.bss_info = dummy_bss_info,
	.bssid = dummy_bssid,
};

static char dummy_drv_handle;

static int __init wlan_ko_init(void)
{
	static const uint8_t mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
	unsigned long blocks_before = wlan_osal_live_blocks();
	int rc;

	pr_info("wlan_ko: loading (kernelmod line stack self-test)\n");
	rc = wlan_bridge_attach((struct wlan_seam_drv *)&dummy_drv_handle,
	    &dummy_ops, mac);
	pr_info("wlan_ko: self-test attach rc=%d live_blocks=%lu->%lu\n",
	    rc, blocks_before, wlan_osal_live_blocks());
	return 0;
}

static void __exit wlan_ko_exit(void)
{
	wlan_bridge_detach();
	pr_info("wlan_ko: self-test detach done, live blocks=%lu\n",
	    wlan_osal_live_blocks());
}

#endif /* CONFIG_WLAN_KO_DRIVER */

module_init(wlan_ko_init);
module_exit(wlan_ko_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("kernelmod line: net80211 + bridge (+ verbatim rtl8xxxu) in the Linux kernel");

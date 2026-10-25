/*
 * @file
 * @brief R1 HAL self-test entry for the RTL8189FTV.
 *
 * The module registers one SDIO driver for 024c:f179. The probe is the first
 * front-end step: it only announces the function and reads the SDIO vendor and
 * device IDs; the adapter/HAL bring-up (probe -> firmware -> MAC/channel) is
 * added on top of the same entry. The vendor tree supplies the HAL, SDIO and
 * firmware translation units; this file and the stubs beside it are the
 * first-party cut.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>

#define RTL8189F_VENDOR	0x024c
#define RTL8189F_DEVICE	0xf179

/* adapter.c: the HAL-facing bring-up (SDIO enable -> dvobj -> adapter ->
 * rtw_hal_init -> firmware/channel). */
extern int rtl8189f_attach(struct sdio_func *func);
extern void rtl8189f_detach(struct sdio_func *func);
extern int rtl8189f_mac80211_register(void);
extern void rtl8189f_mac80211_unregister(void);

static int rtl8189f_probe(struct sdio_func *func,
    const struct sdio_device_id *id)
{
	int ret;

	pr_info("rtl8189f: sdio probe %04x:%04x function %u\n",
	    func->vendor, func->device, func->num);
	ret = rtl8189f_attach(func);
	if (ret)
		return ret;
	ret = rtl8189f_mac80211_register();
	if (ret) {
		rtl8189f_detach(func);
		return ret;
	}
	return 0;
}

static void rtl8189f_remove(struct sdio_func *func)
{
	pr_info("rtl8189f: sdio remove %04x:%04x\n", func->vendor, func->device);
	rtl8189f_mac80211_unregister();
	rtl8189f_detach(func);
}

static const struct sdio_device_id rtl8189f_ids[] = {
	{ SDIO_DEVICE(RTL8189F_VENDOR, RTL8189F_DEVICE) },
	{ }
};
MODULE_DEVICE_TABLE(sdio, rtl8189f_ids);

static struct sdio_driver rtl8189f_sdio_driver = {
	.name		= "rtl8189f",
	.id_table	= rtl8189f_ids,
	.probe		= rtl8189f_probe,
	.remove		= rtl8189f_remove,
};

module_sdio_driver(rtl8189f_sdio_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RTL8189FTV HAL self-test (R1)");
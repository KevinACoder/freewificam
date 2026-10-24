/*
 * @file
 * @brief iwlwifi arm: module entry glue (opmode first, PCI driver second).
 *
 * The real driver ships as two modules: iwlwifi.ko (core + PCIe transport,
 * whose module_init registers the PCI driver) and iwlmvm.ko (opmode + rate
 * control, whose module_init registers the opmode). A single kernelmod module
 * carries both, so driver_glue.h redirects their module_init/module_exit to
 * wlan_modinit_<fn> / wlan_modexit_<fn> and this file calls them in load order.
 *
 * Load order is the real two-module order: iwlwifi core first (it initialises
 * the opmode table's per-opmode drv lists, then registers the PCI driver), then
 * iwlmvm (registers the opmode).  The core's probe loads firmware and puts the
 * drv on the "iwlmvm" list even when the opmode is not registered yet
 * (request_module_nowait); iwl_mvm_init then walks that list and starts the
 * opmode.  Doing mvm first would walk the not-yet-INIT_LIST_HEAD'ed list and
 * NULL-deref.  Unload is the reverse.
 */

#ifdef CONFIG_WLAN_KO_DRIVER_IWLWIFI

#include <linux/kernel.h>
#include <linux/module.h>

extern int wlan_modinit_iwl_mvm_init(void);
extern int wlan_modinit_iwl_drv_init(void);
extern void wlan_modexit_iwl_drv_exit(void);
extern void wlan_modexit_iwl_mvm_exit(void);

int wlan_driver_register(void)
{
	int rc;

	rc = wlan_modinit_iwl_drv_init();
	if (rc != 0) {
		pr_err("wlan_ko: iwlwifi core init failed rc=%d\n", rc);
		return rc;
	}
	rc = wlan_modinit_iwl_mvm_init();
	if (rc != 0) {
		pr_err("wlan_ko: iwlmvm opmode registration failed rc=%d\n", rc);
		wlan_modexit_iwl_drv_exit();
		return rc;
	}
	return 0;
}

void wlan_driver_unregister(void)
{
	wlan_modexit_iwl_mvm_exit();
	wlan_modexit_iwl_drv_exit();
}

#endif /* CONFIG_WLAN_KO_DRIVER_IWLWIFI */

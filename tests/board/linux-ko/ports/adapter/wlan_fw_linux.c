/*
 * @file
 * @brief Firmware lookup for the bridge, on the kernelmod line.
 *
 * The RTOS images serve the driver's firmware through the project's own
 * registry (components/linuxkpi/fw_registry.c), and the rtw88 build compiles
 * that registry and the 8821CU blob into wlan_ko.ko (CONFIG_WLAN_KO_FW_EMBED):
 * the driver's request_firmware_nowait() and the bridge's accessor both read
 * the one compiled-in table. The 8188 build keeps the kernel's firmware_class
 * and returns -ENOENT here, as before.
 */

#include <linux/kernel.h>
#include <linux/errno.h>

#include <stdint.h>
#include <stddef.h>

#include "wlan_bridge.h"
#ifdef CONFIG_WLAN_KO_FW_EMBED
#include "fw_registry.h"
#endif

int wlan_bridge_fw_lookup(const char *name, const uint8_t **data, size_t *size)
{
#ifdef CONFIG_WLAN_KO_FW_EMBED
	const struct lkpi_fw_entry *entry;

	if (name == NULL || data == NULL || size == NULL) {
		return -EINVAL;
	}
	entry = lkpi_fw_registry_find(name);
	if (entry == NULL) {
		return -ENOENT;
	}
	*data = entry->data;
	*size = entry->size;
	return 0;
#else
	(void)name;
	(void)data;
	(void)size;
	return -ENOENT;
#endif
}

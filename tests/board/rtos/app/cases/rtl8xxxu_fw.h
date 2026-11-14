/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtl8xxxu_fw.h
 * @brief  Which compiled-in firmware image this rtl8xxxu profile carries.
 *
 * The driver's probe asks request_firmware() for the chip's own name; the
 * image has no filesystem, so the registry is filled by the one blob the
 * profile linked.  The 8188EUS profile carries rtlwifi/rtl8188eufw.bin
 * (28.0 / 0x88e1), the RTL8723BU profile carries rtlwifi/rtl8723bu_nic.bin
 * (35.0 / 0x5301).  Both register under the driver's literal MODULE_FIRMWARE
 * name, and the cases print the name they registered - so the boot's own line
 * says which blob the image carried instead of leaving it to the profile.
 */

#ifndef RTL8XXXU_FW_H
#define RTL8XXXU_FW_H

#if defined(CONFIG_RTL8XXXU_CHIP_8723BU) && CONFIG_RTL8XXXU_CHIP_8723BU
int lkpi_fw_rtl8723bu_nic_register(void);
#define RTL8XXXU_FW_NAME "rtlwifi/rtl8723bu_nic.bin"
static inline int rtl8xxxu_fw_register(void)
{
	return lkpi_fw_rtl8723bu_nic_register();
}
#else
int lkpi_fw_rtl8188eufw_register(void);
#define RTL8XXXU_FW_NAME "rtlwifi/rtl8188eufw.bin"
static inline int rtl8xxxu_fw_register(void)
{
	return lkpi_fw_rtl8188eufw_register();
}
#endif

#endif /* RTL8XXXU_FW_H */

/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/efi.h
 * @brief   The EFI runtime-services face: the include a driver writes when it
 *          can read a firmware variable.
 *
 * @design declaration-only compatibility face. The interface (include/linux/efi.h)
 *         carries the EFI configuration-table access, the runtime-services call
 *         surface (efi.get_variable and the efi_status_t/efi_guid_t types) and
 *         the EFI_GUID() spelling.
 *
 *         This port has no EFI: it is a bare-metal image started from its own
 *         vector table, there is no EFI system table and no runtime-services
 *         callback to call (brcmfmac's driver_config.h states CONFIG_EFI off).
 *         The one compiled unit that includes this header - brcmfmac/firmware.c -
 *         guards every EFI name with `#ifdef CONFIG_EFI`, so its
 *         brcmf_fw_nvram_from_efi() is the closed arm's `return NULL` and no EFI
 *         symbol is named by the compiled set. The header is present because the
 *         driver's include line is unconditional; it carries no declaration of
 *         its own, and it must not pretend the runtime services exist - a driver
 *         compiled into a port that has none would be reading a call through a
 *         table that is not there.
 * @date    19.11.2026
 */

#ifndef _LINUXKPI_LINUX_EFI_H_
#define _LINUXKPI_LINUX_EFI_H_

#endif /* _LINUXKPI_LINUX_EFI_H_ */

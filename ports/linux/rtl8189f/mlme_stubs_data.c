/*
 * @file
 * @brief Data symbols the R1 HAL-only keep-set still references.
 *
 * The four OUI/cipher constants are protocol constants copied from the vendor
 * MLME tuple; the rest are module-parameter globals that the vendor os_intfs.c
 * used to define. Diagnostic only; the front-end owns them once it replaces
 * the os_intfs face.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <linux/types.h>

unsigned char RTW_WPA_OUI[] = {0x00, 0x50, 0xf2, 0x01};
unsigned char WMM_PARA_OUI[] = {0x00, 0x50, 0xf2, 0x02, 0x01, 0x01};
unsigned char WPA_TKIP_CIPHER[4] = {0x00, 0x50, 0xf2, 0x02};
unsigned char RSN_TKIP_CIPHER[4] = {0x00, 0x0f, 0xac, 0x02};

unsigned int rtw_drv_log_level = 4;
char *rtw_initmac;
int rtw_mc2u_disable;
char *rtw_phy_file_path = "/lib/firmware/";

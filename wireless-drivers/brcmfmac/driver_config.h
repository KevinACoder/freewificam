/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    driver_config.h
 * @brief   The configuration face of the vendored brcmfmac (BCM43362/AP6181,
 *          SDIO).
 *
 * @design The driver is written against a kernel Kconfig tree; this project
 *         has one configuration channel and no `-D` on any compile line
 *         (docs/BUILD.md), so the answers are restated here once and the
 *         header is force-included in the driver's flag world (the build's
 *         BRCMFMAC_CFLAGS) and nowhere else.  A feature is on only when its
 *         symbol is named; everything else is off by omission.
 *
 *         The face is the SDIO bus with the vendor's broadcast-core protocol
 *         (BCDC), which is what the 43362 speaks.  PCIe, USB, MSGBUF, debug,
 *         tracing, OF/ACPI/DMI/PM are off: this port supplies its own bus and
 *         has no device tree or runtime power management.
 *
 *         Byte pin: the imported brcm80211/ tree's own digest manifest is
 *         `wireless-drivers/brcmfmac/IMPORT.tsv`; `import-driver.py --check`
 *         re-derives it.
 * @date    19.11.2026
 */

#ifndef _BRCMFMAC_DRIVER_CONFIG_H_
#define _BRCMFMAC_DRIVER_CONFIG_H_

/* --- the compiled set ---------------------------------------------------- */
#define CONFIG_BRCMUTIL		1
#define CONFIG_BRCMFMAC		1
#define CONFIG_BRCMFMAC_SDIO	1
#define CONFIG_BRCMFMAC_PROTO_BCDC 1

/* --- stated-off answers --------------------------------------------------
 *
 * Left undefined on purpose (the upstream Kconfig's other buses and the
 * optional debug/tracing/power surfaces): CONFIG_BRCMSMAC,
 * CONFIG_BRCMFMAC_PROTO_MSGBUF, CONFIG_BRCMFMAC_USB, CONFIG_BRCMFMAC_PCIE,
 * CONFIG_BRCMDBG, CONFIG_BRCM_TRACING, CONFIG_OF, CONFIG_DMI, CONFIG_ACPI,
 * CONFIG_PM.
 */

#endif /* _BRCMFMAC_DRIVER_CONFIG_H_ */

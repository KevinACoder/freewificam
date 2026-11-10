/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux80211/driver_config.h
 * @brief   The configuration face of the vendored iwlwifi (AC 7260 / mvm) tree.
 *
 * @design The driver's translation units are written against the kernel's
 *         Kconfig answers, which arrive as `autoconf.h`.  This project has one
 *         configuration channel (`docs/BUILD.md` §1) and no `-D` on any compile
 *         line, so the answers are stated here once, in the same shape the
 *         kernel's generated header has: a feature is *defined* when it is on
 *         and *absent* when it is off.  The distinction matters: iwlwifi guards
 *         thermal/tso/acpi/efi/leds/pm paths with `#ifdef`, so defining the
 *         symbol to 0 would enable them.
 *
 *         The compiled set (`wireless-drivers/iwlwifi/sources.mk`, 72 TUs) is
 *         the feature set: IWLMVM is the op-mode the 7260 uses; the optional
 *         units (debugfs, leds, d3, acpi, uefi, device tracing, dvm, mld, mei)
 *         are not compiled, and their symbols stay undefined so every `#ifdef`
 *         arm stays closed.
 * @date    10.11.2026
 */

#ifndef _LINUX80211_DRIVER_CONFIG_H_
#define _LINUX80211_DRIVER_CONFIG_H_

/* The device family this line drives: the 7260 is an IWLMVM part. */
#define CONFIG_IWLWIFI				1
#define CONFIG_IWLMVM				1
/* The driver's two-module shape; the image links both halves statically. */
#define CONFIG_IWLWIFI_OPMODE_MODULAR		1

/*
 * The RX path allocates `alloc_skb(128)` and then hands the frame to mac80211,
 * which linearizes management frames.  This port's linearize cannot pull page
 * fragments in, and the pool's data slot is 2 KiB, so the driver's small
 * allocation is raised to the slot size: the frame then stays linear and the
 * stack's own receive path sees the beacon/probe-response bytes.  Stack and
 * host allocations keep the exact size they ask for.
 */
#define LKPI_ALLOC_SKB_MIN			2048u

/*
 * Deliberately absent (the `#ifdef` arms stay closed):
 *
 *   CONFIG_IWLDVM, CONFIG_IWLMLD, CONFIG_IWLMEI
 *   CONFIG_IWLWIFI_DEBUGFS, CONFIG_IWLWIFI_DEVICE_TRACING
 *   CONFIG_IWLWIFI_LEDS, CONFIG_PM_SLEEP, CONFIG_ACPI, CONFIG_EFI
 *   CONFIG_THERMAL, CONFIG_INET
 *
 * Each has a price noted in the round's ADR: no thermal zone (mvm/tt.c), no
 * TSO/A-MSDU-in-driver (pcie/gen1_2/tx.c), no debugfs/leds/d3, no ACPI/EFI
 * tables.  The 7260's probe/ALIVE/scan/join do not need them; C6 (throughput)
 * will revisit CONFIG_INET.
 */

#endif /* _LINUX80211_DRIVER_CONFIG_H_ */

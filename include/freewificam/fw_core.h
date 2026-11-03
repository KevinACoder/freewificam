/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    fw_core.h
 * @brief   Core identity and lifecycle — status: lifecycle NOT READY.
 *
 * There is no `fw_init()`/`fw_run()` in this checkpoint.  The RK3568 image
 * entry is application-owned (`tests/board/rtos/app/main.c`): it brings up the
 * polled UART, initialises the kernel through CMSIS-RTOS2, starts the runner
 * thread and starts the scheduler.  A cross-OS consumer cannot call that
 * sequence today, and inventing an empty `fw_init()` would only hide the gap.
 *
 * What this header does provide is the two things every consumer needs before
 * it can even compile against the core:
 *
 *   - `fw_config.h`: the one generated configuration header (this profile's
 *     data).  There are no -D configuration macros and no fallbacks.
 *   - `fw_osal.h`: the CMSIS-RTOS2 ABI the libraries are built against.  The
 *     consumer supplies the implementation; the SDK does not.
 *
 * The control and data faces are separate headers (`fw_wifi.h`,
 * `fw_netif.h`) and are not pulled in here, so a consumer that only wants the
 * configuration does not drag in the net80211 port surface.
 *
 * What L2 needs before this header can carry a lifecycle API (tracked in
 * docs/PORTING.md):
 *
 *   - one explicit `fw_init(const struct fw_port *)` and `fw_run()` with no
 *     global constructors and no linker-script dependence;
 *   - a port descriptor carrying the OS hooks, the bus adapter and the netif
 *     operations, so the core never names an RTOS or a host stack;
 *   - a `-Wl,--whole-archive`/section-registration audit: the current tree
 *     uses `BOARD_TESTS`/`FSymTab` sections in the app layer, which a library
 *     consumer must either keep or replace with explicit registration.
 */

#ifndef FREEWIFICAM_FW_CORE_H
#define FREEWIFICAM_FW_CORE_H

#include "fw_config.h"
#include "fw_osal.h"

/* A consumer can gate code on this instead of guessing from the release. */
#define FW_CORE_API_READY 0

#endif /* FREEWIFICAM_FW_CORE_H */

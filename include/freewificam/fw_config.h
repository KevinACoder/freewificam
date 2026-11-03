/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    fw_config.h
 * @brief   The one configuration header of the published SDK.
 *
 * Every image is one defconfig.  `tools/build/kconfig-gen.py` renders that
 * defconfig into `build/kconfig/<profile>/freewificam_config.h`, and
 * `make.rtos sdk` installs that file next to this wrapper as
 * `freewificam_config.h`.  There are no fallback defaults in the build or in
 * C: an image without a generated header fails at the include below, which is
 * the intended behaviour (a silent default would let two images disagree
 * about the configuration while both build).
 *
 * Rules for consumers:
 *
 *   - Include THIS header, not the generated one by name; the generated name
 *     is an SDK implementation detail and exists only as the installed file.
 *   - Do not pass -D for any configuration value.  The header is the only
 *     configuration channel, so a consumer-built object and the archive it
 *     links were configured by the same values or the build is wrong.
 *   - The configuration is per image: two profiles are two ABIs.  A consumer
 *     that needs another configuration rebuilds the SDK for that profile
 *     (`./make.rtos sdk PROFILE=<name>`); there is no cross-profile ABI.
 *   - A symbol that the defconfig did not set is absent (the generated header
 *     carries a "CONFIG_X is not set" comment), not zero-initialised.  Use
 *     `#if defined(CONFIG_X)` when a feature may be off; do not rely on a
 *     default that the image did not choose.
 *
 * The generated header carries the CONFIG_* symbols plus the legacy macro
 * aliases that vendored code expects (for example `ULOG_ENABLED`).  Those
 * aliases are part of the generated contract, not hand-written here.
 */

#ifndef FREEWIFICAM_FW_CONFIG_H
#define FREEWIFICAM_FW_CONFIG_H

/* Installed by `make.rtos sdk` from the profile's generated header. */
#include "freewificam_config.h"

/*
 * Compile-time identity a consumer may gate on without linking anything.  A
 * missing one means the SDK was assembled from the wrong profile or from a
 * generated header older than this wrapper.
 */
#ifndef CONFIG_PROFILE_NAME
#error "freewificam: freewificam_config.h is missing CONFIG_PROFILE_NAME"
#endif
#ifndef CONFIG_BOARD_NAME
#error "freewificam: freewificam_config.h is missing CONFIG_BOARD_NAME"
#endif
#ifndef CONFIG_BACKEND_NAME
#error "freewificam: freewificam_config.h is missing CONFIG_BACKEND_NAME"
#endif

#endif /* FREEWIFICAM_FW_CONFIG_H */

/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    fsl_debug_console.h
 * @brief   The one NXP SDK header the imported tree includes from its
 *          CONFIG_FREERTOS branch (src/utils/includes.h): the console print
 *          macro.  The port prints through the image's own printf.
 */
#ifndef FSL_DEBUG_CONSOLE_H
#define FSL_DEBUG_CONSOLE_H

#include <stdio.h>

#ifndef PRINTF
#define PRINTF printf
#endif

#endif /* FSL_DEBUG_CONSOLE_H */

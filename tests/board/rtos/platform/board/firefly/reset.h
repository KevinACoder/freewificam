/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   reset.h
 * @brief  System reset through the board's PSCI conduit (firefly family
 *         interface).
 */

#ifndef FREEWIFICAM_RESET_H
#define FREEWIFICAM_RESET_H

/* PSCI SYSTEM_RESET through the SMC conduit. Returns 0 when the firmware
 * accepted the call (the reset then never lets this return); nonzero is the
 * PSCI error the firmware answered with. */
int board_system_reset(void);

#endif /* FREEWIFICAM_RESET_H */

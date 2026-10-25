/*
 * @file   reset.h
 * @brief  System reset through the board's PSCI conduit (board interface).
 */

#ifndef FREEWEB_BOARD_RESET_H
#define FREEWEB_BOARD_RESET_H

/* Warm reset the SoC through PSCI SYSTEM_RESET. A successful reset never
 * returns; a return value means the firmware REFUSED the call (its PSCI
 * refusal code, e.g. NOT_SUPPORTED) and the caller keeps running. */
int board_system_reset(void);

#endif /* FREEWEB_BOARD_RESET_H */
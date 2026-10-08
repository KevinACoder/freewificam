/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    seam_drive.h
 * @brief   The suite's driver of the port's receive/event seams: it delivers a
 *          frame and an event through the port's own production paths, so the
 *          presentation layer under test sees them exactly the way the real
 *          receive path would present them.
 */

#ifndef SEAM_DRIVE_H
#define SEAM_DRIVE_H

#include <stdint.h>
#include <stddef.h>

/* classification the port itself performs (EAPOL vs data), named by scalars */
#define SEAM_DRIVE_EV_SCAN_DONE		0
#define SEAM_DRIVE_EV_ASSOC		1
#define SEAM_DRIVE_EV_DISASSOC		2

int	seam_drive_frame(const uint8_t *frame, size_t len);
int	seam_drive_event(int event, const uint8_t addr[6]);

#endif /* SEAM_DRIVE_H */

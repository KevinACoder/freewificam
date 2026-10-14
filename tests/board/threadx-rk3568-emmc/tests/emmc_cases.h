/**
 * SPDX-License-Identifier: BSD-3-Clause
 * @file    emmc_cases.h
 * @brief   What the eMMC suite's cases share: the card bring-up and the small
 *          helpers every case's evidence lines are built from.
 * @author  zhugengyu
 * @date    14.10.2026
 * @design  the PCIe/NVMe suite's pcie_cases.h, with the storage domain replaced
 *          by the eMMC line.
 */

#ifndef EMMC_CASES_H
#define EMMC_CASES_H

#include <stdint.h>

/* Bring the card up once per boot (idempotent): the controller, the stack's
 * card initialization ladder, and the block device's registration. Returns 0 on
 * success; on failure the reasons are already on the console. */
int emmc_bringup( void );

/* Milliseconds from the board's generic counter. */
uint32_t emmc_now_ms( void );

/* KiB/s over `bytes` in `ms` (callers add one to ms so it is never zero). */
uint32_t emmc_kib( uint32_t bytes, uint32_t ms );

/* The deterministic byte pattern the block and file cases write and verify: a
 * hash of the byte's offset, so a reader regenerates it instead of storing it.
 * The same generator the `stor wr` command uses (ports/fatfs/fatfs_cmds.c), so
 * a byte range written by one and read by the other compares equal. */
uint8_t emmc_pattern( uint32_t index );

/* FNV-1a over a buffer, continuing from `hash`. */
uint32_t emmc_fnv1a( const uint8_t *data, uint32_t len, uint32_t hash );

#endif /* EMMC_CASES_H */

/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   link.ld
 * @brief  Board link override for firefly v2: wraps the generic bare-metal
 *         script with this board's RAM window.
 *
 * The flat binary loads at 0x90000000 (the lab profile's load and `go`
 * address) and the usable image window tops at 0xc0000000. The numbers must
 * exist BEFORE the generic script's DEFINED() tests read them - which is why
 * this wrapper assigns first and INCLUDEs the generic script after (command-
 * line --defsym does NOT satisfy DEFINED() on this binutils).
 *
 * RESOLUTION CONSTRAINT (binutils 2.41): ld resolves INCLUDE against the
 * LINK WORKING DIRECTORY ONLY - not -L paths, not this script's own
 * directory. The image links from tests/board/rtos/build/, so the generic
 * script is one level up inside the platform directory.
 */

board_ram_base = 0x90000000;
board_ram_top = 0xc0000000;

INCLUDE "../platform/arch/aarch64/baremetal.ld"

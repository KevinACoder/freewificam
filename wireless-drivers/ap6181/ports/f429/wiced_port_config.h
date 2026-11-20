/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * wiced_port_config.h - the numbers and policy of the AP6181 port on the F429.
 *
 * One place for everything the vendor driver's defaults would otherwise decide:
 * the SDIO clock, the network-buffer headroom, the thread band, and the
 * diagnostic switches.  The port files include this before any vendor header so
 * a policy change cannot be half-applied.
 */

#ifndef AP6181_WICED_PORT_CONFIG_H
#define AP6181_WICED_PORT_CONFIG_H

#include <stdint.h>

/* --- SDIO bus ------------------------------------------------------------- */

#define AP6181_SDIO_INIT_HZ		400000UL	/* enumeration */
#define AP6181_SDIO_TRANSFER_HZ		24000000UL	/* the transfer state */
#define AP6181_SDIO_4BIT		1

/* The card's function-1 block size the host programs after enumeration. */
#define AP6181_SDIO_BLOCK_SIZE		64U

/* --- Buffers -------------------------------------------------------------- */

/*
 * Each WWD network buffer is an lwIP pbuf of PBUF_RAM; the headroom is what the
 * SDPCM/bus headers are prepended into before a transmit, and what a receive
 * leaves in front of the 802.3 frame.  64 bytes covers the 43362's SDPCM
 * header plus the bus header with room to spare.
 */
#define AP6181_BUFFER_HEADROOM		64U

/* --- Threads -------------------------------------------------------------- */

/*
 * WWD asks for RTOS_HIGHEST_PRIORITY for its thread.  On this platform that is
 * above the timer task and the runner, so the port maps it to a band below the
 * runner: the driver must not be able to starve the case that is waiting for
 * it.  Everything else is above the idle task.
 */
#define AP6181_WWD_THREAD_PRIORITY	6U
#define AP6181_WWD_THREAD_STACK		4096U

/* --- Timing --------------------------------------------------------------- */

#define AP6181_SDIO_COMMAND_TIMEOUT_MS	50U
#define AP6181_SDIO_ENUMERATE_MS	500U

#endif /* AP6181_WICED_PORT_CONFIG_H */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * wwd_buffer.h - what a WWD network buffer is on this port: an lwIP pbuf.
 *
 * The vendor driver treats a buffer as an opaque handle it can ask for a data
 * pointer, a current piece size, a next piece and a front adjustment on.  lwIP's
 * pbuf has exactly that shape, and the seam already speaks pbufs, so the port
 * uses it directly instead of a private slab: the transmit path hands the
 * driver the stack's own pbuf (one reference taken), and the receive path hands
 * the driver's pbuf to the stack.
 *
 * The headroom the driver prepends into is AP6181_BUFFER_HEADROOM, reserved by
 * host_buffer_get().
 */

#ifndef INCLUDED_WWD_BUFFER_H_
#define INCLUDED_WWD_BUFFER_H_

#include "lwip/pbuf.h"

#include "wwd_constants.h"
#include "wiced_constants.h"

typedef struct pbuf *wiced_buffer_t;

typedef struct {
	wiced_buffer_t first;
	wiced_buffer_t last;
} wiced_buffer_interface_fifo_t;

typedef struct {
	wiced_buffer_interface_fifo_t per_interface_fifos[WICED_INTERFACE_MAX];
} wiced_buffer_fifo_t;

#endif /* INCLUDED_WWD_BUFFER_H_ */
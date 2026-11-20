/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * wwd_buffer.c - WWD's buffer pool over lwIP pbufs.
 *
 * A WWD buffer is an lwIP pbuf (wwd_buffer.h).  host_buffer_get() allocates a
 * PBUF_RAM pbuf with AP6181_BUFFER_HEADROOM bytes in front and then strips that
 * headroom off the payload, so the driver sees exactly the size it asked for
 * and can still prepend the SDPCM/bus headers with
 * host_buffer_add_remove_at_front().  The transmit path passes the stack's own
 * pbuf down (one reference taken by the seam), so the common case allocates
 * nothing; the receive path allocates one pbuf per message and hands it to the
 * stack, which frees it.
 *
 * The counters are the case's evidence: a leak shows as tx_buffers/rx_buffers
 * not returning to zero, an exhausted heap shows as buffer_fail.
 */

#include <stdint.h>
#include <string.h>

#include "lwip/opt.h"
#include "lwip/pbuf.h"

#include "network/wwd_buffer_interface.h"
#include "platform/wwd_bus_interface.h"
#include "wwd_constants.h"
#include "wwd_debug.h"

#include "RTOS/wwd_rtos_interface.h"

#include "wiced_port.h"
#include "wiced_port_config.h"

struct ap6181_port_stats ap6181_stats;

static uint32_t s_tx_live;
static uint32_t s_rx_live;

wwd_result_t
wwd_buffer_init(void *native_arg)
{
	(void)native_arg;
	s_tx_live = 0U;
	s_rx_live = 0U;
	return WWD_SUCCESS;
}

wwd_result_t
wwd_buffer_deinit(void)
{
	return WWD_SUCCESS;
}

wwd_result_t
internal_host_buffer_get(wiced_buffer_t *buffer, wwd_buffer_dir_t direction,
			 unsigned short size, unsigned long timeout_ms)
{
	if (buffer == NULL || size == 0U)
		return WWD_BUFFER_UNAVAILABLE_PERMANENT;
	if (size > (unsigned short)WICED_LINK_MTU)
		return WWD_BUFFER_UNAVAILABLE_PERMANENT;

	for (;;) {
		struct pbuf *p = pbuf_alloc(PBUF_RAW,
					    (u16_t)(size + AP6181_BUFFER_HEADROOM),
					    PBUF_RAM);

		if (p != NULL) {
			if (pbuf_remove_header(p, AP6181_BUFFER_HEADROOM) != 0U) {
				(void)pbuf_free(p);
				ap6181_stats.buffer_fail++;
				return WWD_BUFFER_UNAVAILABLE_TEMPORARY;
			}
			*buffer = p;
			if (direction == WWD_NETWORK_TX) {
				s_tx_live++;
				ap6181_stats.tx_buffers = s_tx_live;
			} else {
				s_rx_live++;
				ap6181_stats.rx_buffers = s_rx_live;
			}
			return WWD_SUCCESS;
		}

		if (timeout_ms == 0UL)
			break;
		if (timeout_ms != NEVER_TIMEOUT)
			timeout_ms--;
		(void)host_rtos_delay_milliseconds(1U);
	}

	ap6181_stats.buffer_fail++;
	return WWD_BUFFER_UNAVAILABLE_TEMPORARY;
}

wwd_result_t
host_buffer_get(wiced_buffer_t *buffer, wwd_buffer_dir_t direction,
		unsigned short size, wiced_bool_t wait)
{
	return internal_host_buffer_get(buffer, direction, size,
					(wait == WICED_TRUE) ? NEVER_TIMEOUT : 0UL);
}

void
host_buffer_release(wiced_buffer_t buffer, wwd_buffer_dir_t direction)
{
	if (buffer == NULL)
		return;

	if (direction == WWD_NETWORK_TX) {
		if (s_tx_live > 0U)
			s_tx_live--;
		ap6181_stats.tx_buffers = s_tx_live;
	} else {
		if (s_rx_live > 0U)
			s_rx_live--;
		ap6181_stats.rx_buffers = s_rx_live;
	}
	(void)pbuf_free(buffer);
}

uint8_t *
host_buffer_get_current_piece_data_pointer(wiced_buffer_t buffer)
{
	return (buffer != NULL) ? (uint8_t *)buffer->payload : NULL;
}

uint16_t
host_buffer_get_current_piece_size(wiced_buffer_t buffer)
{
	return (buffer != NULL) ? (uint16_t)buffer->len : 0U;
}

wiced_buffer_t
host_buffer_get_next_piece(wiced_buffer_t buffer)
{
	return (buffer != NULL) ? buffer->next : NULL;
}

wwd_result_t
host_buffer_set_size(wiced_buffer_t buffer, unsigned short size)
{
	if (buffer == NULL)
		return WWD_BUFFER_SIZE_SET_ERROR;
	if (size > (unsigned short)WICED_LINK_MTU)
		return WWD_BUFFER_SIZE_SET_ERROR;
	buffer->len = size;
	buffer->tot_len = size;
	return WWD_SUCCESS;
}

wwd_result_t
host_buffer_add_remove_at_front(wiced_buffer_t *buffer, int32_t add_remove_amount)
{
	uint8_t rc;

	if (buffer == NULL || *buffer == NULL)
		return WWD_BUFFER_POINTER_MOVE_ERROR;

	if (add_remove_amount < 0) {
		if ((uint32_t)(-add_remove_amount) > 0xFFFFU)
			return WWD_BUFFER_POINTER_MOVE_ERROR;
		rc = pbuf_add_header(*buffer, (u16_t)(-add_remove_amount));
	} else {
		if ((uint32_t)add_remove_amount > 0xFFFFU)
			return WWD_BUFFER_POINTER_MOVE_ERROR;
		rc = pbuf_remove_header(*buffer, (u16_t)add_remove_amount);
	}
	return (rc == 0U) ? WWD_SUCCESS : WWD_BUFFER_POINTER_MOVE_ERROR;
}

wwd_result_t
host_buffer_check_leaked(void)
{
	return (s_tx_live == 0U && s_rx_live == 0U) ?
		WWD_SUCCESS : WWD_BUFFER_UNAVAILABLE_PERMANENT;
}

uint32_t
ap6181_buffer_tx_live(void)
{
	return s_tx_live;
}

uint32_t
ap6181_buffer_rx_live(void)
{
	return s_rx_live;
}

void
host_buffer_init_fifo(wiced_buffer_fifo_t *fifo)
{
	if (fifo != NULL)
		memset(fifo, 0, sizeof(*fifo));
}

void
host_buffer_push_to_fifo(wiced_buffer_fifo_t *fifo, wiced_buffer_t buffer,
			 wwd_interface_t interface)
{
	wiced_buffer_interface_fifo_t *f;

	if (fifo == NULL || buffer == NULL)
		return;
	if (interface >= WWD_INTERFACE_MAX)
		return;
	f = &fifo->per_interface_fifos[WWD_INTERFACE_INDEX(interface)];
	((struct pbuf *)buffer)->next = NULL;
	if (f->first == NULL) {
		f->first = buffer;
		f->last = buffer;
	} else {
		((struct pbuf *)f->last)->next = buffer;
		f->last = buffer;
	}
}

wiced_buffer_t
host_buffer_pop_from_fifo(wiced_buffer_fifo_t *fifos,
			  wwd_interface_t *interface_return)
{
	wiced_buffer_t buffer = NULL;
	int index;

	if (fifos == NULL)
		return NULL;
	for (index = 0; index < WWD_INTERFACE_MAX; index++) {
		wiced_buffer_interface_fifo_t *f = &fifos->per_interface_fifos[index];

		if (f->first != NULL) {
			buffer = f->first;
			f->first = ((struct pbuf *)buffer)->next;
			if (f->first == NULL)
				f->last = NULL;
			if (interface_return != NULL)
				*interface_return = WWD_INDEX_TO_INTERFACE(index);
			break;
		}
	}
	return buffer;
}

wiced_bool_t
host_buffer_pool_is_full(wwd_buffer_dir_t direction)
{
	wiced_buffer_t buffer;

	if (host_buffer_get(&buffer, direction, 1U, WICED_FALSE) == WWD_SUCCESS) {
		host_buffer_release(buffer, direction);
		return WICED_FALSE;
	}
	return WICED_TRUE;
}

wwd_result_t
host_buffer_add_application_defined_pool(void *pool_in,
					 wwd_buffer_dir_t direction)
{
	(void)pool_in;
	(void)direction;
	return WWD_UNSUPPORTED;
}
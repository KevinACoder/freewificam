/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * wiced_port.h - the port's own face: diagnostics and the lwIP seam.
 *
 * The vendor WWD core calls host_* entry points and one network hand-off; the
 * board cases read the counters below and nothing else.  Keeping the case's
 * vocabulary here means a driver-internal rename cannot change what a board
 * image prints.
 */

#ifndef AP6181_WICED_PORT_H
#define AP6181_WICED_PORT_H

#include <stdint.h>

#include "lwip/err.h"
#include "lwip/netif.h"

/* One snapshot of everything the port counts.  The case prints it once per
 * stage, so a failing gate comes with its own evidence. */
struct ap6181_port_stats {
	uint32_t sdio_intrs;	/* controller interrupts taken */
	uint32_t cmd_retries;	/* transfers restarted after an error */
	uint32_t cmd_timeouts;	/* command/data waits that expired */
	uint32_t dma_errors;	/* DMA stream error flags seen */
	uint32_t oob_intrs;	/* module out-of-band line wakes */
	uint32_t tx_buffers;	/* outstanding WWD transmit buffers */
	uint32_t rx_buffers;	/* outstanding WWD receive buffers */
	uint32_t buffer_fail;	/* buffer allocations that came back empty */
	uint32_t rx_frames;	/* frames handed to the stack */
	uint32_t tx_frames;	/* frames handed to the driver */
	uint32_t rx_dropped;	/* frames the seam dropped */
	uint16_t rca;		/* the enumerated card's relative address */
	uint8_t  bus_width;	/* 1 or 4 */
	uint8_t  enumerated;	/* the card answered CMD3/CMD7 */
	uint32_t clock_hz;	/* the transfer-state clock the host programmed */
	/* The last failing transfer's shape: what command, what argument, and
	 * the controller's state when it gave up.  A bring-up failure reads the
	 * host's own view instead of guessing from the code. */
	uint32_t last_cmd;
	uint32_t last_arg;
	uint32_t last_sta;
	uint32_t last_dlen;
	uint32_t last_dctrl;
	uint32_t enum_attempts;
	uint32_t dma_intrs;	/* DMA stream 3 completion/error interrupts */
	uint32_t cmd53_isr;	/* CMDREND observed for a CMD53 */
	uint32_t cmd53_dma;	/* DMA streams started for a CMD53 */
	uint32_t last_ndtr;	/* the stream's count at the last failure */
	uint32_t last_err_sta;	/* the controller's flags the ISR left */
	uint32_t last_isr_sta;
	uint32_t cmd52_count;
	uint32_t last52_arg;
	uint32_t last52_resp;
};

extern struct ap6181_port_stats ap6181_stats;

/* The lwIP interface seam.  The case adds the netif with ap6181_netif_init,
 * then calls ap6181_netif_attach once so the receive hand-off knows where to
 * put frames. */
err_t ap6181_netif_init(struct netif *netif);
void ap6181_netif_attach(struct netif *netif);

/* The stack's memory pools must exist before the driver asks for its first
 * buffer (a WWD buffer is a pbuf).  The stages without a netif only need the
 * core; the data stage starts the TCP/IP thread because its netif input is
 * tcpip_input.  Each image calls exactly one of them once. */
void ap6181_lwip_core_init(void);
int  ap6181_lwip_stack_start(void);

#endif /* AP6181_WICED_PORT_H */
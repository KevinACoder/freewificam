/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   net_stack.c
 * @brief  The one-shot lwIP stack bring-up shared by the wired-NIC cases.
 *
 * One boot may run more than one NIC case (the board carries two wired
 * NICs); lwIP's stack initialisation (tcpip_init) is once-per-boot by
 * contract - a second call re-creates the stack's own thread and mailboxes
 * and dies on a netif assertion (first board evidence: 'netif already
 * added', first GMAC round). This wrapper makes the bring-up idempotent:
 * the first caller runs it, every later caller reuses the result.
 */

#include <stddef.h>

#include "cases.h"

#include "os/cmsis2/cmsis2.h"

#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "lwip/apps/lwiperf.h"

static osSemaphoreId_t stack_ready;
static int stack_state;		/* 0 = cold, 1 = up, -1 = failed */

static void stack_done(void *arg)
{
	(void)arg;
	CASE_INFO("net: stack ready lwip=%s\n", LWIP_VERSION_STRING);
	stack_state = 1;
	(void)osSemaphoreRelease(stack_ready);
}

int case_net_stack_up(void)
{
	if (stack_state != 0) {
		return stack_state == 1 ? 0 : -1;
	}

	stack_ready = osSemaphoreNew(1u, 0u, NULL);
	if (stack_ready == NULL) {
		stack_state = -1;
		return -1;
	}

	tcpip_init(stack_done, NULL);

	if (osSemaphoreAcquire(stack_ready, 5000u) != osOK) {
		stack_state = -1;
		return -1;
	}
	return 0;
}

/*
 * The one lwiperf TCP server for the whole boot. Only one listener can own
 * the port; a second NIC case starting its own server fails (first board
 * evidence) and two client sessions sharing one report callback race their
 * teardown into lwiperf's pcb bookkeeping.
 */
static int lwiperf_server_state;

void case_net_lwiperf_server_start(void)
{
	if (stack_state != 1 || lwiperf_server_state != 0) {
		return;
	}

	LOCK_TCPIP_CORE();
	lwiperf_server_state =
		lwiperf_start_tcp_server_default(NULL, NULL) != NULL ? 1 : -1;
	UNLOCK_TCPIP_CORE();
	/* The port is the library's default (5001); naming it explicitly here
	 * keeps the console line and the bench command from drifting apart -
	 * the line said 5201 while the listener was on 5001. */
	CASE_INFO("net: lwiperf server %s on :%u\n",
		  lwiperf_server_state == 1 ? "listening" : "FAILED",
		  (unsigned)LWIPERF_TCP_PORT_DEFAULT);
}

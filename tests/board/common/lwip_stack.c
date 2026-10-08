/*
 * @file    lwip_stack.c
 * @brief   One-time lwIP stack bring-up shared by this image's network cases.
 * @design  registry-free singleton in the case layer: tcpip_init is the
 *          stack's own documented single entry (third-party/lwip/src/api/
 *          tcpip.c:659-675 - it initialises the stack, creates the TCP/IP
 *          thread and runs the caller's callback on that thread), so the
 *          only local state needed is "has it run" plus the semaphore the
 *          callback releases.
 */

#include "board_suite.h"

#include "os/cmsis2/cmsis2.h"

#include "lwip/init.h"
#include "lwip/tcpip.h"

#include "log.h"

#include "lwip_stack.h"

static osSemaphoreId_t stack_ready;
static int stack_started;
static int stack_is_up;

static void stack_done(void *arg)
{
	(void)arg;
	board_log("net: READY tcpip=up lwip=%s\n", LWIP_VERSION_STRING);
	(void)osSemaphoreRelease(stack_ready);
}

int lwip_stack_up(void)
{
	if (stack_is_up) {
		return 0;
	}
	if (stack_ready == NULL) {
		stack_ready = osSemaphoreNew(1u, 0u, NULL);
		if (stack_ready == NULL) {
			return -1;
		}
	}

	if (!stack_started) {
		stack_started = 1;
		tcpip_init(stack_done, NULL);
	}

	/* The callback releases the semaphore once; later callers read the
	 * sticky flag instead of consuming a second count. */
	if (osSemaphoreAcquire(stack_ready, osKernelGetTickFreq() * 5u) != osOK) {
		board_log("net: stack bring-up callback never ran (5s)\n");
		return -1;
	}
	stack_is_up = 1;

	return 0;
}

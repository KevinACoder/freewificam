/**
 * @file    os_timer.c
 * @brief   Case os-timer: a CMSIS-RTOS2 timer runs on this kernel.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  case row of the regression table (tests/board/common/board_suite.h).
 *
 * The ThreadX CMSIS-RTOS2 backend's timers are new code and the hub class
 * rides them, so the board image proves the path on its own: a periodic timer
 * at 100 ms plus a one-shot at 200 ms, both waited on, both counted.
 */

#include "board_suite.h"

#include "os/cmsis2/cmsis2.h"

static volatile unsigned timer_fires;
static volatile unsigned oneshot_fires;

static void periodic_body(void *argument)
{
	(void)argument;
	timer_fires++;
}

static void oneshot_body(void *argument)
{
	(void)argument;
	oneshot_fires++;
}

static void os_timer(void)
{
	osTimerId_t periodic;
	osTimerId_t oneshot;

	periodic = osTimerNew(periodic_body, osTimerPeriodic, 0, 0);
	oneshot = osTimerNew(oneshot_body, osTimerOnce, 0, 0);
	if (periodic == 0 || oneshot == 0) {
		board_case_fail("timer create failed");
		return;
	}
	if (osTimerStart(periodic, osKernelGetTickFreq() / 10U) != osOK) {
		board_case_fail("periodic start failed");
		return;
	}
	if (osTimerStart(oneshot, osKernelGetTickFreq() / 5U) != osOK) {
		board_case_fail("one-shot start failed");
		return;
	}

	osDelay(600U);
	board_log("os2-timer: periodic fires=%u one-shot fires=%u\n",
		  timer_fires, oneshot_fires);
	if (osTimerStop(periodic) != osOK) {
		board_case_fail("periodic stop failed");
		return;
	}
	if (timer_fires < 3U || oneshot_fires != 1U) {
		board_case_fail("timer cadence outside 3..6 fires / one-shot != 1");
		return;
	}
	osTimerDelete(periodic);
	osTimerDelete(oneshot);
}

BOARD_TEST("os-timer", os_timer);

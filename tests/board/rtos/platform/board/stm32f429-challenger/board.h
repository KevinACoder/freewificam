#ifndef BRINGUP_BOARD_H
#define BRINGUP_BOARD_H

#include <stdint.h>

extern uint32_t SystemCoreClock;
extern uint32_t g_clock_fallback;

void SystemInit(void);

#endif

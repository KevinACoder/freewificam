/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   cases.h
 * @brief  The case interface shared by the board image and the host tier.
 *
 * One case = one group of checks that prints its own anchor lines and returns
 * 0 on pass.  A case never stops the image: the runner records the verdict and
 * moves to the next group, so one failing group still leaves the others'
 * evidence in the same boot.
 *
 * The anchor text is the contract the board runner judges (tools/rtos/judge.py
 * and the expect file): it is fixed, greppable and prefixed.  Changing a line
 * here means changing the judge in the same commit.
 */

#ifndef RTOS_APP_CASES_H
#define RTOS_APP_CASES_H

#include <stdint.h>

#include "clock.h"

#if defined(RTOS_HOST_TIER)
#include <stdio.h>
#define CASE_INFO(...)   printf(__VA_ARGS__)
#define CASE_ANCHOR(...) printf(__VA_ARGS__)
#else
#include "log.h"
#define CASE_INFO(...)   LOG_INFO(__VA_ARGS__)
#define CASE_ANCHOR(...) LOG_ANCHOR(__VA_ARGS__)
#endif

/* Each case is compiled only when its switch is on, so a short arm builds a
 * short image (`make CASES=tick,cmsis2`). */
#if defined(RTOS_CASE_TICK)
int case_tick_run(void);
#endif
#if defined(RTOS_CASE_CMSIS2)
int case_cmsis2_run(void);
#endif
#if defined(RTOS_CASE_NET)
int case_net_run(void);
#endif
#if defined(RTOS_CASE_EMMC)
int case_emmc_run(void);
#endif

#endif /* RTOS_APP_CASES_H */
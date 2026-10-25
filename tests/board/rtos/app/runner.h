/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   runner.h
 * @brief  The case runner: starts the kernel's only application thread.
 */

#ifndef RTOS_APP_RUNNER_H
#define RTOS_APP_RUNNER_H

/* Create the runner thread and return.  The caller starts the scheduler; the
 * runner then walks the compiled-in case list and prints the summary line.
 * Returns 0 when the thread was created. */
int rtos_runner_start(void);

#endif /* RTOS_APP_RUNNER_H */
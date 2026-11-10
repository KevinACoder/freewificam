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

/* The compiled-in case table, exposed to the shell command layer.  One table:
 * the runner and the `case` commands walk the same entries. */
unsigned rtos_case_count(void);
const char *rtos_case_name(unsigned index);
int rtos_case_find(const char *name);
int rtos_case_run(unsigned index);

#endif /* RTOS_APP_RUNNER_H */
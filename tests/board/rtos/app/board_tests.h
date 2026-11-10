/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   board_tests.h
 * @brief  The image's case table as a linker section.
 *
 * A case is one {name, fn} pair in the BOARD_TESTS input section.  The linker
 * script keeps the section (KEEP) and brackets it with __board_tests_start /
 * __board_tests_end, so the runner, the shell `case` command and the host-side
 * extractor all read the same table.  The host tier has no linker script and
 * keeps its plain static array; it uses CASE_ENTRY instead of BOARD_TEST.
 */

#ifndef RTOS_APP_BOARD_TESTS_H
#define RTOS_APP_BOARD_TESTS_H

struct rtos_case {
	const char *name;
	int (*run)(void);
};

#if !defined(RTOS_HOST_TIER)
#define BOARD_TEST(name, fn) \
	static const struct rtos_case __rtos_case_##fn \
	__attribute__((used, section("BOARD_TESTS"))) = { name, fn }
#endif

#endif /* RTOS_APP_BOARD_TESTS_H */

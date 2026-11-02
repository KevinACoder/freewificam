/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   shell_cmds.h
 * @brief  Entry points of the board command layer used outside the REPL.
 */

#ifndef FREEWIFICAM_SHELL_CMDS_H
#define FREEWIFICAM_SHELL_CMDS_H

/* Run the CONFIG_SHELL_AUTORUN list once, with the legacy runner anchors.
 * Empty list = interactive boot: nothing is executed. */
void shell_autorun(void);

#endif /* FREEWIFICAM_SHELL_CMDS_H */

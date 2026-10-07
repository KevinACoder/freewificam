/*
 * CherrySH configuration for this project's console port.
 *
 * The component's csh.h includes "csh_config.h"; this file is that include
 * object (the upstream csh_config_template.h next to the component is kept as
 * the reference, it is never the one compiled). Values follow the upstream
 * template except where the port's chosen shape says otherwise:
 *
 * - CONFIG_CSH_NOBLOCK = 0: the console backends block until a byte arrives,
 *   so the repl loop has no "no data yet" branch and needs no event glue.
 * - CONFIG_CSH_MULTI_THREAD = 0: one repl task runs commands inline; the
 *   execution-thread mode is a reserved upgrade path.
 * - CONFIG_CSH_LNBUFF_STATIC = 1: the line buffer is a static array owned by
 *   the port (MCU-compliant, and the pair of checks in chry_shell.h requires
 *   it together with the two switches above).
 */

#ifndef CSH_CONFIG_H
#define CSH_CONFIG_H

/*!< argument check */
#define CONFIG_CSH_DEBUG 0

/*!< default row */
#define CONFIG_CSH_DFTROW 25

/*!< default column */
#define CONFIG_CSH_DFTCOL 80

/*!< history support <+550byte> */
#define CONFIG_CSH_HISTORY 1

/*!< completion support <+1100byte> */
#define CONFIG_CSH_COMPLETION 1

/*!< max completion item list count (use stack 4 x count byte) */
#define CONFIG_CSH_MAX_COMPLETION 40

/*!< prompt edit support <+1000byte>  */
#define CONFIG_CSH_PROMPTEDIT 1

/*!< prompt segment count */
#define CONFIG_CSH_PROMPTSEG 7

/*!< xterm support */
#define CONFIG_CSH_XTERM 0

/*!< newline */
#define CONFIG_CSH_NEWLINE "\r\n"

/*!< tab space count */
#define CONFIG_CSH_SPACE 4

/*!< independent ctrl map */
#define CONFIG_CSH_CTRLMAP 0

/*!< independent alt map */
#define CONFIG_CSH_ALTMAP 0

/*!< refresh prompt */
#define CONFIG_CSH_REFRESH_PROMPT 1

/*!< no waiting for sget (0: the backend blocks until data arrives) */
#define CONFIG_CSH_NOBLOCK 0

/*!< help information */
#define CONFIG_CSH_HELP ""

/*!< path length 0:const path, <=255:variable path */
#define CONFIG_CSH_MAXLEN_PATH 128

/*!< path segment count */
#define CONFIG_CSH_MAXSEG_PATH 16

/*!< user count */
#define CONFIG_CSH_MAX_USER 1

/*!< max argument count */
#define CONFIG_CSH_MAX_ARG 8

/*!< linebuffer static or on stack */
#define CONFIG_CSH_LNBUFF_STATIC 1

/*!< linebuffer size (valid only if lnbuff on stack) */
#define CONFIG_CSH_LNBUFF_SIZE 256

/*!< multi-thread mode */
#define CONFIG_CSH_MULTI_THREAD 0

/*!< independent signal handler (for multi instances) */
#define CONFIG_CSH_SIGNAL_HANDLER 0

/*!< Ctrl+c/d/q/s/z/\ F1-F12 UE <+120byte> */
#define CONFIG_CSH_USER_CALLBACK 1

/*!< enable macro export symbol table */
#define CONFIG_CSH_SYMTAB 1

/*!< print buffer size */
#define CONFIG_CSH_PRINT_BUFFER_SIZE 512

#endif

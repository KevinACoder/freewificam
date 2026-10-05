/*
 * @file   csh_config.h
 * @brief  CherrySH configuration for this board (adapter shadow config).
 *
 * CherrySH ships only a template (`components/cherrysh/csh_config_template.h`)
 * and expects the integrator to supply `csh_config.h`. It lives here rather
 * than in components/ because components/ is vendored byte-identical and
 * never edited; all adaptation belongs in port/adapters/cherrysh/. The
 * include order (this directory FIRST on the shell objects' include path)
 * is what makes the shadow win (SPEC cherrysh-adapt §3.8.2).
 *
 * The choices worth explaining:
 *
 *  - CONFIG_CSH_NOBLOCK=1: the REPL must not block inside sget(). The shell
 *    task sleeps on a thread-flag wait and re-enters the REPL when bytes
 *    arrive, so a task that blocked in sget() would never yield and would
 *    starve every lower-priority task. This is what CONFIG_CSH_LNBUFF_STATIC=1
 *    is required for (chry_shell.h #errors on the combination otherwise) -
 *    with noblock, readline state has to survive between calls, so the line
 *    buffer cannot be on the stack of a call that returns early.
 *
 *  - CONFIG_CSH_SYMTAB=1: commands come from the FSymTab/VSymTab linker
 *    sections (see CSH_CMD_EXPORT* in csh.h). baremetal.ld KEEP()s those
 *    sections, so exported commands cannot be discarded.
 *
 *  - CONFIG_CSH_XTERM=0: the console is a plain serial line. Xterm-specific
 *    escape sequences would emit bytes a terminal emulator has to filter, and
 *    the reader used to capture board logs is line-based, so extra control
 *    traffic makes bring-up output harder to read, not easier.
 *
 *  - CONFIG_CSH_MULTI_THREAD=0: one shell instance. The multi-thread path
 *    calls chry_shell_port_create_context(), which would need a per-command
 *    task; not wanted here.
 *
 * The rest match the template's defaults (values carried over from the
 * prior project's board-proven config, SPEC cherrysh-adapt §3.2.3).
 */

#ifndef CSH_CONFIG_H
#define CSH_CONFIG_H

/*!< argument check */
#define CONFIG_CSH_DEBUG 0

/*!< default row */
#define CONFIG_CSH_DFTROW 25

/*!< default column */
#define CONFIG_CSH_DFTCOL 80

/*!< history support */
#define CONFIG_CSH_HISTORY 1

/*!< completion support */
#define CONFIG_CSH_COMPLETION 1

/*!< max completion item list count */
#define CONFIG_CSH_MAX_COMPLETION 40

/*!< prompt edit support */
#define CONFIG_CSH_PROMPTEDIT 1

/*!< prompt segment count */
#define CONFIG_CSH_PROMPTSEG 7

/*!< xterm support: off, the console is a plain line-oriented serial link */
#define CONFIG_CSH_XTERM 0

/*!< newline: '\n' only - the console sink owns the CRLF expansion
 *   (print discipline). The vendored shell pastes this macro into its own
 *   strings, so a '\r\n' here would reach the wire as CR CR LF. */
#define CONFIG_CSH_NEWLINE "\n"

/*!< tab space count */
#define CONFIG_CSH_SPACE 4

/*!< independent ctrl map */
#define CONFIG_CSH_CTRLMAP 0

/*!< independent alt map */
#define CONFIG_CSH_ALTMAP 0

/*!< refresh prompt */
#define CONFIG_CSH_REFRESH_PROMPT 1

/*!< no waiting for sget: see the note above */
#define CONFIG_CSH_NOBLOCK 1

/*!< help information */
#define CONFIG_CSH_HELP ""

/*!< path length */
#define CONFIG_CSH_MAXLEN_PATH 128

/*!< path segment count */
#define CONFIG_CSH_MAXSEG_PATH 16

/*!< user count */
#define CONFIG_CSH_MAX_USER 1

/*!< max argument count */
#define CONFIG_CSH_MAX_ARG 8

/*!< linebuffer static: REQUIRED by CONFIG_CSH_NOBLOCK=1 */
#define CONFIG_CSH_LNBUFF_STATIC 1

/*!< linebuffer size */
#define CONFIG_CSH_LNBUFF_SIZE 256

/*!< multi-thread mode */
#define CONFIG_CSH_MULTI_THREAD 0

/*!< independent signal handler */
#define CONFIG_CSH_SIGNAL_HANDLER 0

/*!< Ctrl+c/d/q/s/z and F1-F12 */
#define CONFIG_CSH_USER_CALLBACK 1

/*!< enable macro export symbol table (FSymTab / VSymTab) */
#define CONFIG_CSH_SYMTAB 1

/*!< print buffer size */
#define CONFIG_CSH_PRINT_BUFFER_SIZE 512

#endif /* CSH_CONFIG_H */
